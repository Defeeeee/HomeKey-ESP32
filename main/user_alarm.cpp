#include "include/user_alarm.h"
#include "include/EventLog.hpp"
#include <Arduino.h>
#include <WiFi.h>
#include "include/NfcManager.hpp"
#include <string>
#include <vector>
#include "include/app_event_loop.hpp"
#include "include/app_events.hpp"
#include "include/ConfigManager.hpp"
#include "include/MqttManager.hpp"
#include "include/WebServerManager.hpp"
#include <sodium/crypto_sign.h>
#include <sodium/crypto_box.h>
#include "HAP.h"
#include <HomeSpan.h>
#include <dscKeybusInterface.h>
#include <nvs.h>
#include <nvs_flash.h>

enum AlarmMode { DISARMED, ARMING_AWAY, ARMING_HOME, ARMED_AWAY, ARMED_HOME, ENTRY_DELAY, TRIGGERED };
AlarmMode currentMode = DISARMED;
struct ZoneConfig {
    uint8_t id;         // ID de Zona (1-based: 1 a 8)
    int pin;            // Pin GPIO en el ESP32
    int sensorIndex;    // Índice en el array 'sensors' (0-based)
    bool lastPinReading;
    unsigned long lastDebounceTime;
};

ZoneConfig physicalZones[8] = {
    {1, 13, 0, HIGH, 0},
    {2, 17, 1, HIGH, 0},
    {3, 14, 2, HIGH, 0},
    {4, 25, 3, HIGH, 0},
    {5, 26, 4, HIGH, 0},
    {6, 27, 5, HIGH, 0},
    {7, 32, 6, HIGH, 0},
    {8, 255, 7, HIGH, 0}
};

const unsigned long DEBOUNCE_DELAY = 50; // ms
unsigned long lastSamplingTime = 0;
const unsigned long SAMPLING_INTERVAL = 20; // Muestreo cada 20ms
bool sensors[8] = {false, false, false, false, false, false, false, false};
AppEventLoop::SubscriptionHandle m_remote_event;

unsigned long armingStartTime = 0;
const unsigned long EXIT_DELAY_MS = 15000; // 15 segundos de cuenta atrás (DSC-aligned)
int lastSecondsLeft = -1;

unsigned long entryDelayStartTime = 0;
const unsigned long ENTRY_DELAY_MS = 15000; // 15 segundos de retardo de entrada
int lastEntrySecondsLeft = -1;
AlarmMode armedModeBeforeDelay = ARMED_AWAY;

// MQTT connectivity nudge (NON-REBOOTING by design — see the block in
// user_alarm_loop()). An earlier version esp_restart()'d here after 3 min without
// MQTT, which turned ordinary broker outages (Wi-Fi perfectly fine) into an endless
// reboot loop and made the whole system unstable. We no longer reboot; we only give
// the Wi-Fi a single gentle kick after a long outage, to recover the rare "zombie"
// link case (radio associated, data path dead) without touching a healthy system.
unsigned long lastMqttConnectedTime = 0;
unsigned long lastWifiNudgeTime = 0;
const unsigned long MQTT_DOWN_NUDGE_MS = 600000;     // 10 min continuously without MQTT
const unsigned long WIFI_NUDGE_COOLDOWN_MS = 600000; // at most one Wi-Fi nudge per 10 min

// DSC Keybus Interface Global Instance (Clock: 21, Read: 18, Write: 19)
dscKeypadInterface dsc(21, 18, 19);
unsigned long lastKeypadBeepTime = 0;
std::string keypadPinBuffer = "";
bool isCommandMode = false;
bool zoneBypassed[8] = {false, false, false, false, false, false, false, false};
bool chimeEnabled = false;
bool zoneAlarmMemory[8] = {false, false, false, false, false, false, false, false};
bool hasAlarmMemory = false;
bool isBypassMode = false;
bool isMemoryMode = false;
bool isTroubleMode = false;
bool isSimulateMode = false;
bool isWaitingForInstallerCode = false;
bool isInstallerMode = false;
bool isRssiMeterMode = false;
bool isMacroMode = false;
bool isSirenTestActive = false;
unsigned long lastKeypadActivityTime = 0;

void broadcast_ui_update();

extern "C" void user_alarm_siren_test(bool active) {
    isSirenTestActive = active;
    if (!active) {
        dsc.buzzer(0);
        dsc.beep(0);
    }
    broadcast_ui_update();
}

extern "C" bool user_alarm_is_siren_testing() {
    return isSirenTestActive;
}
unsigned long lastSystemActivityTime = 0;

void save_alarm_state(AlarmMode mode) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("SAVED_DATA", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_u8(my_handle, "alarm_state_val", (uint8_t)mode);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        Serial.printf("💾 [ALARM] Guardado estado de alarma en NVS: %d\n", mode);
    }
}

AlarmMode restore_alarm_state() {
    nvs_handle_t my_handle;
    uint8_t mode_val = (uint8_t)DISARMED;
    esp_err_t err = nvs_open("SAVED_DATA", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        err = nvs_get_u8(my_handle, "alarm_state_val", &mode_val);
        nvs_close(my_handle);
        if (err == ESP_OK) {
            Serial.printf("💾 [ALARM] Restaurado estado de alarma desde NVS: %d\n", mode_val);
            return (AlarmMode)mode_val;
        }
    }
    return DISARMED;
}

// Event-log source attribution: set at the outermost action entry points
// (keypad / remote / auto-arm) just before calling arm/disarm; consumed when the
// resulting ARMED/DISARMED transition is logged in update_current_mode() below.
static eventlog::Source g_pendingSource = eventlog::Source::SYSTEM;
static uint8_t g_lastTriggerZone = 0; // zone (1-8) that caused the next TRIGGERED, 0 if unknown

void update_current_mode(AlarmMode mode) {
    if (currentMode != mode) {
        currentMode = mode;
        save_alarm_state(mode);

        // Persistent audit/diagnostic trail. Restore-on-boot sets currentMode
        // directly (not through this function), so these fire only on real
        // runtime transitions — never a spurious "armed" event at startup.
        uint8_t src = static_cast<uint8_t>(g_pendingSource);
        switch (mode) {
            case ARMED_AWAY:
                eventlog::add(eventlog::EventType::ARMED_AWAY, src);
                g_pendingSource = eventlog::Source::SYSTEM;
                break;
            case ARMED_HOME:
                eventlog::add(eventlog::EventType::ARMED_HOME, src);
                g_pendingSource = eventlog::Source::SYSTEM;
                break;
            case DISARMED:
                eventlog::add(eventlog::EventType::DISARMED, src);
                g_pendingSource = eventlog::Source::SYSTEM;
                break;
            case ENTRY_DELAY:
                eventlog::add(eventlog::EventType::ENTRY_DELAY);
                break;
            case TRIGGERED:
                eventlog::add(eventlog::EventType::TRIGGERED, g_lastTriggerZone);
                break;
            default:
                break; // ARMING_AWAY / ARMING_HOME are transient — not logged
        }
    }
}

extern std::unique_ptr<ConfigManager> configManager;
extern std::unique_ptr<MqttManager> mqttManager;
extern std::unique_ptr<WebServerManager> webServerManager;
extern std::unique_ptr<NfcManager> nfcManager;

void print_status();
void broadcast_ui_update() {
    if (webServerManager) {
        webServerManager->broadcastDeviceMetrics();
    }
}

void mqtt_publish_state(const char* state) {
    lastKeypadActivityTime = millis();
    dsc.lightBacklight = on;
    AppEventLoop::publish(ALARM_EVENT, ALARM_STATE_CHANGED, (const uint8_t*)state, strlen(state));
    broadcast_ui_update();
}

// Maps raw DSC Keybus codes to standard characters
static char decodeDscKey(byte rawCode) {
    switch(rawCode) {
        case 5:  return '1';
        case 10: return '2';
        case 15: return '3';
        case 17: return '4';
        case 22: return '5';
        case 27: return '6';
        case 28: return '7';
        case 34: return '8';
        case 39: return '9';
        case 40: return '*';
        case 45: return '#';
        case 0:  return '0'; 
        case 0xAF: return 's'; // Stay key
        case 0xB1: return 'a'; // Away key
        default: return '?'; 
    }
}

// Global helper to process zone transitions and state rules
void trigger_zone_change(int zoneIdx, bool isOpen, const char* sourceName) {
    if (zoneIdx < 0 || zoneIdx >= 8) return;
    lastSystemActivityTime = millis();
    
    if (user_alarm_is_zone_disabled(zoneIdx)) {
        sensors[zoneIdx] = false;
        return;
    }
    
    sensors[zoneIdx] = isOpen;
    int zoneId = zoneIdx + 1;
    if (mqttManager && strcmp(sourceName, "MQTT-RF") != 0) mqttManager->publishSensorState(zoneId, isOpen);
    Serial.printf("⚡ [%s] Cambio en Zona %d: %s\n", sourceName, zoneId, isOpen ? "OPEN" : "CLOSED");
    broadcast_ui_update();

    // If the zone is bypassed, do NOT trigger any alarm/chime logic!
    if (zoneBypassed[zoneIdx]) {
        Serial.printf("ℹ️ [SISTEMA] Zona %d está anulada (bypassed). Ignorando lógica de alarma.\n", zoneId);
        return;
    }

    // Chime feature when disarmed and zone opens (disabled)
    /*
    if (isOpen && currentMode == DISARMED && chimeEnabled) {
        dsc.beep(3);
    }
    */

    bool shouldTrigger = false;
    bool shouldStartEntryDelay = false;
    
    if (isOpen) {
        if (currentMode == ARMED_AWAY) {
            if (zoneId == 1) {
                shouldStartEntryDelay = true;
            } else {
                shouldTrigger = true;
            }
        } else if (currentMode == ARMED_HOME) {
            uint8_t mask = configManager->getConfig<espConfig::misc_config_t>().armedHomeZones;
            if ((mask >> zoneIdx) & 0x01) {
                if (zoneId == 1) {
                    shouldStartEntryDelay = true;
                } else {
                    shouldTrigger = true;
                }
            }
        } else if (currentMode == ENTRY_DELAY) {
            if (zoneId != 1 && zoneId != 2) { // Ignore Zone 2 (motion sensor) during entry delay
                if (armedModeBeforeDelay == ARMED_AWAY) {
                    shouldTrigger = true;
                } else if (armedModeBeforeDelay == ARMED_HOME) {
                    uint8_t mask = configManager->getConfig<espConfig::misc_config_t>().armedHomeZones;
                    if ((mask >> zoneIdx) & 0x01) {
                        shouldTrigger = true;
                    }
                }
            }
        }
    }

    if (shouldStartEntryDelay) {
        g_lastTriggerZone = (uint8_t)zoneId; // so a subsequent entry-timeout TRIGGERED logs this zone
        armedModeBeforeDelay = currentMode;
        update_current_mode(ENTRY_DELAY);
        entryDelayStartTime = millis();
        lastEntrySecondsLeft = -1;
        mqtt_publish_state("pending");
        Serial.printf("\n⏳ [SISTEMA] Puerta principal abierta (%s). INICIANDO RETARDO DE ENTRADA (15s)...\n", sourceName);
    }
    if (shouldTrigger) {
        g_lastTriggerZone = (uint8_t)zoneId; // record which zone tripped for the event log
        update_current_mode(TRIGGERED);
        zoneAlarmMemory[zoneIdx] = true;
        hasAlarmMemory = true;
        mqtt_publish_state("triggered");
        Serial.printf("\n🔴🔴🔴 [ALERTA] INTRUSION DETECTADA EN ZONA %d (%s) 🔴🔴🔴\n", zoneId, sourceName);
        dsc.buzzer(255); // Sound physical buzzer continuously for up to 255s
    }
    
    print_status();
}

extern "C" const char* user_alarm_get_state_string() {
    if(currentMode == DISARMED) return "disarmed";
    if(currentMode == ARMING_AWAY) return "arming_away";
    if(currentMode == ARMING_HOME) return "arming_home";
    if(currentMode == ARMED_AWAY) return "armed_away";
    if(currentMode == ARMED_HOME) return "armed_home";
    if(currentMode == ENTRY_DELAY) return "pending";
    if(currentMode == TRIGGERED) return "triggered";
    return "disarmed";
}

extern "C" bool user_alarm_get_sensor_state(int id) {
    if (id >= 1 && id <= 8) {
        return sensors[id - 1];
    }
    return false;
}

extern "C" void user_alarm_set_sensor_state(int id, bool isOpen) {
    if (id >= 1 && id <= 8) {
        trigger_zone_change(id - 1, isOpen, "MQTT-RF");
    }
}

void print_status() {
    Serial.printf("\n==================================================\n");
    Serial.print("MODO ALARMA: ");
    if(currentMode == DISARMED) Serial.println("⚪ DESARMADO");
    else if(currentMode == ARMING_AWAY) Serial.println("⏳ PENDIENTE DE SALIDA (AWAY)");
    else if(currentMode == ARMING_HOME) Serial.println("⏳ PENDIENTE DE SALIDA (HOME)");
    else if(currentMode == ARMED_AWAY) Serial.println("🟠 ARMADO (AWAY)");
    else if(currentMode == ARMED_HOME) Serial.println("🏠 ARMADO (HOME)");
    else if(currentMode == ENTRY_DELAY) Serial.println("⏳ RETARDO DE ENTRADA (PENDING)");
    else Serial.println("🔴 !!! DISPARADO !!!");
    
    Serial.print("SENSORES: [");
    for(int i=0; i<8; i++) Serial.print(sensors[i] ? "! " : ". ");
    Serial.println("]");
    Serial.println("==================================================\n");
}

extern "C" void user_alarm_setup() { 
    auto& miscConfig = configManager->getConfig<espConfig::misc_config_t>();
    physicalZones[0].pin = miscConfig.zonePin1;
    physicalZones[1].pin = miscConfig.zonePin2;
    physicalZones[2].pin = miscConfig.zonePin3;
    physicalZones[3].pin = miscConfig.zonePin4;
    physicalZones[4].pin = miscConfig.zonePin5;
    physicalZones[5].pin = miscConfig.zonePin6;
    physicalZones[6].pin = miscConfig.zonePin7;
    physicalZones[7].pin = miscConfig.zonePin8;

    for (int i = 0; i < 8; i++) {
        auto& zone = physicalZones[i];
        if (zone.pin != 255 && !user_alarm_is_zone_disabled(i)) {
            pinMode(zone.pin, INPUT_PULLUP);
            zone.lastPinReading = digitalRead(zone.pin);
            sensors[zone.sensorIndex] = (zone.lastPinReading == HIGH); // HIGH = OPEN
        } else {
            sensors[zone.sensorIndex] = false;
        }
    }
    // Initialize Siren Pin if configured
    if (miscConfig.sirenPin != 255) {
        pinMode(miscConfig.sirenPin, OUTPUT);
        digitalWrite(miscConfig.sirenPin, miscConfig.sirenActiveHigh ? LOW : HIGH);
        Serial.printf("📢 [ALARM] Configured Siren GPIO Pin %d (Active %s)\n", miscConfig.sirenPin, miscConfig.sirenActiveHigh ? "HIGH" : "LOW");
    }

    currentMode = restore_alarm_state();
    if (currentMode == ARMING_AWAY) currentMode = ARMED_AWAY;
    if (currentMode == ARMING_HOME) currentMode = ARMED_HOME;
    if (currentMode == ENTRY_DELAY) currentMode = ARMED_AWAY; // safety fallback

    // Start the virtual panel
    uint8_t clockPin = miscConfig.dscClockPin;
    uint8_t readPin = miscConfig.dscReadPin;
    uint8_t writePin = miscConfig.dscWritePin;
    if (clockPin == 0 || clockPin == 255 || 
        readPin == 0 || readPin == 255 || 
        writePin == 0 || writePin == 255 ||
        clockPin == readPin || clockPin == writePin || readPin == writePin) {
        Serial.println("⚠️ [ALARM] Invalid keypad pins in config. Falling back to defaults: Clock=21, Read=18, Write=19");
        clockPin = 21;
        readPin = 18;
        writePin = 19;
    }
    Serial.printf("⌨️ [ALARM] Keypad active pins: Clock=%d, Read=%d, Write=%d\n", clockPin, readPin, writePin);
    Serial.printf("⚡ [ALARM] Physical Zone pins: %d, %d, %d, %d, %d, %d, %d, %d\n", 
                  physicalZones[0].pin, physicalZones[1].pin, physicalZones[2].pin, physicalZones[3].pin,
                  physicalZones[4].pin, physicalZones[5].pin, physicalZones[6].pin, physicalZones[7].pin);
    dsc.setPins(clockPin, readPin, writePin);
    dsc.begin();
    dsc.key = 0xFF; // Reset to custom idle state 
    
    // Set initial board state based on restored currentMode
    if (currentMode == ARMED_AWAY || currentMode == ARMED_HOME) {
        dsc.lightReady = off;
        dsc.lightArmed = on;
    } else {
        dsc.lightReady = on;
        dsc.lightArmed = off;
    }
    dsc.lightTrouble = off;
    dsc.lightBacklight = on;
    lastKeypadActivityTime = millis();
    lastSystemActivityTime = millis();

    mqtt_publish_state(user_alarm_get_state_string());
    m_remote_event = AppEventLoop::subscribe(ALARM_EVENT, ALARM_SET_REMOTE, [](const uint8_t* data, size_t size){
        if(size == 0) return;
        std::string cmd(reinterpret_cast<const char*>(data), size);
        g_pendingSource = eventlog::Source::REMOTE; // web UI / HomeKit / MQTT
        if (cmd == "ARMED_AWAY") user_alarm_arm_away();
        else if (cmd == "ARMED_HOME") user_alarm_arm_home();
        else if (cmd == "DISARMED") user_alarm_disarm();
    });
}

extern "C" void user_alarm_arm_home() {
    lastSystemActivityTime = millis();
    if (currentMode != ARMED_HOME && currentMode != ARMING_HOME) {
        // Ready Check (checking all 8 zones, skipping bypassed ones and motion sensor Zone 2)
        bool anyZoneOpen = false;
        for (int i = 0; i < 8; i++) {
            if (i == 1) continue; // Ignore Zone 2 (motion sensor) for ready check
            if (sensors[i] && !zoneBypassed[i]) anyZoneOpen = true;
        }
        if (anyZoneOpen) {
            Serial.println("\n⚠️ [SISTEMA] No se puede armar Home: Zonas abiertas.");
            dsc.beep(4); // Play error chirp
            return;
        }

        // Clear alarm memory on arming
        memset(zoneAlarmMemory, 0, sizeof(zoneAlarmMemory));
        hasAlarmMemory = false;

        update_current_mode(ARMING_HOME);
        armingStartTime = millis();
        lastSecondsLeft = -1;
        
        dsc.beep(1); // First immediate arming confirmation beep
        mqtt_publish_state("arming");
        
        Serial.println("\n🏠 [SISTEMA] Iniciando ARMADO HOME...");
    }
}

extern "C" void user_alarm_arm_away() {
    lastSystemActivityTime = millis();
    if (currentMode != ARMED_AWAY && currentMode != ARMING_AWAY) {
        // Ready Check (checking all 8 zones, skipping bypassed ones and motion sensor Zone 2)
        bool anyZoneOpen = false;
        for (int i = 0; i < 8; i++) {
            if (i == 1) continue; // Ignore Zone 2 (motion sensor) for ready check
            if (sensors[i] && !zoneBypassed[i]) anyZoneOpen = true;
        }
        if (anyZoneOpen) {
            Serial.println("\n⚠️ [SISTEMA] No se puede armar Away: Zonas abiertas.");
            dsc.beep(4); // Play error chirp
            return;
        }

        // Clear alarm memory on arming
        memset(zoneAlarmMemory, 0, sizeof(zoneAlarmMemory));
        hasAlarmMemory = false;

        update_current_mode(ARMING_AWAY);
        armingStartTime = millis();
        lastSecondsLeft = -1;
        
        dsc.beep(1); // First immediate arming confirmation beep
        mqtt_publish_state("arming");
        
        Serial.println("\n🟠 [SISTEMA] Iniciando ARMADO AWAY...");
    }
}

extern "C" void user_alarm_disarm() { 
    lastSystemActivityTime = millis();
    if (currentMode != DISARMED) {
        update_current_mode(DISARMED);
 
        // Clear active beeps and buzzer
        dsc.beep(0);
        dsc.buzzer(0);
        // Play double confirmation beep
        dsc.beep(2);
 
        // Clear bypasses on disarm
        for (int i = 0; i < 8; i++) {
            user_alarm_set_zone_bypass(i, false);
        }
        // Reset sub-modes to ensure we aren't stuck in menus
        isBypassMode = false;
        isMemoryMode = false;
        isTroubleMode = false;
        isSimulateMode = false;

        mqtt_publish_state("disarmed");
        Serial.println("\n🟢 [SISTEMA] ALARMA DESARMADA");
        print_status();
    }
}

// Web UI siren-disable toggle. This ONLY ever writes miscConfig.sirenDisabled
// and persists it — it never touches currentMode, never calls arm/disarm, so
// it structurally cannot arm (or disarm) the system. The alarm state machine
// (arming, zone monitoring, TRIGGERED state, MQTT/HomeKit reporting) keeps
// running exactly as normal; only the audible siren output is gated (see the
// sirenActive computation in user_alarm_loop()). No auto-expiry: it stays in
// whatever state the user last set until they toggle it again.
extern "C" void user_alarm_set_siren_disabled(bool disabled) {
    configManager->updateFromJson<espConfig::misc_config_t>(
        std::string("{\"sirenDisabled\":") + (disabled ? "true" : "false") + "}");
    configManager->saveConfig<espConfig::misc_config_t>();
    eventlog::add(disabled ? eventlog::EventType::SIREN_DISABLED : eventlog::EventType::SIREN_ENABLED,
                  static_cast<uint8_t>(eventlog::Source::REMOTE));
    Serial.printf("\n%s [WEB UI] Sirena %s (el sistema de alarma sigue funcionando con normalidad).\n",
                  disabled ? "🔇" : "🔊", disabled ? "DESHABILITADA" : "HABILITADA");
}

extern "C" bool user_alarm_is_siren_disabled() {
    auto& miscConfig = configManager->getConfig<espConfig::misc_config_t>();
    return miscConfig.sirenDisabled;
}

// Milliseconds MQTT has been continuously disconnected (0 if connected or never yet
// connected). Surfaced in the Web UI health panel. Uses the same lastMqttConnectedTime
// the connectivity nudge maintains in user_alarm_loop().
extern "C" unsigned long user_alarm_mqtt_down_ms() {
    if (mqttManager && mqttManager->isConnected()) return 0;
    if (lastMqttConnectedTime == 0) return 0;
    return millis() - lastMqttConnectedTime;
}

extern "C" void user_alarm_loop() {
    // 1. Maintain Keybus clock (MUST RUN CONSTANTLY)
    dsc.loop();

    // 2. Handle Keypad input
    if (dsc.key != 0xFF) {
        lastKeypadActivityTime = millis();
        dsc.lightBacklight = on;
        // Any arm/disarm triggered while processing a physical keypad key is
        // attributed to the keypad in the event log (consumed on the transition).
        g_pendingSource = eventlog::Source::KEYPAD;
        byte rawKey = dsc.key;
        dsc.key = 0xFF; // Clear buffer
        char key = decodeDscKey(rawKey);
        
        if (key != '?') {
            // Anti-ghosting filter for unpowered keypad:
            // Unpowered keypad reads stuck low read pin, causing continuous rapid '0' keypresses.
            static unsigned long lastKeyTime = 0;
            static char lastKey = '\0';
            static int rapidZeroCount = 0;
            unsigned long now = millis();

            // Clear PIN buffer on idle timeout (5 seconds of keypad inactivity)
            if (keypadPinBuffer.length() > 0 && (now - lastKeyTime > 5000)) {
                keypadPinBuffer = "";
                Serial.println("⌨️ [TECLADO DSC] PIN buffer borrado por inactividad.");
            }
            
            if (key == '0') {
                if (lastKey == '0' && (now - lastKeyTime < 350)) {
                    rapidZeroCount++;
                    lastKeyTime = now;
                    if (rapidZeroCount >= 3) {
                        keypadPinBuffer = ""; // Clear any partial PIN accumulated from ghosting
                    }
                    return; // Ignore ghost keypress
                } else {
                    rapidZeroCount = 0;
                }
            }
            lastKeyTime = now;
            lastKey = key;

            Serial.printf("⌨️ [TECLADO DSC] Tecla presionada: [ %c ]\n", key);
            // (Keypad hardware automatically generates keypress audio feedback)

            if (isWaitingForInstallerCode) {
                if (key >= '0' && key <= '9') {
                    keypadPinBuffer += key;
                    Serial.printf("⌨️ [TECLADO DSC] Código Instalador: %s\n", keypadPinBuffer.c_str());
                    if (keypadPinBuffer.length() == 4) {
                        if (keypadPinBuffer == "5555") {
                            isInstallerMode = true;
                            Serial.println("🔑 [TECLADO DSC] Código correcto. Menú Instalador (*8): 9=Reiniciar, 2=Medidor RSSI, #=Salir.");
                            dsc.beep(3);
                        } else {
                            Serial.println("❌ [TECLADO DSC] Código incorrecto!");
                            dsc.beep(4);
                        }
                        keypadPinBuffer = "";
                        isWaitingForInstallerCode = false;
                    }
                } else if (key == '#' || key == '*') {
                    isWaitingForInstallerCode = false;
                    keypadPinBuffer = "";
                    Serial.println("⌨️ [TECLADO DSC] Cancelado.");
                    dsc.beep(1);
                }
                return;
            }
            if (isInstallerMode) {
                if (key == '9') {
                    Serial.println("🔄 [TECLADO DSC] Reiniciando ESP32...");
                    dsc.beep(2);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                } else if (key == '2') {
                    isRssiMeterMode = !isRssiMeterMode;
                    Serial.printf("⌨️ [TECLADO DSC] Medidor RSSI %s\n", isRssiMeterMode ? "ACTIVADO" : "DESACTIVADO");
                    dsc.beep(isRssiMeterMode ? 3 : 1);
                } else if (key == '#' || key == '*') {
                    isInstallerMode = false;
                    isRssiMeterMode = false;
                    Serial.println("⌨️ [TECLADO DSC] Saliendo del menú de instalador.");
                    dsc.beep(1);
                } else {
                    dsc.beep(4);
                }
                return;
            }

            if (isBypassMode) {
                if (key >= '1' && key <= '8') {
                    int idx = key - '1';
                    user_alarm_set_zone_bypass(idx, !zoneBypassed[idx]);
                } else if (key == '#' || key == '*') {
                    isBypassMode = false;
                    Serial.println("⌨️ [TECLADO DSC] Saliendo de modo anulación.");
                } else {
                    dsc.beep(4);
                }
            }
            else if (isMemoryMode) {
                if (key == '#' || key == '*') {
                    isMemoryMode = false;
                    Serial.println("⌨️ [TECLADO DSC] Saliendo de modo memoria.");
                } else {
                    dsc.beep(4);
                }
            }
            else if (isTroubleMode) {
                if (key == '#' || key == '*') {
                    isTroubleMode = false;
                    Serial.println("⌨️ [TECLADO DSC] Saliendo de modo fallo.");
                } else {
                    dsc.beep(4);
                }
            }
            else if (isSimulateMode) {
                if (key >= '1' && key <= '8') {
                    int idx = key - '1';
                    trigger_zone_change(idx, !sensors[idx], "TECLADO DSC (SIM)");
                } else if (key == '#' || key == '*') {
                    isSimulateMode = false;
                    Serial.println("⌨️ [TECLADO DSC] Saliendo de modo simulación.");
                } else {
                    dsc.beep(4);
                }
            }
            else if (isMacroMode) {
                if (key >= '1' && key <= '9') {
                    std::string macroPayload = "7" + std::string(1, key);
                    if (mqttManager) {
                        mqttManager->publish("home/alarm/keypad/macro", macroPayload, 0, false);
                    }
                    Serial.printf("🚀 [TECLADO DSC] Ejecutando Macro: %s\n", macroPayload.c_str());
                    dsc.beep(2); // 2 confirmation chirps
                    isMacroMode = false;
                } else if (key == '#' || key == '*') {
                    isMacroMode = false;
                    Serial.println("⌨️ [TECLADO DSC] Saliendo de modo macro.");
                } else {
                    dsc.beep(4);
                }
            }
            else if (isCommandMode) {
                if (key == '1') {
                    isBypassMode = true;
                    Serial.println("⌨️ [TECLADO DSC] Modo Anulación Activo (*1). Presione 1-8 para anular/activar, # para salir.");
                } else if (key == '2') {
                    isTroubleMode = true;
                    Serial.println("⌨️ [TECLADO DSC] Modo Fallos Activo (*2). Presione # para salir.");
                } else if (key == '3') {
                    isMemoryMode = true;
                    Serial.println("⌨️ [TECLADO DSC] Modo Memoria Activo (*3). Presione # para salir.");
                } else if (key == '4') {
                    chimeEnabled = !chimeEnabled;
                    Serial.printf("⌨️ [TECLADO DSC] Chime (Campanilla) %s\n", chimeEnabled ? "ACTIVADO" : "DESACTIVADO");
                    if (chimeEnabled) {
                        dsc.beep(3); // 3 rapid chirps
                    } else {
                        dsc.beep(1); // 1 beep (since keypress already beeped, this gives total 2 or a second beep)
                    }
                } else if (key == '6') {
                    isSimulateMode = true;
                    Serial.println("⌨️ [TECLADO DSC] Modo Simulación Activo (*6). Presione 1-8 para alternar sensor, # para salir.");
                } else if (key == '7') {
                    isMacroMode = true;
                    Serial.println("⌨️ [TECLADO DSC] Modo Macro Activo (*7). Presione 1-9 para ejecutar macro, # para salir.");
                } else if (key == '0') {
                    Serial.println("⌨️ [TECLADO DSC] Armado rápido (*0)...");
                    if (currentMode == DISARMED) {
                        user_alarm_arm_away();
                    } else {
                        dsc.beep(4);
                    }
                } else if (key == '9') {
                    Serial.println("⌨️ [TECLADO DSC] Armado rápido en Modo Home (*9)...");
                    if (currentMode == DISARMED) {
                        user_alarm_arm_home();
                    } else {
                        dsc.beep(4);
                    }
                } else if (key == '8') {
                    isWaitingForInstallerCode = true;
                    keypadPinBuffer = "";
                    Serial.println("⌨️ [TECLADO DSC] Ingrese Código de Instalador de 4 dígitos...");
                } else if (key == '#' || key == '*') {
                    Serial.println("⌨️ [TECLADO DSC] Cancelado.");
                } else {
                    Serial.println("⚠️ [TECLADO DSC] Código de comando desconocido.");
                    dsc.beep(4);
                }
                isCommandMode = false;
            } 
            else {
                // Normal Mode (PIN Code entry & Quick Arming)
                if (key == '0' && keypadPinBuffer.length() == 0) {
                    bool newSirenState = !user_alarm_is_siren_testing();
                    user_alarm_siren_test(newSirenState);
                    Serial.printf("📢 [TECLADO DSC] Tecla '0' -> Sirena %s\n", newSirenState ? "ACTIVADA" : "DESACTIVADA");
                    if (newSirenState) {
                        dsc.beep(3);
                    } else {
                        dsc.beep(1);
                    }
                }
                else if (key >= '0' && key <= '9') {
                    keypadPinBuffer += key;
                    Serial.printf("⌨️ [TECLADO DSC] PIN Buffer: %s\n", keypadPinBuffer.c_str());
                    
                    if (keypadPinBuffer.length() == 4) {
                        if (keypadPinBuffer == configManager->getConfig<espConfig::misc_config_t>().alarmCode) {
                            Serial.println("🔑 [TECLADO DSC] PIN correcto ingresado.");
                            if (currentMode == DISARMED) {
                                user_alarm_arm_away();
                            } else {
                                user_alarm_disarm();
                            }
                        } else {
                            Serial.println("❌ [TECLADO DSC] PIN incorrecto!");
                            dsc.beep(4); // Play error chirp
                        }
                        keypadPinBuffer = ""; // Reset buffer
                    }
                }
                else if (key == 's') {
                    Serial.println("⌨️ [TECLADO DSC] Tecla Stay presionada. Armado Home...");
                    if (currentMode == DISARMED) {
                        user_alarm_arm_home();
                    } else {
                        dsc.beep(4);
                    }
                }
                else if (key == 'a') {
                    Serial.println("⌨️ [TECLADO DSC] Tecla Away presionada. Armado Away...");
                    if (currentMode == DISARMED) {
                        user_alarm_arm_away();
                    } else {
                        dsc.beep(4);
                    }
                }
                else if (key == '#') {
                    if (keypadPinBuffer.length() > 0) {
                        keypadPinBuffer = "";
                        Serial.println("⌨️ [TECLADO DSC] PIN buffer borrado.");
                    } else {
                        // Quick Arm Away (only if ready)
                        if (currentMode == DISARMED) {
                            user_alarm_arm_away();
                        }
                    }
                }
                else if (key == '*') {
                    // Start Command Mode
                    isCommandMode = true;
                    keypadPinBuffer = "";
                    Serial.println("⌨️ [TECLADO DSC] Modo Comando Activo (*). Presione 1-4, 7 o 0.");
                }
            }
        }
    }

    // 3. Monitor physical zones via GPIO
    if (millis() - lastSamplingTime >= SAMPLING_INTERVAL) {
        lastSamplingTime = millis();
        for (auto& zone : physicalZones) {
            if (zone.pin == 255 || user_alarm_is_zone_disabled(zone.sensorIndex)) continue;
            int reading = digitalRead(zone.pin);
            if (reading != zone.lastPinReading) {
                zone.lastDebounceTime = millis();
                zone.lastPinReading = reading;
            }
            if ((millis() - zone.lastDebounceTime) > DEBOUNCE_DELAY) {
                bool isOpen = (reading == HIGH);
                if (isOpen != sensors[zone.sensorIndex]) {
                    trigger_zone_change(zone.sensorIndex, isOpen, "HARDWARE");
                }
            }
        }
    }
 
    // 4. Handle Exit Delay Timer & Keypad Beeping
    if (currentMode == ARMING_AWAY || currentMode == ARMING_HOME) {
        unsigned long elapsed = millis() - armingStartTime;
        if (elapsed >= EXIT_DELAY_MS) {
            if (currentMode == ARMING_AWAY) {
                update_current_mode(ARMED_AWAY);
                mqtt_publish_state("armed_away");
                Serial.println("\n🟠 [SISTEMA] !!! ALARMA ARMADA (AWAY) !!!");
            } else {
                update_current_mode(ARMED_HOME);
                mqtt_publish_state("armed_home");
                Serial.println("\n🏠 [SISTEMA] !!! ALARMA ARMADA (HOME) !!!");
            }
            dsc.beep(3); // 3 rapid confirmation beeps on armed state
            print_status();
        } else {
            int secondsLeft = (EXIT_DELAY_MS - elapsed) / 1000;
            if (secondsLeft != lastSecondsLeft) {
                Serial.printf("⏳ ARMANDO EN %d SEG...\n", secondsLeft + 1);
                lastSecondsLeft = secondsLeft;
            }
            
            // Beeps during Exit Delay
            if (elapsed >= 10000) { // Last 5 seconds: fast beeps
                if (millis() - lastKeypadBeepTime >= 333) {
                    dsc.beep(1);
                    lastKeypadBeepTime = millis();
                }
            } else { // First 10 seconds: slow beeps
                if (millis() - lastKeypadBeepTime >= 1000) {
                    dsc.beep(1);
                    lastKeypadBeepTime = millis();
                }
            }
        }
    }

    // 5. Handle Entry Delay Timer & Keypad Beeping
    if (currentMode == ENTRY_DELAY) {
        unsigned long elapsed = millis() - entryDelayStartTime;
        if (elapsed >= ENTRY_DELAY_MS) {
            update_current_mode(TRIGGERED);
            mqtt_publish_state("triggered");
            Serial.println("\n🔴🔴🔴 [ALERTA] TIEMPO DE ENTRADA AGOTADO - INTRUSION DETECTADA 🔴🔴🔴");
            dsc.buzzer(255); // Sound physical buzzer continuously for up to 255s
            print_status();
        } else {
            int secondsLeft = (ENTRY_DELAY_MS - elapsed) / 1000;
            if (secondsLeft != lastEntrySecondsLeft) {
                Serial.printf("⚠️ [ALERTA] ALARMA SE DISPARARÁ EN %d SEG... ¡Haga tap con HomeKey! 🔊 ¡BEEP!\n", secondsLeft + 1);
                lastEntrySecondsLeft = secondsLeft;
            }

            // Beeps during Entry Delay
            if (elapsed >= 10000) { // Last 5 seconds: rapid warning beeps
                if (millis() - lastKeypadBeepTime >= 250) {
                    dsc.beep(1);
                    lastKeypadBeepTime = millis();
                }
            } else { // First 10 seconds: slow beeps
                if (millis() - lastKeypadBeepTime >= 1000) {
                    dsc.beep(1);
                    lastKeypadBeepTime = millis();
                }
            }
        }
    }

    // 6. Handle Serial Console Inputs
    if (Serial.available() > 0) {
        char c = toupper(Serial.read());
        if (c == 'A') user_alarm_arm_away();
        else if (c == 'D') user_alarm_disarm();
        else if (c >= '1' && c <= '8') {
            int idx = c - '1';
            trigger_zone_change(idx, !sensors[idx], "SIMULADO");
        }
    }

    // 7. Handle Triggered Siren / Keypad Buzzer Wailing & Physical Siren GPIO Pin
    static bool wasSirenActive = false;
    static unsigned long last_beep = 0;
    bool sirenActive = (currentMode == TRIGGERED) || isSirenTestActive;
    if (sirenActive) {
        if (!wasSirenActive || last_beep == 0 || millis() - last_beep > 60000) {
            Serial.println("📢 !!! SIRENA ACTIVA !!!");
            dsc.buzzer(255); // Keep wailing (renew keepalive every 60s)
            last_beep = millis();
        }
        wasSirenActive = true;
    } else {
        if (wasSirenActive) {
            dsc.buzzer(0);
            dsc.beep(0);
            wasSirenActive = false;
            last_beep = 0;
        }
    }

    auto& miscConfig = configManager->getConfig<espConfig::misc_config_t>();
    if (miscConfig.sirenPin != 255) {
        // Web UI siren-disable toggle gates ONLY this physical relay output.
        // currentMode/TRIGGERED/arming logic above is untouched and keeps running normally.
        bool sirenOutputActive = sirenActive && !miscConfig.sirenDisabled;
        digitalWrite(miscConfig.sirenPin, sirenOutputActive ? (miscConfig.sirenActiveHigh ? HIGH : LOW) : (miscConfig.sirenActiveHigh ? LOW : HIGH));
    }

    // 8. Synchronize physical keypad LEDs with system state in real-time
    if (isRssiMeterMode) {
        // Map RSSI level to 1-8 LEDs
        int leds = 0;
        if (WiFi.isConnected()) {
            int32_t rssi = WiFi.RSSI();
            if (rssi < -85) leds = 1;
            else if (rssi < -80) leds = 2;
            else if (rssi < -75) leds = 3;
            else if (rssi < -70) leds = 4;
            else if (rssi < -65) leds = 5;
            else if (rssi < -60) leds = 6;
            else if (rssi < -55) leds = 7;
            else leds = 8;
        }
        dsc.lightZone1 = (leds >= 1) ? on : off;
        dsc.lightZone2 = (leds >= 2) ? on : off;
        dsc.lightZone3 = (leds >= 3) ? on : off;
        dsc.lightZone4 = (leds >= 4) ? on : off;
        dsc.lightZone5 = (leds >= 5) ? on : off;
        dsc.lightZone6 = (leds >= 6) ? on : off;
        dsc.lightZone7 = (leds >= 7) ? on : off;
        dsc.lightZone8 = (leds >= 8) ? on : off;
    } else if (isBypassMode) {
        // In bypass mode, zone LEDs show which zones are currently bypassed
        dsc.lightZone1 = zoneBypassed[0] ? on : off;
        dsc.lightZone2 = zoneBypassed[1] ? on : off;
        dsc.lightZone3 = zoneBypassed[2] ? on : off;
        dsc.lightZone4 = zoneBypassed[3] ? on : off;
        dsc.lightZone5 = zoneBypassed[4] ? on : off;
        dsc.lightZone6 = zoneBypassed[5] ? on : off;
        dsc.lightZone7 = zoneBypassed[6] ? on : off;
        dsc.lightZone8 = zoneBypassed[7] ? on : off;
    } else if (isMemoryMode) {
        // In memory mode, zone LEDs show which zones triggered the alarm
        dsc.lightZone1 = zoneAlarmMemory[0] ? on : off;
        dsc.lightZone2 = zoneAlarmMemory[1] ? on : off;
        dsc.lightZone3 = zoneAlarmMemory[2] ? on : off;
        dsc.lightZone4 = zoneAlarmMemory[3] ? on : off;
        dsc.lightZone5 = zoneAlarmMemory[4] ? on : off;
        dsc.lightZone6 = zoneAlarmMemory[5] ? on : off;
        dsc.lightZone7 = zoneAlarmMemory[6] ? on : off;
        dsc.lightZone8 = zoneAlarmMemory[7] ? on : off;
    } else if (isTroubleMode) {
        // In trouble mode: Zone 1 = WiFi, Zone 2 = NFC, Zone 3 = MQTT, Zone 4 = HomeKit
        dsc.lightZone1 = (!WiFi.isConnected()) ? on : off;
        dsc.lightZone2 = (nfcManager == nullptr) ? on : off;
        dsc.lightZone3 = (!mqttManager || !mqttManager->isConnected()) ? on : off;
        dsc.lightZone4 = (HAPClient::nAdminControllers() == 0) ? on : off;
        dsc.lightZone5 = off;
        dsc.lightZone6 = off;
        dsc.lightZone7 = off;
        dsc.lightZone8 = off;
    } else if (isSimulateMode || currentMode == DISARMED || currentMode == ARMING_AWAY || currentMode == ARMING_HOME) {
        // Otherwise, show active zones when disarmed/arming
        dsc.lightZone1 = sensors[0] ? on : off;
        dsc.lightZone2 = sensors[1] ? on : off;
        dsc.lightZone3 = sensors[2] ? on : off;
        dsc.lightZone4 = sensors[3] ? on : off;
        dsc.lightZone5 = sensors[4] ? on : off;
        dsc.lightZone6 = sensors[5] ? on : off;
        dsc.lightZone7 = sensors[6] ? on : off;
        dsc.lightZone8 = sensors[7] ? on : off;
    } else {
        // System is armed (Armed, Entry Delay, Triggered): Zone LEDs must be OFF
        dsc.lightZone1 = off;
        dsc.lightZone2 = off;
        dsc.lightZone3 = off;
        dsc.lightZone4 = off;
        dsc.lightZone5 = off;
        dsc.lightZone6 = off;
        dsc.lightZone7 = off;
        dsc.lightZone8 = off;
    }

    // Ready LED Check (checking all 8 zones, skipping bypassed ones and motion sensor Zone 2)
    bool anyReadyZoneOpen = false;
    for (int i = 0; i < 8; i++) {
        if (i == 1) continue; // Ignore Zone 2 (motion sensor) for ready LED check
        if (sensors[i] && !zoneBypassed[i]) anyReadyZoneOpen = true;
    }
    dsc.lightReady = (currentMode == DISARMED && !anyReadyZoneOpen) ? on : off;

    // Armed LED state
    if (currentMode == ARMED_AWAY || currentMode == ARMED_HOME) {
        dsc.lightArmed = on;
    } else if (currentMode == ARMING_AWAY || currentMode == ARMING_HOME) {
        dsc.lightArmed = blink;
    } else {
        dsc.lightArmed = off;
    }

    // Bypass LED state
    bool anyBypassed = false;
    for (int i = 0; i < 8; i++) {
        if (zoneBypassed[i]) anyBypassed = true;
    }
    if (isBypassMode) {
        dsc.lightBypass = blink;
    } else {
        dsc.lightBypass = anyBypassed ? on : off;
    }

    // Memory LED state
    if (isMemoryMode) {
        dsc.lightMemory = blink;
    } else {
        dsc.lightMemory = hasAlarmMemory ? on : off;
    }

    // Trouble LED state: active if WiFi or MQTT is offline
    bool hasTrouble = (!WiFi.isConnected() || (!mqttManager || !mqttManager->isConnected()));
    if (isTroubleMode) {
        dsc.lightTrouble = blink;
    } else {
        dsc.lightTrouble = hasTrouble ? on : off;
    }

    // Log MQTT connectivity edges (up/down) to the persistent event log so an
    // overnight broker/network outage is visible after the fact.
    {
        static int8_t mqttWas = -1; // -1 unknown, 0 down, 1 up
        int8_t mqttNow = (mqttManager && mqttManager->isConnected()) ? 1 : 0;
        if (mqttWas != -1 && mqttNow != mqttWas) {
            eventlog::add(mqttNow ? eventlog::EventType::MQTT_UP : eventlog::EventType::MQTT_LOST);
        }
        mqttWas = mqttNow;
    }

    // MQTT connectivity nudge. The ESP-IDF MQTT client already auto-reconnects on its
    // own, so ordinary broker/network blips recover without our help — we must NOT
    // reboot for those (doing so just loops forever while the broker is down). The only
    // case that needs a push is a "zombie" Wi-Fi link (radio associated, data path dead)
    // that fires no disconnect event. So after a long *continuous* MQTT outage with
    // Wi-Fi still claiming to be connected, force ONE non-destructive Wi-Fi reconnect
    // and back off. This never calls esp_restart().
    if (mqttManager && mqttManager->isConnected()) {
        lastMqttConnectedTime = millis();
    } else if (lastMqttConnectedTime == 0) {
        lastMqttConnectedTime = millis(); // Don't count time before the first connection.
    } else if (millis() - lastMqttConnectedTime > MQTT_DOWN_NUDGE_MS
               && WiFi.isConnected()
               && millis() - lastWifiNudgeTime > WIFI_NUDGE_COOLDOWN_MS) {
        lastWifiNudgeTime = millis();
        Serial.println("📶 [MQTT] Sin MQTT >10min con WiFi asociado. Forzando reconexión WiFi (SIN reiniciar).");
        WiFi.reconnect();
    }

    // Auto-Backlight Dimming (30-second timeout, only when DISARMED or ARMED)
    if (currentMode == DISARMED || currentMode == ARMED_AWAY || currentMode == ARMED_HOME) {
        if (millis() - lastKeypadActivityTime > 30000) {
            dsc.lightBacklight = off;
        }
    } else {
        // Keep backlight active during exit delay, entry delay, and triggered states
        dsc.lightBacklight = on;
    }

    // Auto-Protect (No-Motion Auto-Arming)
    if (currentMode == DISARMED && miscConfig.autoArmEnabled) {
        unsigned long elapsed = millis() - lastSystemActivityTime;
        unsigned long timeoutMs = (unsigned long)miscConfig.autoArmTimeoutMins * 60000;
        if (elapsed >= timeoutMs) {
            // Ready Check (checking all 8 zones, skipping bypassed ones and motion sensor Zone 2)
            bool anyZoneOpen = false;
            for (int i = 0; i < 8; i++) {
                if (i == 1) continue; // Ignore Zone 2 (motion sensor) for ready check
                if (sensors[i] && !zoneBypassed[i]) anyZoneOpen = true;
            }

            lastSystemActivityTime = millis(); // Reset to wait next timeout if arming fails/succeeds

            if (anyZoneOpen) {
                Serial.println("\n⚠️ [AUTO-PROTECT] No se puede auto-armar: Zonas abiertas.");
                dsc.beep(4); // Play error chirp
            } else {
                Serial.printf("🕒 [AUTO-PROTECT] Inactividad detectada (%lu mins). Auto-armado instantáneo con chirp...\n", (unsigned long)miscConfig.autoArmTimeoutMins);
                
                // Clear alarm memory on arming
                memset(zoneAlarmMemory, 0, sizeof(zoneAlarmMemory));
                hasAlarmMemory = false;

                g_pendingSource = eventlog::Source::AUTO; // no-motion auto-arm
                if (miscConfig.autoArmMode == 0) {
                    update_current_mode(ARMED_HOME);
                    mqtt_publish_state("armed_home");
                } else {
                    update_current_mode(ARMED_AWAY);
                    mqtt_publish_state("armed_away");
                }
                dsc.beep(1); // Play single confirmation chirp
                print_status();
            }
        }
    }
}

extern "C" void user_alarm_failed_tap() { 
    Serial.println("NFC Fail"); 
    lastSystemActivityTime = millis();
    // Play warning tone pattern on keypad to indicate failed tap
    dsc.beep(4); 
}

extern "C" bool user_alarm_is_zone_bypassed(int zoneIdx) {
    if (zoneIdx >= 0 && zoneIdx < 8) {
        return zoneBypassed[zoneIdx];
    }
    return false;
}

extern "C" void user_alarm_set_zone_bypass(int zoneIdx, bool bypassed) {
    if (zoneIdx >= 0 && zoneIdx < 8) {
        zoneBypassed[zoneIdx] = bypassed;
        Serial.printf("ℹ️ [SISTEMA] Zone %d bypass changed to %s\n", zoneIdx + 1, bypassed ? "BYPASSED" : "ACTIVE");
        
        // Publish state to MQTT
        if (mqttManager) {
            std::string stateTopic = "home/alarm/zone/" + std::to_string(zoneIdx + 1) + "/bypass/state";
            mqttManager->publish(stateTopic, bypassed ? "ON" : "OFF", 0, true);
        }
        
        broadcast_ui_update();
    }
}

extern "C" bool user_alarm_is_zone_disabled(int zoneIdx) {
    if (zoneIdx < 0 || zoneIdx >= 8) return false;
    auto& miscConfig = configManager->getConfig<espConfig::misc_config_t>();
    switch (zoneIdx) {
        case 0: return miscConfig.zoneDisabled1;
        case 1: return miscConfig.zoneDisabled2;
        case 2: return miscConfig.zoneDisabled3;
        case 3: return miscConfig.zoneDisabled4;
        case 4: return miscConfig.zoneDisabled5;
        case 5: return miscConfig.zoneDisabled6;
        case 6: return miscConfig.zoneDisabled7;
        case 7: return miscConfig.zoneDisabled8;
        default: return false;
    }
}

