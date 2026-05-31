<div align="center">
  <img width="180" height="180" alt="alarm-system-logo" src="https://github.com/user-attachments/assets/6c4bc1e8-c294-4a4b-842a-9837a680b913" style="border-radius: 36px; box-shadow: 0 10px 25px rgba(0,0,0,0.3);" />

  # ESP32 HomeKey-Enabled Alarm System
  ### *A professional-grade, multi-zone DIY security system with Apple HomeKey support*

  [![Discord](https://badgen.net/discord/members/VWpZ5YyUcm?icon=discord)](https://discord.com/invite/VWpZ5YyUcm)
  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
  
  **A complete burglar alarm control panel featuring native Apple HomeKit & NFC-based Apple HomeKey authentication.**
  
  [Web Interface Documentation](https://rednblkx.github.io/HomeKey-ESP32/)

</div>

---

## 📌 Project Overview & Scope Pivot

Originally conceived as an NFC-based smart lock integration, this project has evolved into a **fully featured, DIY Alarm Control Panel** running on an ESP32. Instead of Apple HomeKey being the product itself, **HomeKey (NFC) now serves as the secure, high-speed credential system** to arm, disarm, and manage a complete household security grid.

The system mimics professional panels (like the DSC PowerSeries) by organizing physical and simulated sensors into security zones, enforcing standard Entry/Exit delay grace periods, sounding wailing sirens during breaches, and supporting physical/virtual DSC keypads. Tapping your iPhone or Apple Watch (via Apple HomeKey) instantly disarms the alarm and unlocks the entry point in a single, seamless, sub-300ms transaction.

---

## ✨ Features & What We Have Done

We have built a production-ready, split-architecture system consisting of **ESP32 Controller Firmware** and a high-fidelity **Companion Mobile App**.

### 1. Core Alarm Controller (State Machine)
We implemented a rigid security state machine running in the firmware (`user_alarm.cpp`) that enforces transition rules and safety measures:
*   **States**: `DISARMED`, `ARMING_AWAY`, `ARMING_HOME`, `ARMED_AWAY`, `ARMED_HOME`, `ENTRY_DELAY` (Pending), and `TRIGGERED` (Alarm wailing).
*   **Security Auditing**: Ready check prevents arming if any unbypassed zones are open.
*   **Entry/Exit Delays**: 15-second configurable count-down grace periods (aligned with DSC standard protocols) to enter/exit before arming or triggering.

### 2. Physical & Virtual DSC Keypad Integration
We fully integrated support for legacy hardware keypads (e.g., DSC PowerSeries PK5501/PC1555 keypads) using the DSC Keybus protocol:
*   **Real-time LED Mirroring**: The ESP32 controls the physical keypad's status LEDs (Ready, Armed, Bypass, Memory, Trouble) to reflect system states in real-time.
*   **Interactive Command Codes**:
    *   `*1`: **Zone Bypass Mode** (Bypasses active zones using keys 1-8).
    *   `*2`: **Trouble Diagnosis Mode** (LED 1 lights up for WiFi disconnection, LED 2 for NFC reader issues).
    *   `*3`: **Alarm Memory Mode** (Displays which zones triggered the alarm during the last armed period).
    *   `*4`: **Chime Control** (Toggles entry/exit door chimes).
    *   `*7`: **Zone Simulation Mode** (Permits testing and sensor toggles without physical hardware triggers).
    *   `*0` & `*9`: Quick arming in Away and Home modes, respectively.
*   **Hardware Protections**: Included anti-ghosting filtering to handle unpowered or unstable keypad connections (e.g., filtering false sequential `0` keypresses).
*   **Code Entry Validation**: Captures 4-digit user PINs to arm and disarm the system.

### 3. Multi-Zone Security Matrix
*   **8 Independent Zones**: Supports up to 8 sensor zones (doors, windows, motion detectors).
*   **Physical Inputs**: Configured via physical ESP32 GPIOs with internal pull-ups and active low/high triggers.
*   **Software Debouncing**: Integrated a 50ms software debounce filter to eliminate false triggers on noisy reed switches.
*   **Bypassing**: Zones can be bypassed individually either via the physical keypad or remotely via MQTT/Home Assistant.

### 4. Apple HomeKey (NFC) Credential System
*   **PN532 & PN7160 Driver Support**: High-performance NFC drivers supporting SPI/I2C.
*   **Apple Express Mode**: Authenticate and disarm the system using an iPhone or Apple Watch without waking the device or requiring biometric/passcode checks.
*   **Power Reserve Support**: Allows entry using Apple Watch/iPhone even when their main batteries are depleted.
*   **Immediate Feedback**: Taps generate instant audio-visual responses (double-beeps for successful disarm, error chirps on the keypad for failed taps).

### 5. Native Apple HomeKit Integration
*   Built on the **HomeSpan** framework, exposing the system directly to the iOS/macOS Apple Home app.
*   Presents itself as a native Security System Accessory, allowing native iOS control.

### 6. Smart Home & MQTT Broker Sync
*   Publishes alarm states (`disarmed`, `arming`, `armed_away`, `armed_home`, `pending`, `triggered`) and individual zone states (open, closed, bypassed) instantly.
*   **Home Assistant Autodiscovery**: Automatically exposes alarm control panels and binary sensors to Home Assistant without manual configuration.

### 7. Companion Mobile App (`vector-security-app`)
We designed and built a stunning, iOS-inspired hybrid app using React Native and Expo:
*   **Glassmorphic Aesthetic**: Premium dark mode UI featuring real-time blurred backgrounds (`expo-blur`), radial glowing status rings, micro-animations, and haptic feedback.
*   **Real-time WebSocket Sync**: Continuous communication with Home Assistant's WebSocket API to mirror and control states.
*   **Biometrics Integration**: Secure arming/disarming commands validated through FaceID or TouchID before transmission.
*   **Sensor Management**: Dedicated zone tab showcasing status (Open, Closed, Bypassed) of all 8 zones with manual bypass sliders.
*   **Secure Settings**: Persistent configurations secured locally via Android/iOS `expo-secure-store`.

---

## 📐 System Architecture

The following diagram illustrates how the hardware modules, firmware logic, MQTT broker, and mobile app interface together:

```mermaid
graph TD
    %% Users & Devices
    Watch[Apple Watch / iPhone] -->|NFC Apple HomeKey| NFC[PN532 / PN7160 Reader]
    Keypad[Physical DSC Keypad] -->|DSC Keybus| ESP[ESP32 Main Control Unit]
    Sensors[Physical GPIO Sensors 1-8] -->|Dry Contact / Reed| ESP
    
    %% ESP32 Internal Logic
    subgraph ESP32 ["ESP32 Controller Firmware"]
        NFC -->|SPI/I2C| NfcMgr[NfcManager]
        Keypad <-->|GPIO Clk-R-W| UserAlarm[user_alarm State Machine]
        Sensors -->|GPIO Interrupts| HwMgr[HardwareManager]
        
        NfcMgr -->|Valid Token Verified| UserAlarm
        HwMgr -->|Debounced Zone Open/Close| UserAlarm
        
        UserAlarm -->|Buzzer / LEDs| Keypad
        UserAlarm -->|Trigger Wailing Siren| Siren[Physical Siren / Relay]
        
        Config[ConfigManager] <-->|Persists Data| NVS[(NVS Flash)]
        WebServer[WebServerManager] <-->|OTA / Config Webpage| WebUI[Browser UI]
    end

    %% Network & Integrations
    UserAlarm <-->|HomeKit Protocol| HomeSpan[HomeSpan Bridge]
    UserAlarm <-->|MQTT Status & Cmds| MQTT[MQTT Broker]
    
    HomeSpan <-->|Native Hub Integration| AppleHome[Apple Home App]
    MQTT <-->|Auto-Discovery| HA[Home Assistant]
    
    %% Companion App
    HA <-->|WebSocket Stream| App[Mobile Companion App]
    App -->|Biometric/PIN Verification| AppAction[Arm / Disarm / Bypass]
    AppAction -->|WS Commands| HA

    style ESP fill:#111827,stroke:#3b82f6,stroke-width:2px,color:#fff
    style UserAlarm fill:#1e3a8a,stroke:#3b82f6,color:#fff
    style NFC fill:#065f46,stroke:#10b981,color:#fff
    style Keypad fill:#7c2d12,stroke:#ea580c,color:#fff
    style App fill:#5b21b6,stroke:#8b5cf6,color:#fff
    style HA fill:#1e293b,stroke:#475569,color:#fff
```

---

## 📂 Firmware Project Directory Structure

```
alarma-homekey-arduino/
├── main/                       # Core ESP-IDF / C++ application source
│   ├── main.cpp                # System Entrypoint & manager startup
│   ├── user_alarm.cpp          # Core Alarm State Machine & Zone Matrix
│   ├── HardwareManager.cpp     # GPIO monitoring & zone debouncer
│   ├── NfcManager.cpp          # HomeKey NFC protocol handler
│   ├── HomeKitLock.cpp         # HomeSpan/HomeKit device bridge
│   ├── MqttManager.cpp         # Home Assistant MQTT integration
│   ├── WebServerManager.cpp    # Config Web UI, Websocket Server & OTA
│   ├── ConfigManager.cpp       # EEPROM/NVS parameters manager
│   ├── ReaderDataManager.cpp   # HomeKey credential verification
│   └── include/                # Header Declarations
│       ├── user_alarm.h        # Public alarm control interfaces
│       ├── config.hpp          # Persistent storage structs
│       └── defaults.h          # Hardcoded fallbacks & GPIO presets
├── components/                 # Git submodules & libraries
│   ├── HK-HomeKit-Lib/         # Under-the-hood Apple HomeKey cryptographic engine
│   ├── HomeSpan/               # HomeKit Accessory Protocol (HAP) engine
│   └── PN532/                  # NFC chip drivers
├── data/                       # Svelte 5 Web interface build files
└── docs/                       # Project static pages (Hugo site source)
```

---

## 🛠️ Getting Started & Wiring

### 1. Prerequisites
*   **ESP32 Development Board** (ESP32-WROOM-32 or ESP32-S3).
*   **NFC Module**: PN532 (SPI recommended) or PN7160 (I2C).
*   **DSC Keypad**: PowerSeries PC1555 / PK5501 (optional, for physical interface).
*   **Sensors**: Magnetic door reed switches, PIR motion sensors, or limit switches.

### 2. Wiring Connections

#### NFC Module (PN532 SPI)
| PN532 Pin | ESP32 GPIO | Description |
|-----------|------------|-------------|
| VCC       | 5V / 3.3V  | Power |
| GND       | GND        | Ground |
| SCK       | GPIO 14    | SPI Clock |
| MISO      | GPIO 12    | SPI Master In Slave Out |
| MOSI      | GPIO 13    | SPI Master Out Slave In |
| SS/CS     | GPIO 15    | SPI Chip Select |

#### DSC Keypad Keybus
Connection requires interfacing with the keypad's green (data out) and yellow (clock) lines. Use level-shifting transistors or resistor dividers if stepping down from the DSC 12V logic levels to ESP32 3.3V logic.
*   **DSC Clock (Yellow)** $\rightarrow$ **GPIO 21**
*   **DSC Read (Green - Keypad to Panel)** $\rightarrow$ **GPIO 18**
*   **DSC Write (Green - Panel to Keypad)** $\rightarrow$ **GPIO 19**

#### Zone Sensors (Zones 1-8)
Connect dry contacts between the designated GPIO and GND. The firmware configures internal pull-ups.
*   **Zone 1 (Entry/Exit door)**: GPIO 13
*   **Zone 2**: GPIO 17
*   **Zone 3**: GPIO 14
*   **Zone 4**: GPIO 25
*   **Zone 5**: GPIO 26
*   **Zone 6**: GPIO 27
*   **Zone 7**: GPIO 32
*   **Zone 8**: Configurable / Virtual

---

## 💻 Compilation & Flashing

This project uses the **ESP-IDF v5.1+** toolchain.

```bash
# 1. Clone repository and initialize submodules
git clone --recursive https://github.com/Defeeeee/HomeKey-ESP32.git
cd HomeKey-ESP32

# 2. Set targets and build
idf.py set-target esp32
idf.py build

# 3. Flash to ESP32 and monitor output
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## 📱 Companion Mobile App (`vector-security-app`)

The companion mobile app is stored in the `vector-security-app` workspace. It works side-by-side with the ESP32 through Home Assistant.

### How to Install and Run:
1.  Navigate to `/Users/defeee/PycharmProjects/app-alarma/vector-security-app`.
2.  Install dependencies:
    ```bash
    npm install
    ```
3.  Start the Expo development server:
    ```bash
    npx expo start
    ```
4.  Open the iOS Simulator (`i`) or scan the QR code using your physical device.

---

## ⚖️ Disclaimer & License
*   This project is licensed under the **MIT License**.
*   **Disclaimer**: This project implements Apple HomeKey functionality through reverse engineering. Use at your own risk in security-critical environments. Not affiliated with Apple Inc. or DSC Tyco Security Products.
