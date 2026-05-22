#include "include/user_alarm.h"
#include <Arduino.h>
#include <string>
#include <vector>
#include "include/app_event_loop.hpp"
#include "include/app_events.hpp"
#include "include/ConfigManager.hpp"

enum AlarmMode { DISARMED, ARMING_AWAY, ARMING_HOME, ARMED_AWAY, ARMED_HOME, TRIGGERED };
AlarmMode currentMode = DISARMED;
bool sensors[8] = {false, false, false, false, false, false, false, false};
AppEventLoop::SubscriptionHandle m_remote_event;

unsigned long armingStartTime = 0;
const unsigned long EXIT_DELAY_MS = 10000; // 10 segundos de cuenta atrás
int lastSecondsLeft = -1;

extern std::unique_ptr<ConfigManager> configManager;

void mqtt_publish_state(const char* state) {
    AppEventLoop::publish(ALARM_EVENT, ALARM_STATE_CHANGED, (const uint8_t*)state, strlen(state));
}

void print_status() {
    Serial.printf("\n==================================================\n");
    Serial.print("MODO ALARMA: ");
    if(currentMode == DISARMED) Serial.println("⚪ DESARMADO");
    else if(currentMode == ARMING_AWAY) Serial.println("⏳ PENDIENTE (AWAY)");
    else if(currentMode == ARMING_HOME) Serial.println("⏳ PENDIENTE (HOME)");
    else if(currentMode == ARMED_AWAY) Serial.println("🟠 ARMADO (AWAY)");
    else if(currentMode == ARMED_HOME) Serial.println("🏠 ARMADO (HOME)");
    else Serial.println("🔴 !!! DISPARADO !!!");
    
    Serial.print("SENSORES: [");
    for(int i=0; i<8; i++) Serial.print(sensors[i] ? "! " : ". ");
    Serial.println("]");
    Serial.println("==================================================\n");
}

extern "C" void user_alarm_setup() { 
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
            bool trig = false;
            if (currentMode == ARMED_AWAY && sensors[idx]) trig = true;
            if (currentMode == ARMED_HOME && sensors[idx]) {
                uint8_t mask = configManager->getConfig<espConfig::misc_config_t>().armedHomeZones;
                if ((mask >> idx) & 0x01) trig = true;
            }
            if (trig) {
                currentMode = TRIGGERED;
                mqtt_publish_state("triggered");
                Serial.println("\n🔴🔴🔴 [ALERTA] INTRUSION DETECTADA 🔴🔴🔴");
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
