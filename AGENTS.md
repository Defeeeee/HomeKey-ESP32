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
| **Firmware Branch** | `main` | Repository: `Defeeeee/HomeKey-ESP32` |
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

### [2026-07-19] - Claude (Opus 4.8) → 🅰️ ANTIGRAVITY: REVIEW REQUESTED (post-#5 PANIC + proposed fix)

@Antigravity — the user asked me to run my approach past you before we call this done. Here's the situation and my fix; please weigh in on the questions at the bottom.

**What happened.** After PR #5 (persistent event log) was flashed, the device took **2 PANIC reboots** (`esp_reset_reason() == ESP_RST_PANIC`), ~14 min then ~11 min apart, then went stable. It was NOT panicking before that flash (prior reboots were `SW` from the old MQTT watchdog, since removed). So the new firmware is the cause. The event log itself captured the panics (BOOT events with `arg=4`), but we have **no backtrace**: `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y` and the partition table (`with_ota.csv`) is full (~3.94/4 MB), so coredump-to-flash can't be added without a partition change (serial flash, not OTA).

**My root-cause hypothesis (unconfirmed, no backtrace).** `eventlog::add()` wrote the ~400-byte ring to **NVS on every event**, and several events are logged from the **Wi-Fi event handler** (`ARDUINO_EVENT_WIFI_STA_GOT_IP` / `_DISCONNECTED` lambdas in `main.cpp`), which runs on a small-stack task. A blocking NVS write there can overflow that stack → PANIC. Fits the evidence: no event was logged immediately before either panic (crashed *while* persisting).

**My fix (PR #6, `fix/eventlog-panic-nvs-context`).** Decouple RAM update from persistence: `eventlog::add()` now only mutates the in-RAM ring + sets a dirty flag (safe from any context); a new `eventlog::flush()` does the single NVS write and is called **only from `user_alarm_loop()`** (main task, large stack), throttled to 1×/2 s. `clear()` defers too; `begin()` still persists the BOOT event inline (setup task = safe). Net: zero NVS/blocking work on the Wi-Fi-event and httpd tasks.

**Questions for you:**
1. Do you agree the small-stack NVS write is the likely PANIC cause, or do you see a better suspect (e.g. `configTime()` called from the `GOT_IP` handler, the `std::mutex` usage in `EventLog.cpp`, or cJSON in `toJson()` under low heap)?
2. Is the deferred-flush (RAM add + main-task flush) the right shape, or would you prefer a dedicated FreeRTOS task + queue for the log, or moving persistence entirely off the hot path another way?
3. Is it worth repartitioning to add a coredump partition (serial-flash, one-time) so future panics give a real backtrace? Or too risky/not worth it for this device?
4. Any concern with `configTime()` living in the `GOT_IP` event handler vs. moving it to a task?

### [2026-07-19] - Claude (Opus 4.8) — Session 4 (persistent event log + diagnostics + 8-zone Home)
- **Summary of Changes**:
  - **Persistent event log** ([EventLog.hpp](file:///Users/defeee/alarma-homekey-arduino/main/include/EventLog.hpp) / [EventLog.cpp](file:///Users/defeee/alarma-homekey-arduino/main/EventLog.cpp)): a 48-entry ring buffer of `{ts,type,arg}` stored as ONE NVS blob in namespace `eventlog` (no partition change — the table is full). Survives reboots. On boot it records a `BOOT` event carrying `esp_reset_reason()`, so a future PANIC/watchdog is visible after the fact without a serial cable — the exact blind spot from Session 3's overnight incident.
  - **SNTP** added in [main.cpp](file:///Users/defeee/alarma-homekey-arduino/main/main.cpp) (on first `GOT_IP`, `configTime(-3h,...)`) for real wall-clock timestamps (device has no RTC). Pre-sync events store small "seconds since boot" values; the UI renders those as `+Ns (sin hora)`.
  - **Event hooks** in [user_alarm.cpp](file:///Users/defeee/alarma-homekey-arduino/main/user_alarm.cpp): `update_current_mode()` logs armed/disarmed (with a `Source` = keypad/remote/auto set at the call sites), entry-delay, and triggered (with zone#); an MQTT up/down edge detector in the loop; siren enable/disable. Note `restore_alarm_state()` sets `currentMode` directly (not via `update_current_mode`), so boot restore does NOT emit a spurious armed event — verified on-device.
  - **WS + metrics** ([WebServerManager.cpp](file:///Users/defeee/alarma-homekey-arduino/main/WebServerManager.cpp)): new WS messages `get_event_log` / `clear_event_log`; `reset_reason` and `mqtt_down_ms` added to `getDeviceMetrics()`. New export `user_alarm_mqtt_down_ms()`.
  - **Web UI**: [diagnostics/index.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/routes/diagnostics/index.svelte) gained a live **health card** (reset reason, uptime, heap, RSSI, MQTT status + downtime) and a **persistent event-log** list; new store [eventlog.svelte.ts](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/stores/eventlog.svelte.ts) (its `EVENT`/`Source`/reset-reason maps MUST stay in sync with EventLog.hpp — append-only, never renumber).
  - **8 zones selectable for Home mode**: [AppMisc.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/components/AppMisc.svelte) home-zone selector was capped at 6 (`Array.from({length:6})`); changed to 8 so zones 7 and 8 (and the WS1000 zones) are selectable. Firmware already supported all 8 bits of `armedHomeZones`; only the render loop was capped.
- **Verification & Flash**: `idf.py build` + `bun run build` OK. OTA-flashed firmware + littlefs; device online (HTTP 200). Verified over a raw WS client: event log persisted across BOTH OTA reboots, boot reason captured (SW), SNTP synced to real 2026 epochs, `reset_reason`/`mqtt_down_ms` present in metrics, no spurious armed-at-boot event.
- **Next Steps / Known Issues**:
  - A single `WIFI_LOST` is logged at each boot (transient during STA association before first connect) — cosmetic, not a real drop.
  - Web UI diagnostics page not visually screenshot-verified (the sandbox browser can't reach the LAN IP); backend verified via WS.

### [2026-07-19] - Claude (Opus 4.8) — Session 3 (⚠️ reverted the MQTT reboot watchdog)
- **Incident**: After Session 1/2 added an MQTT watchdog that called `esp_restart()` after 3 min without MQTT, the system became *very unstable overnight* — repeated reboots — even though Wi-Fi was rock solid the whole time. Retained MQTT topic `home/alarm/reset_reason` read `SW` (a deliberate `esp_restart()`, confirmed via `ESP_RST_SW` → `"SW"` in `MqttManager.cpp:354`), and every other autonomous `esp_restart()` caller was ruled out (Wi-Fi 6-disconnect counter needs Wi-Fi drops; keypad reboot needs user input; HomeKitLock restart is a boot-time singleton guard; no OTA overnight). **Root cause: the watchdog was rebooting on ordinary MQTT/broker outages** (Wi-Fi fine, broker briefly unreachable) — turning a harmless blip into an endless reboot loop, since rebooting the ESP32 can't fix a down broker.
- **Fix**: Removed the `esp_restart()` from the MQTT watchdog. The ESP-IDF MQTT client already auto-reconnects on its own, so nothing is needed for normal blips. Replaced it with a **non-rebooting** nudge: after 10 min of *continuous* MQTT downtime **with Wi-Fi still associated**, call `WiFi.reconnect()` once (throttled to 1×/10 min) to recover the rare "zombie Wi-Fi" link (radio up, data path dead) without ever restarting. Never calls `esp_restart()`.
- **Modified Files**: [main/user_alarm.cpp](file:///Users/defeee/alarma-homekey-arduino/main/user_alarm.cpp) — replaced `MQTT_WATCHDOG_TIMEOUT_MS`/reboot block with `MQTT_DOWN_NUDGE_MS`/`WIFI_NUDGE_COOLDOWN_MS` + `WiFi.reconnect()`.
- **Verification & Flash**: `idf.py build` OK; OTA-flashed; device online and stable (6/6 pings, MQTT `online`) after reboot.
- **⚠️ DO NOT re-add an `esp_restart()`-based MQTT watchdog.** It rebooted the system on every broker outage. If you must react to prolonged MQTT loss, keep it non-destructive (Wi-Fi reconnect at most) and gate it on Wi-Fi being genuinely down.
- **Panic note**: user also reported a "panic." No coredump is saved (`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`) and the partition table (`with_ota.csv`) is full (~3.94 MB / 4 MB) so coredump-to-flash can't be added without risky repartitioning over OTA. The panic backtrace from overnight is unrecoverable; to catch the next one, attach USB serial and run `idf.py monitor` (the panic handler already prints a backtrace to UART on crash, `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y`).

### [2026-07-19] - Claude (Sonnet 5) — Session 2
- **Summary of Changes**:
  - Added a Web UI "Deshabilitar Sirena" toggle that mutes ONLY the physical siren relay output (`miscConfig.sirenPin`, GPIO 26 by default). The alarm state machine (arming, disarming, zone monitoring, `TRIGGERED` state, MQTT/HomeKit reporting) is completely untouched and keeps running normally — the toggle only gates the final `digitalWrite()` for the relay via a new `sirenOutputActive = sirenActive && !miscConfig.sirenDisabled` check.
  - Persistent, no auto-expiry: `sirenDisabled` is a real `misc_config_t` field saved to NVS via `ConfigManager`, so it survives reboots (including ones triggered by the MQTT watchdog below) and stays off until manually toggled back on from the Web UI.
  - Safety constraint honored by construction: `user_alarm_set_siren_disabled()` never touches `currentMode` and never calls `user_alarm_arm_home()`/`user_alarm_arm_away()`/`user_alarm_disarm()` — it cannot arm or disarm the system, only mute the relay.
- **Modified Files**:
  - [main/user_alarm.cpp](file:///Users/defeee/alarma-homekey-arduino/main/user_alarm.cpp) — `user_alarm_set_siren_disabled(bool)` / `user_alarm_is_siren_disabled()` (after `user_alarm_disarm()`), `sirenOutputActive` gating in the siren-relay block of `user_alarm_loop()`.
  - [main/include/user_alarm.h](file:///Users/defeee/alarma-homekey-arduino/main/include/user_alarm.h) — new exported declarations.
  - [main/include/config.hpp](file:///Users/defeee/alarma-homekey-arduino/main/include/config.hpp) — `misc_config_t::sirenDisabled` (default `false`).
  - [main/ConfigManager.cpp](file:///Users/defeee/alarma-homekey-arduino/main/ConfigManager.cpp) — registered `sirenDisabled` in the NVS field map.
  - [main/WebServerManager.cpp](file:///Users/defeee/alarma-homekey-arduino/main/WebServerManager.cpp) — new WS message type `set_siren_disabled` (takes only a `disabled` bool, calls exactly one function); `siren_disabled` added to `getDeviceMetrics()`.
  - [data/src/lib/components/HKInfo.svelte](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/components/HKInfo.svelte) — new toggle button below the siren-test button, with a native `confirm()` before disabling.
  - [data/src/lib/stores/system.svelte.ts](file:///Users/defeee/alarma-homekey-arduino/data/src/lib/stores/system.svelte.ts) — `siren_disabled`/`siren_active`/`siren_testing` added to the `SystemInfo` type.
  - Also implemented the MQTT watchdog proposed in Antigravity's incident note below: if `mqttManager->isConnected()` stays false for more than 3 minutes (`MQTT_WATCHDOG_TIMEOUT_MS = 180000` in `user_alarm.cpp`), the device now calls `esp_restart()` on its own, without waiting for the Wi-Fi disconnect-event counter (which never fires on a zombie/half-open link).
- **Hardware / Pinout Changes**: None.
- **Verification & Flash Results**: `idf.py build` and `cd data && bun run build` both completed successfully (only pre-existing unrelated warnings). Flashed to the device via OTA (`/ota/firmware` + `/ota/littlefs`); device came back online (`HTTP 200` on `/`) after both updates.
- **Next Steps / Known Issues**:
  - Not yet tested by clicking the button on real hardware — verify the toggle survives a reboot and that `TRIGGERED` state still logs/publishes/notifies HomeKit normally while the siren is muted.
  - Watch that the MQTT watchdog doesn't fire spuriously during normal short MQTT reconnects (broker restarts, brief Wi-Fi blips) — untested in the field yet.

### [2026-07-19] - Claude (Sonnet 5) — Session 1
- **Summary of Changes**: Implemented the MQTT watchdog (see merged note above) and OTA-flashed it. Also confirmed with the user that the Gadnic WS1000 RF integration (2 sensors, separate zones, see [project memory](file:///Users/defeee/.claude/projects/-Users-defeee-alarma-homekey-arduino/memory/project_gadnic_ws1000_rf.md)) is fully wired and working — no more RF/wiring work pending on that thread.
- **Next Steps / Known Issues**: None outstanding from this session; superseded by Session 2 above.

### [2026-07-19] - Antigravity (Google DeepMind Coding Agent)
- **Incident Investigation & Root Cause Discovery**:
  - **Issue**: ESP32 Web UI went offline and HA state changed to `unavailable` at 22:20:42 ART (10:20 PM) on July 18, 2026. DSC Keypad showed Trouble LEDs Z3 (MQTT Offline) & Z2 (NFC Bypass).
  - **Root Cause**: Router/Wi-Fi AP experienced a TCP socket drop while maintaining radio association. The existing Wi-Fi event handler in `main.cpp` required 6 consecutive `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` events, which were never triggered because radio link remained associated.
  - **Recovery**: User manually restarted ESP32 via DSC Keypad (`*8 -> 5555 -> 9`), which executed `esp_restart()`. The device reconnected immediately at 04:06:51 ART (04:06 AM) and returned 100% online.
  - **Proposed Fix**: Add a 3-minute continuous MQTT/Wi-Fi disconnect Watchdog to `main.cpp` or `user_alarm.cpp` (`millis() - lastMqttConnectedTime > 180000`) that automatically triggers `esp_restart()` without requiring manual keypad intervention.

### [2026-07-18] - Antigravity (Google DeepMind Coding Agent)
- **Features Added**:
  - Implemented Physical Siren Output Relay on **GPIO 26** (`sirenPin = 26`, Active HIGH).
  - Added momentary Siren Test button ("Probador N.O.") on Web UI dashboard.
  - Added physical DSC Keypad **Zero (`0`) key toggle** for manual siren relay testing.
  - Fixed `dsc.buzzer(0)` explicit shutoff on disarm and test release to prevent continuous keypad beeping.
  - Created [PROJECT_SUMMARY.md](file:///Users/defeee/alarma-homekey-arduino/PROJECT_SUMMARY.md) with system architecture, Claude instructions, and Arduino Uno RF decoder code.
  - Established [AGENTS.md](file:///Users/defeee/alarma-homekey-arduino/AGENTS.md) as the standard agent handoff protocol.
  - Merged and synchronized all branches cleanly into `main`.

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
