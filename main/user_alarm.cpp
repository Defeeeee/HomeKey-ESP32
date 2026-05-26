#include "include/user_alarm.h"
#include <Arduino.h>
#include <string>
#include <vector>
#include "include/app_event_loop.hpp"
#include "include/app_events.hpp"
#include "include/ConfigManager.hpp"
#include "include/MqttManager.hpp"
#include "include/WebServerManager.hpp"

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
const unsigned long EXIT_DELAY_MS = 10000; // 10 segundos de cuenta atrás
int lastSecondsLeft = -1;

unsigned long entryDelayStartTime = 0;
const unsigned long ENTRY_DELAY_MS = 15000; // 15 segundos de retardo de entrada
int lastEntrySecondsLeft = -1;
AlarmMode armedModeBeforeDelay = ARMED_AWAY;

extern std::unique_ptr<ConfigManager> configManager;
extern std::unique_ptr<MqttManager> mqttManager;
extern std::unique_ptr<WebServerManager> webServerManager;

void broadcast_ui_update() {
    if (webServerManager) {
        webServerManager->broadcastDeviceMetrics();
    }
}

void mqtt_publish_state(const char* state) {
    AppEventLoop::publish(ALARM_EVENT, ALARM_STATE_CHANGED, (const uint8_t*)state, strlen(state));
    broadcast_ui_update();
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

    mqtt_publish_state("disarmed");
    m_remote_event = AppEventLoop::subscribe(ALARM_EVENT, ALARM_SET_REMOTE, [](const uint8_t* data, size_t size){
        if(size == 0) return;
        std::string cmd(reinterpret_cast<const char*>(data), size);
        if (cmd == "ARMED_AWAY") user_alarm_arm_away();
        else if (cmd == "ARMED_HOME") {
            if (currentMode != ARMED_HOME && currentMode != ARMING_HOME) {
                currentMode = ARMING_HOME;
                armingStartTime = millis();
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
        Serial.println("\n🟠 [SISTEMA] Iniciando ARMADO AWAY...");
    }
}

extern "C" void user_alarm_disarm() { 
    if (currentMode != DISARMED) {
        currentMode = DISARMED;

        mqtt_publish_state("disarmed");
        Serial.println("\n🟢 [SISTEMA] ALARMA DESARMADA");
        print_status();
    }
}

extern "C" void user_alarm_loop() {

    // Monitorear todas las zonas físicas
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
                    sensors[zone.sensorIndex] = isOpen;
                    if (mqttManager) mqttManager->publishSensorState(zone.id, isOpen);
                    Serial.printf("⚡ [HARDWARE] Cambio en Zona %d: %s\n", zone.id, isOpen ? "OPEN" : "CLOSED");
                    broadcast_ui_update();
                    
                    bool shouldTrigger = false;
                    bool shouldStartEntryDelay = false;
                    if (isOpen) {
                        if (currentMode == ARMED_AWAY) {
                            if (zone.id == 1) {
                                shouldStartEntryDelay = true;
                            } else {
                                shouldTrigger = true;
                            }
                        } else if (currentMode == ARMED_HOME) {
                            uint8_t mask = configManager->getConfig<espConfig::misc_config_t>().armedHomeZones;
                            if ((mask >> zone.sensorIndex) & 0x01) {
                                if (zone.id == 1) {
                                    shouldStartEntryDelay = true;
                                } else {
                                    shouldTrigger = true;
                                }
                            }
                        } else if (currentMode == ENTRY_DELAY) {
                            // Si ya estamos en retardo de entrada y se abre una zona instantánea (que no sea Z1)
                            if (zone.id != 1) {
                                if (armedModeBeforeDelay == ARMED_AWAY) {
                                    shouldTrigger = true;
                                } else if (armedModeBeforeDelay == ARMED_HOME) {
                                    uint8_t mask = configManager->getConfig<espConfig::misc_config_t>().armedHomeZones;
                                    if ((mask >> zone.sensorIndex) & 0x01) {
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
                        Serial.println("\n⏳ [SISTEMA] Puerta principal abierta. INICIANDO RETARDO DE ENTRADA (15s)...");
                    }
                    if (shouldTrigger) {
                        currentMode = TRIGGERED;
                        mqtt_publish_state("triggered");
                        Serial.printf("\n🔴🔴🔴 [ALERTA] INTRUSION DETECTADA EN ZONA %d 🔴🔴🔴\n", zone.id);
                    }
                }
            }
            zone.lastPinReading = reading;
        }
    }
 
    // Manejar cuenta atrás de armado
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
            print_status();
        } else {
            int secondsLeft = (EXIT_DELAY_MS - elapsed) / 1000;
            if (secondsLeft != lastSecondsLeft) {
                Serial.printf("⏳ ARMANDO EN %d SEG...\n", secondsLeft + 1);
                lastSecondsLeft = secondsLeft;
            }
        }
    }

    // Manejar cuenta atrás de retardo de entrada
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
        }
    }

    // Monitorización de sensores y sirena (solo si ya está armado)
    if (currentMode == ARMED_AWAY || currentMode == ARMED_HOME) {
        // (Lógica de sensores igual que antes)
    }

    if (Serial.available() > 0) {
        char c = toupper(Serial.read());
        if (c == 'A') user_alarm_arm_away();
        else if (c == 'D') user_alarm_disarm();
        else if (c >= '1' && c <= '8') {
            int idx = c - '1';
            sensors[idx] = !sensors[idx];
            bool isOpen = sensors[idx];
            int zoneId = idx + 1;
            if (mqttManager) mqttManager->publishSensorState(zoneId, isOpen);
            Serial.printf("⚡ [SIMULADO] Cambio en Zona %d: %s\n", zoneId, isOpen ? "OPEN" : "CLOSED");
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
                    if ((mask >> idx) & 0x01) {
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
                            if ((mask >> idx) & 0x01) {
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
                Serial.println("\n⏳ [SISTEMA] Puerta principal abierta (SIMULADO). INICIANDO RETARDO DE ENTRADA (15s)...");
            }
            if (shouldTrigger) {
                currentMode = TRIGGERED;
                mqtt_publish_state("triggered");
                Serial.printf("\n🔴🔴🔴 [ALERTA] INTRUSION DETECTADA EN ZONA %d (SIMULADO) 🔴🔴🔴\n", zoneId);
            }
            print_status();
        }
    }

    static unsigned long last_beep = 0;
    if (currentMode == TRIGGERED && millis() - last_beep > 1000) {
        Serial.println("📢 !!! SIRENA ACTIVA !!!");
        last_beep = millis();
    }
}

extern "C" void user_alarm_failed_tap() { Serial.println("NFC Fail"); }

