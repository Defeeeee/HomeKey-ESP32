#pragma once
#include <cstdint>
#include <string>

/*
  Persistent event log: a small fixed-size ring buffer of timestamped events that
  survives reboots (stored as a single NVS blob in the "eventlog" namespace, so it
  needs no partition-table change — the table is already full). It backs both the
  diagnostic health view (boot/reset reason, connectivity) and the alarm audit
  trail (arm/disarm/trigger). The store itself is "dumb": callers own the meaning
  of `type`/`arg`. See EventType below for the agreed-upon numbering (the Svelte UI
  hardcodes matching labels).
*/

namespace eventlog {

// Event type codes. MUST stay in sync with the labels in the Web UI
// (data/src/lib/components/Diagnostics.svelte). Append-only: never renumber.
enum class EventType : uint8_t {
  BOOT           = 0,  // arg = esp_reset_reason_t (POWERON/SW/PANIC/...)
  ARMED_AWAY     = 1,  // arg = Source
  ARMED_HOME     = 2,  // arg = Source
  ARMING         = 3,  // arg = Source
  DISARMED       = 4,  // arg = Source
  ENTRY_DELAY    = 5,  // arg = 0
  TRIGGERED      = 6,  // arg = zone (1-8, 0 = unknown)
  MQTT_LOST      = 7,  // arg = 0
  MQTT_UP        = 8,  // arg = 0
  WIFI_LOST      = 9,  // arg = 0
  WIFI_UP        = 10, // arg = 0
  SIREN_DISABLED = 11, // arg = Source
  SIREN_ENABLED  = 12, // arg = Source
  AUTO_BYPASS    = 13, // arg = zone (1-8) auto-bypassed at auto-arm because it was open
  BYPASS_RESTORE = 14, // arg = zone (1-8) auto-un-bypassed after it finally closed
  SIREN_CUTOFF   = 15, // arg = minutes after which the sounder was auto-silenced
  ZONE_LEFT_OPEN = 16, // arg = zone (1-8) still open past zoneOpenWarnMins
  ZONE_CHATTER   = 17, // arg = zone (1-8) opening far too often — likely a faulty sensor
};

// Who initiated a user action (for arm/disarm/siren events). Stored in `arg`.
enum class Source : uint8_t { SYSTEM = 0, KEYPAD = 1, REMOTE = 2, AUTO = 3 };

// Load the ring from NVS and append a BOOT event carrying esp_reset_reason().
// Safe to call once, early, after nvs_flash_init().
void begin();

// Append an event. `ts` is epoch seconds from time(nullptr); if the clock is not
// yet SNTP-synced it will be a small "seconds since boot"-ish value, which the UI
// renders as a relative time. Thread-safe; persists to NVS.
void add(EventType type, uint8_t arg = 0);

// Persist the RAM ring to NVS if it changed since the last flush. Call ONLY from
// the main loop task (large stack) — add() itself never writes NVS, so heavy work
// stays off the small-stack Wi-Fi-event and httpd tasks. Cheap no-op when clean.
void flush();

// Serialize the whole ring as a JSON array, newest entry first:
//   [{ "ts": <uint32>, "type": <uint8>, "arg": <uint8> }, ...]
std::string toJson();

// Wipe the ring (RAM + NVS). Used by an explicit "clear log" UI action.
void clear();

} // namespace eventlog
