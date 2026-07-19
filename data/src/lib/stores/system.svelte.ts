export type SystemInfo = {
  deviceName: string,
  version: string,
  uptime: number,
  free_heap: number,
  wifi_ssid: string,
  wifi_rssi: number,
  eth_enabled: boolean,
  log_level: number,
  chip_model: number,
  nfc_connected: boolean,
  mqtt_connected: boolean,
  mqtt_error_code: number,
  mqtt_error_message?: string,
  alarm_state: string,
  alarm_zones: boolean[],
  alarm_bypassed: boolean[],
  alarm_disabled: boolean[],
  siren_active?: boolean,
  siren_testing?: boolean,
  siren_disabled?: boolean,
  mqtt_down_ms?: number,
  reset_reason?: string
};

export const systemInfo : SystemInfo = $state({
  deviceName: '',
  version: '',
  uptime: 0,
  free_heap: 0,
  wifi_ssid: '',
  wifi_rssi: 0,
  eth_enabled: false,
  log_level: 2,
  chip_model: 0,
  nfc_connected: false,
  mqtt_connected: false,
  mqtt_error_code: 0,
  alarm_state: 'disarmed',
  alarm_zones: [false, false, false, false, false, false, false, false],
  alarm_bypassed: [false, false, false, false, false, false, false, false],
  alarm_disabled: [false, false, false, false, false, false, false, false]
});

/**
 * Update system information from API response
 */
export function updateSystemInfo(newInfo : Partial<SystemInfo>) {
  Object.assign(systemInfo, newInfo);
}


let loadingState = $state(false);

export function setLoadingState(value: boolean) {
  loadingState = value;
}

export function getLoadingState() {
  return loadingState;
}
