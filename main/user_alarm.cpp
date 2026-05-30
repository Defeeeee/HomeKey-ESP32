#include "include/user_alarm.h"
#include <Arduino.h>
#include <string>
#include <vector>
#include "include/app_event_loop.hpp"
#include "include/app_events.hpp"
#include "include/ConfigManager.hpp"
#include "include/MqttManager.hpp"
#include "include/WebServerManager.hpp"
#include <dscKeybusInterface.h>

enum AlarmMode { DISARMED, ARMING_AWAY, ARMING_HOME, ARMED_AWAY, ARMED_HOME, ENTRY_DELAY, TRIGGERED };
AlarmMode currentMode = DISARMED;
#define PIN_ZONE_1 13
#define PIN_ZONE_2 17
#define PIN_ZONE_3 14
#define PIN_ZONE_4 25
#define PIN_ZONE_5 26
#define PIN_ZONE_6 27

struct ZoneConfig {
    uint8_t id;         // ID de Zona (1-based: 1 a 6)
    int pin;            // Pin GPIO en el ESP32
    int sensorIndex;    // Índice en el array 'sensors' (0-based)
    bool lastPinReading;
    unsigned long lastDebounceTime;
};

ZoneConfig physicalZones[6] = {
    {1, PIN_ZONE_1, 0, HIGH, 0},
    {2, PIN_ZONE_2, 1, HIGH, 0}, // GPIO 17 para Zona 2
    {3, PIN_ZONE_3, 2, HIGH, 0},
    {4, PIN_ZONE_4, 3, HIGH, 0},
    {5, PIN_ZONE_5, 4, HIGH, 0},
    {6, PIN_ZONE_6, 5, HIGH, 0}
};

const unsigned long DEBOUNCE_DELAY = 50; // ms
unsigned long lastSamplingTime = 0;
const unsigned long SAMPLING_INTERVAL = 200; // Muestreo cada 200ms
bool sensors[8] = {false, false, false, false, false, false, false, false};
AppEventLoop::SubscriptionHandle m_remote_event;

unsigned long armingStartTime = 0;
const unsigned long EXIT_DELAY_MS = 15000; // 15 segundos de cuenta atrás (DSC-aligned)
int lastSecondsLeft = -1;

unsigned long entryDelayStartTime = 0;
const unsigned long ENTRY_DELAY_MS = 15000; // 15 segundos de retardo de entrada
int lastEntrySecondsLeft = -1;
AlarmMode armedModeBeforeDelay = ARMED_AWAY;

// DSC Keybus Interface Global Instance (Clock: 21, Read: 18, Write: 19)
dscKeypadInterface dsc(21, 18, 19);
unsigned long lastKeypadBeepTime = 0;

extern std::unique_ptr<ConfigManager> configManager;
extern std::unique_ptr<MqttManager> mqttManager;
extern std::unique_ptr<WebServerManager> webServerManager;

void print_status();
void broadcast_ui_update() {
    if (webServerManager) {
        webServerManager->broadcastDeviceMetrics();
    }
}

void mqtt_publish_state(const char* state) {
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
        default: return '?'; 
    }
}

// Global helper to process zone transitions and state rules
void trigger_zone_change(int zoneIdx, bool isOpen, const char* sourceName) {
    if (zoneIdx < 0 || zoneIdx >= 8) return;
    
    sensors[zoneIdx] = isOpen;
    int zoneId = zoneIdx + 1;
    if (mqttManager) mqttManager->publishSensorState(zoneId, isOpen);
    Serial.printf("⚡ [%s] Cambio en Zona %d: %s\n", sourceName, zoneId, isOpen ? "OPEN" : "CLOSED");
    broadcast_ui_update();

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
            if (zoneId != 1) {
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
        armedModeBeforeDelay = currentMode;
        currentMode = ENTRY_DELAY;
        entryDelayStartTime = millis();
        lastEntrySecondsLeft = -1;
        mqtt_publish_state("pending");
        Serial.printf("\n⏳ [SISTEMA] Puerta principal abierta (%s). INICIANDO RETARDO DE ENTRADA (15s)...\n", sourceName);
    }
    if (shouldTrigger) {
        currentMode = TRIGGERED;
        mqtt_publish_state("triggered");
        Serial.printf("\n🔴🔴🔴 [ALERTA] INTRUSION DETECTADA EN ZONA %d (%s) 🔴🔴🔴\n", zoneId, sourceName);
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
    for (auto& zone : physicalZones) {
        pinMode(zone.pin, INPUT_PULLDOWN);
        zone.lastPinReading = digitalRead(zone.pin);
        sensors[zone.sensorIndex] = (zone.lastPinReading == LOW); // LOW = OPEN
    }
    currentMode = DISARMED;

    // Start the virtual panel
    dsc.begin();
    dsc.key = 0xFF; // Reset to custom idle state 
    
    // Set initial "Clean" board state
    dsc.lightReady = on;
    dsc.lightArmed = off;
    dsc.lightTrouble = off;
    dsc.lightBacklight = on;

    mqtt_publish_state("disarmed");
    m_remote_event = AppEventLoop::subscribe(ALARM_EVENT, ALARM_SET_REMOTE, [](const uint8_t* data, size_t size){
        if(size == 0) return;
        std::string cmd(reinterpret_cast<const char*>(data), size);
        if (cmd == "ARMED_AWAY") user_alarm_arm_away();
        else if (cmd == "ARMED_HOME") {
            if (currentMode != ARMED_HOME && currentMode != ARMING_HOME) {
                currentMode = ARMING_HOME;
                armingStartTime = millis();
                dsc.beep(1); // First immediate arming confirmation beep
                Serial.println("\n🏠 [SISTEMA] Iniciando ARMADO HOME...");
            }
        }
        else if (cmd == "DISARMED") user_alarm_disarm();
    });
}

extern "C" void user_alarm_arm_away() {
    if (currentMode != ARMED_AWAY && currentMode != ARMING_AWAY) {
        currentMode = ARMING_AWAY;
        armingStartTime = millis();
        lastSecondsLeft = -1;
        
        dsc.beep(1); // First immediate arming confirmation beep
        
        Serial.println("\n🟠 [SISTEMA] Iniciando ARMADO AWAY...");
    }
}

extern "C" void user_alarm_disarm() { 
    if (currentMode != DISARMED) {
        currentMode = DISARMED;
 
        // Clear active beeps and buzzer
        dsc.beep(0);
        dsc.buzzer(0);
        // Play double confirmation beep
        dsc.beep(2);
 
        mqtt_publish_state("disarmed");
        Serial.println("\n🟢 [SISTEMA] ALARMA DESARMADA");
        print_status();
    }
}
 
extern "C" void user_alarm_loop() {
    // 1. Maintain Keybus clock (MUST RUN CONSTANTLY)
    dsc.loop();

    // 2. Handle Keypad input
    if (dsc.key != 0xFF) {
        byte rawKey = dsc.key;
        dsc.key = 0xFF; // Clear buffer
        char key = decodeDscKey(rawKey);
        
        if (key != '?') {
            Serial.printf("⌨️ [TECLADO DSC] Tecla presionada: [ %c ]\n", key);
            
            if (key >= '1' && key <= '8') {
                int idx = key - '1';
                // Toggle simulated zone state
                trigger_zone_change(idx, !sensors[idx], "TECLADO DSC");
            }
            else if (key == '9') {
                if (currentMode == DISARMED) {
                    user_alarm_arm_away();
                } else {
                    user_alarm_disarm();
                }
            }
            else if (key == '0') {
                Serial.println(">> BUZZER ACTIVATED (2 Seconds)");
                dsc.buzzer(2);
            }
            else if (key == '#') {
                if (currentMode == DISARMED) {
                    user_alarm_arm_away();
                }
            }
            else if (key == '*') {
                user_alarm_disarm();
            }
        }
    }

    // 3. Monitor physical zones via GPIO
    if (millis() - lastSamplingTime >= SAMPLING_INTERVAL) {
        lastSamplingTime = millis();
        for (auto& zone : physicalZones) {
            int reading = digitalRead(zone.pin);
            if (reading != zone.lastPinReading) {
                zone.lastDebounceTime = millis();
            }
            if ((millis() - zone.lastDebounceTime) > DEBOUNCE_DELAY) {
                bool isOpen = (reading == LOW);
                if (isOpen != sensors[zone.sensorIndex]) {
                    trigger_zone_change(zone.sensorIndex, isOpen, "HARDWARE");
                }
            }
            zone.lastPinReading = reading;
        }
    }
 
    // 4. Handle Exit Delay Timer & Keypad Beeping
    if (currentMode == ARMING_AWAY || currentMode == ARMING_HOME) {
        unsigned long elapsed = millis() - armingStartTime;
        if (elapsed >= EXIT_DELAY_MS) {
            if (currentMode == ARMING_AWAY) {
                currentMode = ARMED_AWAY;
                mqtt_publish_state("armed_away");
                Serial.println("\n🟠 [SISTEMA] !!! ALARMA ARMADA (AWAY) !!!");
            } else {
                currentMode = ARMED_HOME;
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
            currentMode = TRIGGERED;
            mqtt_publish_state("triggered");
            Serial.println("\n🔴🔴🔴 [ALERTA] TIEMPO DE ENTRADA AGOTADO - INTRUSION DETECTADA 🔴🔴🔴");
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

    // 7. Handle Triggered Siren / Keypad Buzzer Wailing
    static unsigned long last_beep = 0;
    if (currentMode == TRIGGERED && millis() - last_beep > 1000) {
        Serial.println("📢 !!! SIRENA ACTIVA !!!");
        dsc.buzzer(2); // Sounds DSC keypad buzzer for 2s continuously
        last_beep = millis();
    }

    // 8. Synchronize physical keypad LEDs with system state in real-time
    dsc.lightZone1 = sensors[0] ? on : off;
    dsc.lightZone2 = sensors[1] ? on : off;
    dsc.lightZone3 = sensors[2] ? on : off;
    dsc.lightZone4 = sensors[3] ? on : off;
    dsc.lightZone5 = sensors[4] ? on : off;
    dsc.lightZone6 = sensors[5] ? on : off;
    dsc.lightZone7 = sensors[6] ? on : off;
    dsc.lightZone8 = sensors[7] ? on : off;

    bool anyZoneOpen = false;
    for (int i = 0; i < 6; i++) {
        if (sensors[i]) anyZoneOpen = true;
    }
    dsc.lightReady = (currentMode == DISARMED && !anyZoneOpen) ? on : off;

    if (currentMode == ARMED_AWAY || currentMode == ARMED_HOME) {
        dsc.lightArmed = on;
    } else if (currentMode == ARMING_AWAY || currentMode == ARMING_HOME) {
        dsc.lightArmed = blink;
    } else {
        dsc.lightArmed = off;
    }
}

extern "C" void user_alarm_failed_tap() { 
    Serial.println("NFC Fail"); 
    // Play warning tone pattern on keypad to indicate failed tap
    dsc.beep(4); 
}

