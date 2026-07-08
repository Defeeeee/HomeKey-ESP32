export interface ZoneActivity {
  time: string;
  zone: number;
  state: "OPEN" | "CLOSED";
}

export interface ZoneStats {
  triggerCount: number;
  lastTriggered?: string;
}

export const zoneHistory = $state<ZoneActivity[]>([]);
export const zoneStats = $state<ZoneStats[]>(
  Array.from({ length: 8 }, () => ({ triggerCount: 0 }))
);

let lastZonesState: boolean[] = [false, false, false, false, false, false, false, false];
let initialized = false;

export function processZoneStateChange(currentZones: boolean[]) {
  if (!currentZones || currentZones.length !== 8) return;

  if (!initialized) {
    lastZonesState = [...currentZones];
    initialized = true;
    return;
  }

  for (let i = 0; i < 8; i++) {
    if (currentZones[i] !== lastZonesState[i]) {
      const state = currentZones[i] ? "OPEN" : "CLOSED";
      const timeStr = new Date().toLocaleTimeString();
      const activity: ZoneActivity = {
        time: timeStr,
        zone: i + 1,
        state
      };
      
      zoneHistory.unshift(activity);
      if (zoneHistory.length > 20) {
        zoneHistory.pop();
      }

      if (currentZones[i]) {
        zoneStats[i].triggerCount += 1;
        zoneStats[i].lastTriggered = timeStr;
      }
      
      lastZonesState[i] = currentZones[i];
    }
  }
}
