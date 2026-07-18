# Project Architecture & AI Agent Guide

Welcome to the **Vector Security & HomeKey-ESP32 Alarm System**. This document provides an architectural overview, hardware specifications, instructions for AI coding assistants (such as Claude, Gemini, GPT-4, etc.), and the complete Arduino Uno RF Decoder sketch.

---

## 🏛️ System Architecture

The ecosystem consists of three main hardware and software layers:

```
┌──────────────────────────────────────┐
│  DSC WS4945 / 433MHz Sensors         │
│  (Gadnic / EV1527 RF Sensors)        │
└──────────────────┬───────────────────┘
                   │ 433.92 MHz RF
                   ▼
┌──────────────────────────────────────┐
│  Arduino Uno RF Decoder Bridge       │ ──► Reads RF signals (Interrupt 0 / Pin 2)
│  (Data Pin 2, Output Pin 3)          │ ──► Sends 3.3V Logic Signal via Pin 3
└──────────────────┬───────────────────┘
                   │ Voltage Divider (R1=4.7k, R2=10k) + Shared GND
                   ▼
┌──────────────────────────────────────┐
│  ESP32 Alarm Controller & Web UI     │ ──► Physical Zone Inputs (GPIO 13, 17, 14, 25, 27, 32)
│  (C++ ESP-IDF / Svelte 5 Web UI)     │ ──► Siren Output Relay (GPIO 26)
└─────────┬──────────────────┬─────────┘ ──► Physical DSC Keybus (Clock 21, Read 18, Write 19)
          │                  │
          ▼                  ▼
┌──────────────────┐  ┌─────────────────────────────┐
│ Home Assistant   │  │ Mobile App (React Native)   │
│ (MQTT Broker)    │  │ (vector-security-app)       │
└──────────────────┘  └─────────────────────────────┘
```

---

## 🤖 Instructions for AI Agents (Claude / Gemini / GPT)

When analyzing, modifying, or extending this codebase, follow these guidelines:

1. **Target Workspace**:
   - Firmware & Web UI: [alarma-homekey-arduino](file:///Users/defeee/alarma-homekey-arduino)
   - Mobile Client: [vector-security-app](file:///Users/defeee/PycharmProjects/app-alarma/vector-security-app)

2. **Core Logic Locations**:
   - Alarm State Machine & Keypad Logic: [user_alarm.cpp](file:///Users/defeee/alarma-homekey-arduino/main/user_alarm.cpp) / [user_alarm.h](file:///Users/defeee/alarma-homekey-arduino/main/include/user_alarm.h)
   - Configuration Management (NVS): [ConfigManager.cpp](file:///Users/defeee/alarma-homekey-arduino/main/ConfigManager.cpp) / [config.hpp](file:///Users/defeee/alarma-homekey-arduino/main/include/config.hpp)
   - Web Server & WebSockets: [WebServerManager.cpp](file:///Users/defeee/alarma-homekey-arduino/main/WebServerManager.cpp)
   - Web UI Dashboard: [HKInfo.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/components/HKInfo.svelte)
   - Hardware Pin Settings UI: [AppMisc.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/components/AppMisc.svelte) / [HardwareConfig.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/components/HardwareConfig.svelte)

3. **Build & Flash Commands**:
   - Web UI build: `cd data && bun run build`
   - ESP32 Firmware build: `source ~/esp/esp-idf/export.sh && idf.py build`
   - OTA Firmware flash:
     ```bash
     http_proxy="" https_proxy="" curl --http0.9 -H "Expect:" -v --data-binary @build/HomeKey-ESP32.bin http://192.168.68.200/ota/firmware
     ```
   - OTA Filesystem (LittleFS) flash:
     ```bash
     http_proxy="" https_proxy="" curl --http0.9 -H "Expect:" -v --data-binary @build/spiffs.bin http://192.168.68.200/ota/littlefs
     ```

4. **Hardware Constraints**:
   - ESP32 GPIs **34, 35, 36, 39 are INPUT-ONLY**. They cannot drive output loads or relays.
   - Siren Relay Output is assigned to **GPIO 26** (Active HIGH).

---

## ⚡ Arduino Uno 433MHz Decoder & Bridge Code

Below is the complete standalone Arduino Uno sketch (`decoder_bridge.ino`) used to capture wireless DSC WS4945 sensors (and EV1527/Gadnic sensors) and bridge them to the ESP32 Zone input pin.

```cpp
/* 
  ===========================================================================
  ARDUINO UNO: DSC WS4945 MASTER DECODER & ESP32 BRIDGE (WITH SERIAL DIAGNOSTICS)
  DATA Pin -> Uno Pin 2 | Bridge Out Pin -> Uno Pin 3 (To Voltage Divider / 10k)
  ===========================================================================
*/

// --- SIGNAL TIMINGS ---
#define BIT_MIN       150   
#define BIT_MAX       2500  
#define DATA_PIVOT    550   

const int BRIDGE_OUT_PIN = 3; // Connect through Voltage Divider to ESP32 GPIO 13
const unsigned long COOLDOWN_TIME = 1000; // 1 second protection frame to limit serial spam

volatile unsigned long lastPulseTime = 0;
volatile uint32_t rollingBuffer = 0;

// --- VOLATILE MEMORY TRANSFERS FOR LOOP PRINTING ---
volatile byte printStateFlag = 0; // 0 = Idle, 1 = Opened/Tampered, 2 = Closed/Secure
unsigned long lastPrintTime = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(BRIDGE_OUT_PIN, OUTPUT);
  digitalWrite(BRIDGE_OUT_PIN, LOW); // Default to a clean secure state on startup
  
  pinMode(2, INPUT); // Hardware Interrupt Pin 0
  attachInterrupt(0, dscSignatureBridgeISR, CHANGE);

  Serial.println(F("\n=================================================="));
  Serial.println(F("     ARDUINO UNO MASTER DECODER & BRIDGE READY     "));
  Serial.println(F("=================================================="));
}

void loop() {
  // Safely check if the interrupt caught an RF footprint event
  if (printStateFlag != 0) {
    byte stateSnapshot = printStateFlag;
    printStateFlag = 0; // Clear flag instantly
    
    unsigned long currentTime = millis();
    if (currentTime - lastPrintTime >= COOLDOWN_TIME) {
      lastPrintTime = currentTime;

      Serial.print(F("[UNO DECODER] "));
      if (stateSnapshot == 1) {
        Serial.println(F("ALARM TRACKED: 🔴 Sending HIGH Signal to ESP32"));
      } else if (stateSnapshot == 2) {
        Serial.println(F("SYSTEM NORMAL: 🟢 Sending LOW Signal to ESP32"));
      }
    }
  }
}

void dscSignatureBridgeISR() {
  unsigned long now = micros();
  unsigned long duration = now - lastPulseTime;
  lastPulseTime = now;

  if (duration >= BIT_MIN && duration <= BIT_MAX) {
    bool bit = (duration > DATA_PIVOT);
    rollingBuffer = (rollingBuffer << 1) | bit;

    // IF DOOR OPEN OR CHASSIS TAMPER DETECTED: Drive the bridge signal HIGH (5V)
    if (rollingBuffer == 0x028A2A2 || rollingBuffer == 0xC00A28A8 || 
        rollingBuffer == 0x80145151 || rollingBuffer == 0x80155151 || rollingBuffer == 0x54544551) {
      digitalWrite(BRIDGE_OUT_PIN, HIGH);
      printStateFlag = 1; // Signal the loop to print an alert
    } 
    // IF DOOR CLOSED OR TAMPER SECURED: Drive the bridge signal LOW (0V)
    else if (rollingBuffer == 0x02828A8 || rollingBuffer == 0x80141454 || 
             rollingBuffer == 0x0505151 || rollingBuffer == 0x02828A88) {
      digitalWrite(BRIDGE_OUT_PIN, LOW);
      printStateFlag = 2; // Signal the loop to print normal status
    }
  }
}
```

---

## 🔌 Hardware Interconnection Schematic

To connect the Arduino Uno output (5V logic) to the ESP32 input (3.3V logic):

```text
[ ARDUINO UNO ]                                              [ ESP32 ]
  Pin 3 (BRIDGE_OUT) ────[ R1: 4.7kΩ ]───┬───────────────►  GPIO 13 (Zone 1)
                                         │
                                     [ R2: 10kΩ ]
                                         │
  GND ───────────────────────────────────┴───────────────►  GND
```

*Note: Connecting GND between both microcontrollers is mandatory to establish a shared reference.*
