<script lang="ts">
  import type { HKInfo } from "$lib/types/api";
  import { systemInfo } from "$lib/stores/system.svelte.js";
  import { calculateWifiSignal } from "$lib/utils/wifi.js";
  import ws from '$lib/services/ws.js';
  const version: string = __DEV__ ? "dev" : __VERSION__;

  let { hkInfo, error }: { hkInfo: HKInfo | null; error: string | null } =
    $props();

  let wifi_rssi = $derived(systemInfo?.wifi_rssi);
  let wifi_signal = $derived.by(() => calculateWifiSignal(wifi_rssi));

  let alarm_state = $derived(systemInfo?.alarm_state || 'disarmed');
  let alarm_zones = $derived(systemInfo?.alarm_zones || [false, false, false, false, false, false, false, false]);

  const setAlarmState = (state: string) => {
    if (ws && ws.connected) {
      ws.send({ type: 'set_alarm_state', data: state });
    }
  };

  const getAlarmStateStyles = (state: string) => {
    switch (state) {
      case 'disarmed':
        return {
          bg: 'bg-success/10 border-success/30',
          badge: 'badge-success',
          text: 'text-success',
          iconColor: 'text-success',
          label: 'DISARMED',
          desc: 'System is disarmed. Home is open.'
        };
      case 'arming_away':
        return {
          bg: 'bg-warning/10 border-warning/30 animate-pulse',
          badge: 'badge-warning',
          text: 'text-warning',
          iconColor: 'text-warning',
          label: 'ARMING AWAY',
          desc: 'Exit delay active. Leaving premises...'
        };
      case 'arming_home':
        return {
          bg: 'bg-warning/10 border-warning/30 animate-pulse',
          badge: 'badge-warning',
          text: 'text-warning',
          iconColor: 'text-warning',
          label: 'ARMING HOME',
          desc: 'Exit delay active. Arming home zones...'
        };
      case 'armed_away':
        return {
          bg: 'bg-primary/10 border-primary/30',
          badge: 'badge-primary',
          text: 'text-primary',
          iconColor: 'text-primary',
          label: 'ARMED AWAY',
          desc: 'All security zones are armed.'
        };
      case 'armed_home':
        return {
          bg: 'bg-info/10 border-info/30',
          badge: 'badge-info',
          text: 'text-info',
          iconColor: 'text-info',
          label: 'ARMED HOME',
          desc: 'Selected perimeter zones are armed.'
        };
      case 'pending':
        return {
          bg: 'bg-warning/20 border-warning/50 animate-pulse',
          badge: 'badge-warning',
          text: 'text-warning font-bold',
          iconColor: 'text-warning',
          label: 'ENTRY DELAY',
          desc: 'Entry door opened! Tap Apple HomeKey or Disarm now.'
        };
      case 'triggered':
        return {
          bg: 'bg-error/20 border-error/50 animate-bounce',
          badge: 'badge-error animate-ping',
          text: 'text-error font-extrabold',
          iconColor: 'text-error',
          label: 'ALARM TRIGGERED',
          desc: 'INTRUSION DETECTADA! Siren is screaming.'
        };
      default:
        return {
          bg: 'bg-base-300 border-base-content/20',
          badge: 'badge-ghost',
          text: 'text-base-content/60',
          iconColor: 'text-base-content/60',
          label: 'UNKNOWN',
          desc: 'Unknown system state.'
        };
    }
  };

  let stateStyles = $derived(getAlarmStateStyles(alarm_state));
</script>

<div class="w-full py-6">
  <div class="mb-6">
    <h1 class="text-2xl font-bold">Security Control Center</h1>
    <p class="text-base-content/60">
      Real-time status monitoring, zone breaches, and alarm keypad.
    </p>
  </div>

  <div class="grid grid-cols-1 lg:grid-cols-3 gap-6 max-w-6xl items-start mb-6">
    <!-- COLUMN 1 & 2: Alarm Control Panel -->
    <div class="lg:col-span-2 card bg-base-200 shadow-xl border {stateStyles.bg} transition-all duration-300">
      <div class="card-body p-6">
        <div class="flex flex-col sm:flex-row justify-between items-start sm:items-center gap-4 mb-6">
          <div class="flex items-center gap-4">
            <div class="w-14 h-14 rounded-2xl bg-base-100 flex items-center justify-center shadow-md">
              {#if alarm_state === 'disarmed'}
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-8 {stateStyles.iconColor}">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75 11.25 15 15 9.75m-3-7.036A11.959 11.959 0 0 1 3.598 6 11.99 11.99 0 0 0 3 9.749c0 5.592 3.824 10.29 9 11.623 5.176-1.332 9-6.03 9-11.622 0-1.31-.21-2.57-.599-3.75A11.952 11.952 0 0 1 12 2.714Z" />
                </svg>
              {:else if alarm_state === 'triggered'}
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-8 {stateStyles.iconColor} animate-pulse">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M14.857 17.082a9.04 9.04 0 0 1-5.714 0m5.714 0a3 3 0 1 1-5.714 0M3.124 7.5A8.969 8.969 0 0 1 5.292 3m13.416 0a8.969 8.969 0 0 1 2.168 4.5M19.186 10.122A3 3 0 0 0 21 8.25c0-2.428-2.017-4.4-4.5-4.4-.99 0-1.89.327-2.617.882M12 12v.01M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm0 0v.01M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm0 0v.01M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z" />
                </svg>
              {:else}
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-8 {stateStyles.iconColor}">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M16.5 10.5V6.75a4.5 4.5 0 1 0-9 0v3.75m-.75 11.25h10.5a2.25 2.25 0 0 0 2.25-2.25v-6.75a2.25 2.25 0 0 0-2.25-2.25H6.75a2.25 2.25 0 0 0-2.25 2.25v6.75a2.25 2.25 0 0 0 2.25 2.25Z" />
                </svg>
              {/if}
            </div>
            <div>
              <div class="flex items-center gap-2">
                <span class="text-xl font-bold uppercase tracking-wider {stateStyles.text}">{stateStyles.label}</span>
                <span class="badge {stateStyles.badge} badge-sm font-semibold uppercase">{alarm_state}</span>
              </div>
              <p class="text-sm text-base-content/85 mt-0.5">{stateStyles.desc}</p>
            </div>
          </div>
        </div>

        <div class="divider my-2 opacity-30"></div>

        <!-- Alarm Control Keypad Actions -->
        <div>
          <h3 class="text-sm font-bold text-base-content/80 mb-3 uppercase tracking-wider">Control Panel</h3>
          <div class="grid grid-cols-1 sm:grid-cols-3 gap-3">
            <button 
              onclick={() => setAlarmState('DISARMED')} 
              class="btn btn-lg bg-success hover:bg-success-focus text-success-content border-none flex flex-col items-center justify-center p-3 gap-1 shadow-md hover:scale-102 transition-transform"
            >
              <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-6">
                <path stroke-linecap="round" stroke-linejoin="round" d="M13.5 10.5V6.75a4.5 4.5 0 1 1 9 0v3.75M3.75 21.75h16.5a2.25 2.25 0 0 0 2.25-2.25v-6.75a2.25 2.25 0 0 0-2.25-2.25H3.75a2.25 2.25 0 0 0-2.25 2.25v6.75a2.25 2.25 0 0 0 2.25 2.25Z" />
              </svg>
              <span class="text-sm font-bold uppercase tracking-wide">Disarm</span>
            </button>

            <button 
              onclick={() => setAlarmState('ARMED_AWAY')} 
              class="btn btn-lg bg-primary hover:bg-primary-focus text-primary-content border-none flex flex-col items-center justify-center p-3 gap-1 shadow-md hover:scale-102 transition-transform"
            >
              <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-6">
                <path stroke-linecap="round" stroke-linejoin="round" d="M16.5 10.5V6.75a4.5 4.5 0 1 0-9 0v3.75m-.75 11.25h10.5a2.25 2.25 0 0 0 2.25-2.25v-6.75a2.25 2.25 0 0 0-2.25-2.25H6.75a2.25 2.25 0 0 0-2.25 2.25v6.75a2.25 2.25 0 0 0 2.25 2.25Z" />
              </svg>
              <span class="text-sm font-bold uppercase tracking-wide">Arm Away</span>
            </button>

            <button 
              onclick={() => setAlarmState('ARMED_HOME')} 
              class="btn btn-lg bg-info hover:bg-info-focus text-info-content border-none flex flex-col items-center justify-center p-3 gap-1 shadow-md hover:scale-102 transition-transform"
            >
              <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-6">
                <path stroke-linecap="round" stroke-linejoin="round" d="m2.25 12 8.954-8.955c.44-.439 1.152-.439 1.591 0L21.75 12M4.5 9.75v10.125c0 .621.504 1.125 1.125 1.125H9.75v-4.875c0-.621.504-1.125 1.125-1.125h2.25c.621 0 1.125.504 1.125 1.125V21h4.125c.621 0 1.125-.504 1.125-1.125V9.75M8.25 21h8.25" />
              </svg>
              <span class="text-sm font-bold uppercase tracking-wide">Arm Home</span>
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- COLUMN 3: Live Zone Status -->
    <div class="card bg-base-200 shadow-xl border border-base-content/10 h-full">
      <div class="card-body p-5">
        <h2 class="card-title text-base uppercase tracking-wider text-base-content/85 flex justify-between items-center">
          Security Zones
          <span class="badge badge-sm font-bold bg-base-300">Live Status</span>
        </h2>
        
        <div class="space-y-2 mt-2">
          {#each Array.from({ length: 6 }) as _, i}
            <div class="flex items-center justify-between p-3 rounded-xl border transition-all duration-300 {alarm_zones[i] ? 'bg-error/10 border-error/30' : 'bg-base-100 border-base-300'}">
              <div class="flex items-center gap-3">
                <div class="w-8 h-8 rounded-lg flex items-center justify-center font-bold text-sm {alarm_zones[i] ? 'bg-error text-error-content animate-pulse' : 'bg-success text-success-content'}">
                  Z{i + 1}
                </div>
                <div>
                  <span class="text-sm font-semibold text-base-content/90">Zone {i + 1}</span>
                  <span class="text-[10px] text-base-content/65 block">{i === 0 ? 'Delay (Front Door)' : 'Instant Sensor'}</span>
                </div>
              </div>
              <div class="flex items-center gap-2">
                {#if alarm_zones[i]}
                  <span class="text-xs font-bold text-error uppercase tracking-wider flex items-center gap-1">
                    <span class="w-2 h-2 rounded-full bg-error animate-ping"></span>
                    Open
                  </span>
                {:else}
                  <span class="text-xs font-semibold text-success uppercase tracking-wider">Secured</span>
                {/if}
              </div>
            </div>
          {/each}
        </div>
      </div>
    </div>
  </div>

  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6 max-w-6xl items-start">
    <!-- HomeKey Card -->
    <div class="card bg-base-200 shadow-xl border border-base-content/10">
      <div class="card-body p-5">
        <div class="flex items-center gap-3 mb-4">
          <div class="w-10 h-10 rounded-lg bg-primary/20 flex items-center justify-center">
            <svg
              xmlns="http://www.w3.org/2000/svg"
              class="size-6 text-primary"
              fill="none"
              viewBox="0 0 24 24"
              stroke="currentColor"
            >
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M15 7a2 2 0 012 2m4 0a6 6 0 01-7.743 5.743L11 17H9v2H7v2H4a1 1 0 01-1-1v-2.586a1 1 0 01.293-.707l5.964-5.964A6 6 0 1121 9z"
              />
            </svg>
          </div>
          <div>
            <h2 class="card-title text-base font-bold">HomeKey Details</h2>
            <p class="text-[10px] text-base-content/60">Registered Devices & GID</p>
          </div>
        </div>

        <div class="space-y-2">
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Reader GID</span>
            <span class="text-sm font-medium font-mono">{hkInfo?.group_identifier || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Reader ID</span>
            <span class="text-sm font-medium font-mono">{hkInfo?.unique_identifier || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Registered Issuers</span>
            <span class="text-sm font-medium">{hkInfo?.issuers?.length || 0}</span>
          </div>
        </div>

        <!-- Issuers Section inside HomeKey Card -->
        {#if hkInfo?.issuers && hkInfo.issuers.length > 0}
          <div class="mt-4 space-y-1">
            {#each hkInfo.issuers as issuer, index (`issuer-${index}`)}
              <div class="collapse collapse-arrow bg-base-100 rounded-lg border border-base-300">
                <input type="checkbox" name="info-collapse-{index}" />
                <div class="collapse-title font-semibold py-3 flex items-center gap-2">
                  <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5 text-base-content/60" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
                  </svg>
                  <span>Issuer {index + 1}</span>
                </div>
                <div class="collapse-content text-sm">
                  <div class="py-2 px-3 bg-base-200 rounded-lg mb-2 border border-base-300">
                    <span class="text-xs text-base-content/60 block mb-1">Issuer ID</span>
                    <span class="text-sm font-mono break-all">{issuer.issuerId || "N/A"}</span>
                  </div>
                  {#if issuer.endpoints && issuer.endpoints.length > 0}
                    <div class="mt-2">
                      <span class="text-xs text-base-content/60 block mb-2">Registered Devices (Endpoints)</span>
                      <ul class="space-y-2">
                        {#each issuer.endpoints as endpoint, epIndex (`ep-${epIndex}`)}
                          <li class="flex items-center gap-2 py-2 px-3 bg-base-200 rounded-lg border border-base-300">
                            <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" width="24" height="24" fill="currentColor" class="size-4 text-primary">
                              <path d="M12 1C16.9706 1 21 5.02944 21 10V14C21 18.9706 16.9706 23 12 23C10.9137 23 9.8724 22.8076 8.90826 22.4549C9.03638 22.2782 9.15938 22.0977 9.27703 21.9134L9.44782 21.633C10.388 20.0636 10.9461 18.2391 10.9963 16.2884L11 16V9H13V16C13 17.7724 12.6453 19.4619 12.0031 21.0015C12.7954 21 13.5599 20.8673 14.2724 20.6229C14.7147 19.2616 14.966 17.8148 14.9968 16.3138L15 16L14.9998 12.999H16.9998L17 16C17 17.0885 16.8977 18.1531 16.7022 19.1847C18.0583 17.9552 18.9297 16.2 18.9959 14.2407L19 14V10C19 6.13401 15.866 3 12 3C10.4277 3 8.97638 3.51841 7.8078 4.39364L6.38282 2.96769C7.92242 1.73631 9.87522 1 12 1ZM7 10C7 7.23858 9.23858 5 12 5C14.7614 5 17 7.23858 17 10V11H15V10C15 8.34315 13.6569 7 12 7C10.4023 7 9.09634 8.24892 9.00509 9.82373L9 10V16C9 17.5669 8.5996 19.0402 7.89554 20.3233L7.87214 20.3627C7.64284 20.7771 7.38087 21.1711 7.09037 21.5417C6.6495 21.2545 6.23541 20.9297 5.85264 20.5719L5.5445 20.2711C3.96956 18.65 3 16.4382 3 14V10C3 7.87522 3.73631 5.92242 4.96769 4.38282L6.39364 5.8078C5.56325 6.91652 5.05405 8.27971 5.00406 9.75935L5 10V14C5 15.6748 5.58816 17.2122 6.56918 18.4169C6.82239 17.7351 6.97017 17.0034 6.99594 16.2407L7 16V10Z">
                            </svg>
                            <span class="text-sm font-mono">{endpoint.endpointId || "N/A"}</span>
                          </li>
                        {/each}
                      </ul>
                    </div>
                  {/if}
                </div>
              </div>
            {/each}
          </div>
        {/if}
      </div>
    </div>

    <!-- System Card -->
    <div class="card bg-base-200 shadow-xl border border-base-content/10">
      <div class="card-body p-5">
        <div class="flex items-center gap-3 mb-4">
          <div class="w-10 h-10 rounded-lg bg-info/20 flex items-center justify-center">
            <svg
              viewBox="0 0 24 24"
              xmlns="http://www.w3.org/2000/svg"
              width="24"
              height="24"
              fill="currentColor"
              class="size-6 text-info"
            >
              <path
                d="M6 18H18V6H6V18ZM14 20H10V22H8V20H5C4.44772 20 4 19.5523 4 19V16H2V14H4V10H2V8H4V5C4 4.44772 4.44772 4 5 4H8V2H10V4H14V2H16V4H19C19.5523 4 20 4.44772 20 5V8H22V10H20V14H22V16H20V19C20 19.5523 19.5523 20 19 20H16V22H14V20ZM8 8H16V16H8V8Z"
              >
              </path>
            </svg>
          </div>
          <div>
            <h2 class="card-title text-base font-bold">System Status</h2>
            <p class="text-[10px] text-base-content/60">Firmware & Hardware Metrics</p>
          </div>
        </div>

        <div class="space-y-2">
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Version</span>
            <span class="text-sm font-medium">{systemInfo?.version || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">UI Version</span>
            <span class="text-sm font-medium">{version}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Device Name</span>
            <span class="text-sm font-medium">{systemInfo?.deviceName || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Uptime</span>
            <span class="text-sm font-medium">{systemInfo?.uptime || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Free Heap</span>
            <span class="text-sm font-medium">{systemInfo?.free_heap || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">WiFi SSID</span>
            <span class="text-sm font-medium">{systemInfo?.wifi_ssid || "N/A"}</span>
          </div>
          {#if !systemInfo?.eth_enabled}
            <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
              <span class="text-sm text-base-content/70">WiFi RSSI</span>
              <span class="text-sm font-medium">{systemInfo?.wifi_rssi || "N/A"} ({wifi_signal})</span>
            </div>
          {/if}
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">Ethernet enabled</span>
            <span class="text-sm font-medium">{systemInfo?.eth_enabled ? "Yes" : "No"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">NFC Module</span>
            <span class="text-sm font-medium">{systemInfo?.nfc_connected ? "Connected" : "Disconnected"}</span>
          </div>
          <div class="flex items-center justify-between py-2 px-3 bg-base-100 rounded-lg border border-base-300">
            <span class="text-sm text-base-content/70">MQTT broker</span>
            <span class="text-sm font-medium">{systemInfo?.mqtt_connected ? "Connected" : "Disconnected"}</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</div>
