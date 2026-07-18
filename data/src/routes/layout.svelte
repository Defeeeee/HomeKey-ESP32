<script lang="ts">
	import '@/app.css';
	import { onMount, onDestroy } from 'svelte';
	import { getLoadingState, systemInfo, updateSystemInfo } from '$lib/stores/system.svelte.js';
	import { initTheme, logoSrc } from '$lib/stores/theme.svelte.js';
	import ws, { type WebSocketEvent } from '$lib/services/ws.js';
	import Logo from '$lib/assets/favicon.png';
	import NavigationMenu from '$lib/components/NavigationMenu.svelte';
	import Notification from '$lib/components/Notification.svelte';
  import { websocketState } from '$lib/stores/websocket.svelte';
  import type { LogEntry } from '$lib/types/api';
  import { logIdIncrement, logs } from '$lib/stores/logs.svelte';
  import { processZoneStateChange } from '$lib/stores/diagnostics.svelte.js';
  import { route } from 'sv-router/generated';

	let { children } = $props();
  let drawerOpen = $state(false);
	let unsubscribeMessages : (() => void) | null = null;

  const isCaptivePortal = $derived(route.pathname.startsWith('/captive-portal'));
	let alarmState = $derived(systemInfo?.alarm_state || 'disarmed');

	function getGlowColor1(state: string) {
		switch (state) {
			case 'disarmed': return 'bg-[#10b981]';
			case 'pending':
			case 'arming_away':
			case 'arming_home':
			case 'armed_home': return 'bg-[#f59e0b]';
			case 'armed_away':
			case 'triggered': return 'bg-[#ef4444]';
			default: return 'bg-primary';
		}
	}

	function getGlowColor2(state: string) {
		switch (state) {
			case 'disarmed': return 'bg-[#8b5cf6]';
			case 'pending':
			case 'arming_away':
			case 'arming_home':
			case 'armed_home': return 'bg-[#ec4899]';
			case 'armed_away':
			case 'triggered': return 'bg-[#be123c]';
			default: return 'bg-secondary';
		}
	}

	onMount(() => {
    initTheme();

    if (isCaptivePortal) return;

		if (ws) {
			ws.connect();

			unsubscribeMessages = ws.on((event : WebSocketEvent<any>) => {
				if (event.type === 'message') {
					const data = event.data;
					if (data.type === 'sysinfo' || data.type === 'metrics') {
						updateSystemInfo(data);
					}
					if (data.alarm_zones) {
						processZoneStateChange(data.alarm_zones);
					}
          if (data.type === 'log') {
						const log : LogEntry = {
							id: Date.now() + logIdIncrement(),
							localts: new Date().toISOString(),
							expanded: false,
							...data
						};

						if (log.msg && log.level) {
							logs.push(log);
						} else {
							console.warn('Invalid log message structure:', data);
						}
          }
				}
      });
		}
	});

	onDestroy(() => {
		if (unsubscribeMessages) {
			unsubscribeMessages();
		}
	});
</script>

<svelte:head>
	<link rel="icon" href={logoSrc()} type="image/svg+xml" />
	<link rel="apple-touch-icon" href={Logo} />
	<title>HomeKey-ESP32</title>
</svelte:head>

{#if isCaptivePortal}
  <!-- Captive Portal: Minimal layout without navbar/sidebar -->
  {@render children()}
{:else}
  <!-- Normal App Layout -->
  <div class="flex flex-col h-dvh bg-[#07060f] relative overflow-hidden text-slate-100">
    <!-- Ambient Background Glows -->
    <div class="absolute inset-0 overflow-hidden pointer-events-none z-0">
      <div class="absolute top-[-10%] left-[-10%] w-[40rem] h-[40rem] rounded-full blur-[130px] opacity-20 transition-all duration-1000 ease-in-out {getGlowColor1(alarmState)}"></div>
      <div class="absolute bottom-[-15%] right-[-10%] w-[45rem] h-[45rem] rounded-full blur-[150px] opacity-15 transition-all duration-1000 ease-in-out {getGlowColor2(alarmState)}"></div>
    </div>

    <!-- Mobile Navbar -->
    <div class="navbar bg-[#0e0e15]/75 border-b border-white/5 backdrop-blur-md lg:hidden sticky top-0 z-9999 relative z-10">
      <div class="navbar-start w-full">
        <label for="main-content-drawer" class="btn btn-ghost drawer-button lg:hidden" aria-label="Menu">
          <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5" fill="none" viewBox="0 0 24 24"
            stroke="currentColor">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 6h16M4 12h8m-8 6h16" />
          </svg>
        </label>
        <div class="flex justify-between w-full">
          <span class="font-bold text-lg">
            {#if !systemInfo.deviceName}
              <div class="skeleton h-8 w-32 bg-white/5"></div>
            {:else}
              {systemInfo.deviceName}
            {/if}
          </span>
          <div class="gap-1 pr-2 flex items-center">
            <div class="inline-grid *:[grid-area:1/1]">
              <div class="status animate-ping" class:status-success={websocketState.connected} class:status-error={!websocketState.connected} class:status-warning={websocketState.state == "reconnecting"}></div>
              <div class="status" class:status-success={websocketState.connected} class:status-error={!websocketState.connected} class:status-warning={websocketState.state == "reconnecting"}></div>
            </div>
            <span class="text-sm" class:text-success={websocketState.connected} class:text-error={!websocketState.connected} class:text-warning={websocketState.state == "reconnecting"}>{websocketState.state == "open" ? 'Online' : websocketState.state == "reconnecting" ? "Reconnecting" : 'Offline'}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Drawer -->
    <div class="drawer lg:drawer-open flex w-full flex-1 overflow-hidden relative z-10">
      <input id="main-content-drawer" type="checkbox" class="drawer-toggle" bind:checked={drawerOpen} />
      <!-- Sidebar -->
      <div class="drawer-side max-lg:fixed max-lg:top-16 max-lg:bottom-0 max-lg:left-0 max-lg:z-50 max-lg:h-[calc(100dvh-4rem)] h-screen flex flex-col overflow-hidden">
        <label for="main-content-drawer" aria-label="close sidebar" class="drawer-overlay w-full h-full absolute"></label>
        <NavigationMenu onClose={() => drawerOpen = false} id="main-navigation" />
      </div>
      <div class="drawer-content h-full w-full">
        <!-- Content -->
        <main id="main-content" class="px-6 h-full overflow-y-auto bg-transparent">
        {#if getLoadingState()}
          <div class="flex items-center justify-center h-full">
            <div class="loading loading-spinner loading-lg text-primary"></div>
          </div>
        {:else}
          {@render children()}
        {/if}
        </main>
      </div>

    </div>
  </div>
  <!-- Global Notifications -->
  <Notification />
{/if}
