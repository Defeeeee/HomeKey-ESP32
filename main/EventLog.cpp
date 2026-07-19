#include "include/EventLog.hpp"

#include <cstring>
#include <ctime>
#include <mutex>

#include "cJSON.h"
#include <esp_system.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace eventlog {

namespace {

constexpr const char* TAG = "EventLog";
constexpr const char* NVS_NAMESPACE = "eventlog";
constexpr const char* NVS_KEY = "ring";
constexpr uint16_t RING_CAPACITY = 48;
constexpr uint16_t RING_MAGIC = 0xE711;

struct Entry {
  uint32_t ts;
  uint8_t type;
  uint8_t arg;
};

// Persisted verbatim as a single NVS blob.
struct Ring {
  uint16_t magic;
  uint16_t head;   // index where the next entry will be written
  uint16_t count;  // number of valid entries (<= RING_CAPACITY)
  uint16_t _pad;
  Entry entries[RING_CAPACITY];
};

std::mutex g_mutex;
Ring g_ring{};
bool g_ready = false;
bool g_dirty = false; // RAM ring has unsaved changes; flushed from the main task

void persistLocked() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open for persist failed: %s", esp_err_to_name(err));
    return;
  }
  err = nvs_set_blob(handle, NVS_KEY, &g_ring, sizeof(g_ring));
  if (err == ESP_OK) {
    nvs_commit(handle);
  } else {
    ESP_LOGW(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
  }
  nvs_close(handle);
}

// RAM-only: never touches NVS, so it is safe to call from any context (main
// loop, Wi-Fi event task, httpd task). Persistence is deferred to flush(), which
// runs from the main task where the stack is large. Doing NVS work here — e.g.
// from the small-stack Wi-Fi event handler — was the suspected cause of PANIC
// reboots, so keep this cheap and non-blocking.
void addLocked(uint8_t type, uint8_t arg) {
  Entry& slot = g_ring.entries[g_ring.head];
  slot.ts = static_cast<uint32_t>(time(nullptr));
  slot.type = type;
  slot.arg = arg;
  g_ring.head = (g_ring.head + 1) % RING_CAPACITY;
  if (g_ring.count < RING_CAPACITY) g_ring.count++;
  g_dirty = true;
}

}  // namespace

void begin() {
  std::lock_guard<std::mutex> lock(g_mutex);

  bool loaded = false;
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    size_t size = sizeof(g_ring);
    if (nvs_get_blob(handle, NVS_KEY, &g_ring, &size) == ESP_OK &&
        size == sizeof(g_ring) && g_ring.magic == RING_MAGIC) {
      loaded = true;
    }
    nvs_close(handle);
  }

  if (!loaded) {
    // First boot / corrupt / schema change: start clean.
    std::memset(&g_ring, 0, sizeof(g_ring));
    g_ring.magic = RING_MAGIC;
  }
  g_ready = true;

  // Record why we (re)booted so a crash/watchdog is visible after the fact.
  // begin() runs in the app-main/setup task (ample stack), so persisting the
  // BOOT event to NVS right here is safe and guarantees it survives immediately.
  addLocked(static_cast<uint8_t>(EventType::BOOT),
            static_cast<uint8_t>(esp_reset_reason()));
  persistLocked();
  g_dirty = false;
}

void add(EventType type, uint8_t arg) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready) return;
  addLocked(static_cast<uint8_t>(type), arg);
}

// Persist the RAM ring to NVS if it changed. MUST be called only from the main
// task (large stack) — this is where the one NVS write per batch of events
// happens, keeping heavy/blocking work off the Wi-Fi-event and httpd tasks.
void flush() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_dirty) return;
  persistLocked();
  g_dirty = false;
}

std::string toJson() {
  std::lock_guard<std::mutex> lock(g_mutex);
  cJSON* arr = cJSON_CreateArray();
  // Walk newest -> oldest.
  for (uint16_t i = 0; i < g_ring.count; i++) {
    uint16_t idx = (g_ring.head + RING_CAPACITY - 1 - i) % RING_CAPACITY;
    const Entry& e = g_ring.entries[idx];
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "ts", e.ts);
    cJSON_AddNumberToObject(obj, "type", e.type);
    cJSON_AddNumberToObject(obj, "arg", e.arg);
    cJSON_AddItemToArray(arr, obj);
  }
  char* raw = cJSON_PrintUnformatted(arr);
  std::string out = raw ? raw : "[]";
  if (raw) cJSON_free(raw);
  cJSON_Delete(arr);
  return out;
}

void clear() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::memset(&g_ring, 0, sizeof(g_ring));
  g_ring.magic = RING_MAGIC;
  g_dirty = true; // deferred to flush() from the main task (no NVS in caller context)
}

}  // namespace eventlog
