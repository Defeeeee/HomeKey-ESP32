// Persistent event log received from the device (survives reboots).
// The numeric `type`/`arg` codes MUST match main/include/EventLog.hpp.

export interface RawEvent {
  ts: number;   // epoch seconds (UTC); if < 1_600_000_000 the clock wasn't NTP-synced yet
  type: number; // EventType
  arg: number;  // Source, zone, or reset reason depending on type
}

export const events = $state<RawEvent[]>([]);

export function setEvents(list: RawEvent[]) {
  events.length = 0;
  if (Array.isArray(list)) events.push(...list);
}

// --- EventType (keep in sync with EventLog.hpp) ---
export const EVENT = {
  BOOT: 0,
  ARMED_AWAY: 1,
  ARMED_HOME: 2,
  ARMING: 3,
  DISARMED: 4,
  ENTRY_DELAY: 5,
  TRIGGERED: 6,
  MQTT_LOST: 7,
  MQTT_UP: 8,
  WIFI_LOST: 9,
  WIFI_UP: 10,
  SIREN_DISABLED: 11,
  SIREN_ENABLED: 12,
  AUTO_BYPASS: 13,
  BYPASS_RESTORE: 14,
  SIREN_CUTOFF: 15,
  ZONE_LEFT_OPEN: 16,
  ZONE_CHATTER: 17,
} as const;

const SOURCE_LABEL = ["sistema", "teclado", "remoto", "auto"];
// esp_reset_reason_t ordering: POWERON=1, EXT=2, SW=3, PANIC=4, INT_WDT=5,
// TASK_WDT=6, WDT=7, DEEPSLEEP=8, BROWNOUT=9 (0 = UNKNOWN).
const RESET_REASON: Record<number, string> = {
  0: "?", 1: "power", 2: "ext", 3: "SW",
  4: "PANIC ⚠", 5: "INT_WDT", 6: "TASK_WDT", 7: "WDT",
  8: "sleep", 9: "brownout ⚡",
};

function src(arg: number): string {
  return SOURCE_LABEL[arg] ?? "sistema";
}

export interface FormattedEvent {
  icon: string;
  label: string;
  detail: string;
  tone: "normal" | "good" | "warn" | "bad";
  time: string;
}

export function formatEvent(e: RawEvent): FormattedEvent {
  // Timestamps below this threshold are "seconds since boot" (clock not yet synced).
  const synced = e.ts >= 1_600_000_000;
  const time = synced
    ? new Date(e.ts * 1000).toLocaleString()
    : `+${e.ts}s (sin hora)`;

  switch (e.type) {
    case EVENT.BOOT:
      return { icon: "↻", label: "Reinicio", detail: RESET_REASON[e.arg] ?? "desconocido",
               tone: e.arg === 4 || e.arg === 9 ? "bad" : "warn", time };
    case EVENT.ARMED_AWAY:
      return { icon: "🔒", label: "Armado AWAY", detail: src(e.arg), tone: "normal", time };
    case EVENT.ARMED_HOME:
      return { icon: "🏠", label: "Armado HOME", detail: src(e.arg), tone: "normal", time };
    case EVENT.ARMING:
      return { icon: "⏳", label: "Armando", detail: src(e.arg), tone: "normal", time };
    case EVENT.DISARMED:
      return { icon: "🔓", label: "Desarmado", detail: src(e.arg), tone: "good", time };
    case EVENT.ENTRY_DELAY:
      return { icon: "⏳", label: "Entrada", detail: "", tone: "warn", time };
    case EVENT.TRIGGERED:
      return { icon: "🚨", label: "ALARMA", detail: e.arg ? `zona ${e.arg}` : "", tone: "bad", time };
    case EVENT.MQTT_LOST:
      return { icon: "📡", label: "MQTT caído", detail: "", tone: "warn", time };
    case EVENT.MQTT_UP:
      return { icon: "📡", label: "MQTT OK", detail: "", tone: "good", time };
    case EVENT.WIFI_LOST:
      return { icon: "📶", label: "WiFi caído", detail: "", tone: "warn", time };
    case EVENT.WIFI_UP:
      return { icon: "📶", label: "WiFi OK", detail: "", tone: "good", time };
    case EVENT.SIREN_DISABLED:
      return { icon: "🔇", label: "Sirena OFF", detail: src(e.arg), tone: "warn", time };
    case EVENT.SIREN_ENABLED:
      return { icon: "🔊", label: "Sirena ON", detail: src(e.arg), tone: "good", time };
    case EVENT.AUTO_BYPASS:
      return { icon: "🚪", label: "Auto-bypass", detail: e.arg ? `zona ${e.arg}` : "", tone: "warn", time };
    case EVENT.BYPASS_RESTORE:
      return { icon: "✅", label: "Zona OK", detail: e.arg ? `zona ${e.arg}` : "", tone: "good", time };
    case EVENT.ZONE_LEFT_OPEN:
      return { icon: "🚪", label: "Zona abierta", detail: e.arg ? `zona ${e.arg}` : "", tone: "warn", time };
    case EVENT.ZONE_CHATTER:
      return { icon: "⚠️", label: "Zona inestable", detail: e.arg ? `zona ${e.arg}` : "", tone: "bad", time };
    case EVENT.SIREN_CUTOFF:
      return { icon: "🔇", label: "Sirena cortada", detail: e.arg ? `tras ${e.arg} min` : "", tone: "warn", time };
    default:
      return { icon: "•", label: `Evento ${e.type}`, detail: "", tone: "normal", time };
  }
}
