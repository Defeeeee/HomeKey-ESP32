# AI Agent Collaboration & Progress Protocol (`AGENTS.md`)

This document defines the **standard operating procedure and handoff protocol** for all AI agents (Claude, Gemini, Antigravity, GPT-4, etc.) working on this repository.

> **RULE FOR ALL AI AGENTS**: Before starting work, read this document to understand the latest project status, hardware pinouts, and active tasks. At the end of every work session or major milestone, **update this document** with a summary of changes made, current hardware state, and next steps.

---

## 📌 Current System State & Configurations

| Parameter | Value / Status | Notes |
| :--- | :--- | :--- |
| **ESP32 Local IP** | `192.168.68.200` | Fixed local network IP |
| **Home Assistant IP** | `100.112.141.102:8123` | Tailscale VPN |
| **MQTT Broker** | `192.168.68.120:1883` | Topic: `home/alarm/#` |
| **Firmware Branch** | `feature/mock-alarm-arduino` | Repository: `Defeeeee/HomeKey-ESP32` |
| **Mobile App Client** | `vector-security-app` | Repository: `Defeeeee/ESP32-AlarmApp` |

---

## ⚡ Active Hardware Pinout Mapping

| Function / Component | ESP32 GPIO Pin | Details |
| :--- | :--- | :--- |
| **DSC Keybus Clock** | **GPIO 21** | Keypad Interface |
| **DSC Keybus Read** | **GPIO 18** | Keypad Interface |
| **DSC Keybus Write** | **GPIO 19** | Keypad Interface |
| **Zone 1** (Front Door / Uno Bridge) | **GPIO 13** | Input (Pull-up / Active HIGH from Uno) |
| **Zone 2** (Living Room Motion) | **GPIO 17** | Input |
| **Zone 3** (Bedroom Window Flap A) | **GPIO 14** | Input |
| **Zone 4** (Fondo B) | **GPIO 25** | Input |
| **Zone 5** (Planta Alta Curtain) | **GPIO 27** | Input |
| **Zone 7** | **GPIO 32** | Input |
| **Physical Siren Relay Output** | **GPIO 26** | **Active HIGH** (Drives external siren relay) |

---

## 🛠️ Key Firmware Features & Conventions

1. **MQTT Keep-Alive**: Configured to **60 seconds** ([MqttManager.cpp](file:///Users/defeee/alarma-homekey-arduino/main/MqttManager.cpp#L167)) to absorb transient Wi-Fi drops and prevent false `unavailable` states in Home Assistant.
2. **Siren Relay Output**: Driven `HIGH` during `TRIGGERED` state or when `isSirenTestActive` is true. Automatically returns to `LOW` on disarm.
3. **DSC Keypad Zero (`0`) Key Siren Toggle**: Pressing `0` on the physical DSC keypad (when `keypadPinBuffer` is empty) toggles siren relay output (GPIO 26) and keypad test mode on/off, playing 3 confirmation chirps on activation and 1 chirp on deactivation.
4. **Web UI Siren Test Button ("Probador N.O.")**: Momentary press-and-hold button in [HKInfo.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/components/HKInfo.svelte) to test siren relay output.
5. **NVS Persistence & Auto-Protect**: Remembers armed/disarmed state across hardware reboots. Auto-arms on inactivity if enabled.

---

## 📝 Agent Progress Log & Handoff History

### [2026-07-18] - Antigravity (Google DeepMind Coding Agent)
- **Features Added**:
  - Implemented Physical Siren Output Relay on **GPIO 26** (`sirenPin = 26`, Active HIGH).
  - Added momentary Siren Test button ("Probador N.O.") on Web UI dashboard.
  - Added physical DSC Keypad **Zero (`0`) key toggle** for manual siren relay testing.
  - Fixed `dsc.buzzer(0)` explicit shutoff on disarm and test release to prevent continuous keypad beeping.
  - Created [PROJECT_SUMMARY.md](file:///Users/defeee/alarma-homekey-arduino/PROJECT_SUMMARY.md) with system architecture, Claude instructions, and Arduino Uno RF decoder code.
  - Established [AGENTS.md](file:///Users/defeee/alarma-homekey-arduino/AGENTS.md) as the standard agent handoff protocol.
- **Verification**:
  - Web UI compiled cleanly with `bun run build`.
  - ESP32 firmware compiled with `idf.py build`.
  - Both `HomeKey-ESP32.bin` and `spiffs.bin` successfully uploaded via OTA.
  - All changes committed and pushed to `feature/mock-alarm-arduino`.

---

## 🔄 Agent Handoff Template (Copy for future entries)

```markdown
### [YYYY-MM-DD] - [Agent Name / Model]
- **Summary of Changes**:
  - ...
- **Modified Files**:
  - ...
- **Hardware / Pinout Changes**:
  - ...
- **Verification & Flash Results**:
  - ...
- **Next Steps / Known Issues**:
  - ...
```
