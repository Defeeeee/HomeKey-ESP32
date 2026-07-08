<script lang="ts">
  import type { HKInfo } from "$lib/types/api";
  import { systemInfo, updateSystemInfo } from "$lib/stores/system.svelte.js";
  import { calculateWifiSignal } from "$lib/utils/wifi.js";
  import ws from '$lib/services/ws.js';
  import { onMount, onDestroy, untrack } from "svelte";
  import { logs, logIdIncrement } from "$lib/stores/logs.svelte";
  const version: string = __DEV__ ? "dev" : __VERSION__;

  let { hkInfo, error }: { hkInfo: HKInfo | null; error: string | null } = $props();

  let wifi_rssi = $derived(systemInfo?.wifi_rssi);
  let wifi_signal = $derived.by(() => calculateWifiSignal(wifi_rssi));

  let alarm_state = $derived(systemInfo?.alarm_state || 'disarmed');
  let alarm_zones = $derived(systemInfo?.alarm_zones || [false, false, false, false, false, false, false, false]);
  let alarm_bypassed = $derived(systemInfo?.alarm_bypassed || [false, false, false, false, false, false, false, false]);
  let alarm_disabled = $derived(systemInfo?.alarm_disabled || [false, false, false, false, false, false, false, false]);

  let isSimMode = $state(false);
  let displayError = $derived(isSimMode ? null : error);

  function addSimLog(msg: string, level = 3, tag = 'SIMULATOR') {
    logs.unshift({
      id: Date.now() + logIdIncrement(),
      localts: new Date().toISOString(),
      expanded: false,
      msg,
      level,
      tag,
      ts: Date.now()
    });
  }

  onMount(() => {
    if (__DEV__) {
      const timer = setTimeout(() => {
        if (!ws || !ws.connected) {
          console.log("No ESP32 connection detected. Auto-activating Simulation Mode!");
          if (!isSimMode) {
            toggleSimMode();
          }
        }
      }, 1000);
      return () => clearTimeout(timer);
    }
  });

  // Audio alerts context (Web Audio API)
  let audioCtx: AudioContext | null = null;
  let beepIntervalId: any = null;
  let sirenIntervalId: any = null;
  let sirenOsc1: OscillatorNode | null = null;
  let sirenOsc2: OscillatorNode | null = null;
  let sirenGain: GainNode | null = null;

  function initAudio() {
    if (!audioCtx) {
      audioCtx = new (window.AudioContext || (window as any).webkitAudioContext)();
    }
  }

  function playShortBeep(freq = 880, duration = 0.08) {
    try {
      initAudio();
      if (!audioCtx) return;
      if (audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
      const osc = audioCtx.createOscillator();
      const gainNode = audioCtx.createGain();
      osc.connect(gainNode);
      gainNode.connect(audioCtx.destination);
      osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
      gainNode.gain.setValueAtTime(0.08, audioCtx.currentTime);
      osc.start();
      osc.stop(audioCtx.currentTime + duration);
    } catch (e) {
      console.warn("Web Audio API beep error", e);
    }
  }

  function startSiren() {
    try {
      initAudio();
      if (!audioCtx) return;
      if (audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
      stopSiren();

      sirenOsc1 = audioCtx.createOscillator();
      sirenOsc2 = audioCtx.createOscillator();
      sirenGain = audioCtx.createGain();

      sirenOsc1.type = 'sawtooth';
      sirenOsc2.type = 'sine';

      sirenOsc1.connect(sirenGain);
      sirenOsc2.connect(sirenGain);
      sirenGain.connect(audioCtx.destination);

      sirenGain.gain.setValueAtTime(0, audioCtx.currentTime);
      sirenGain.gain.linearRampToValueAtTime(0.12, audioCtx.currentTime + 0.5);

      sirenOsc1.start();
      sirenOsc2.start();

      let t = 0;
      sirenIntervalId = setInterval(() => {
        if (!audioCtx || !sirenOsc1 || !sirenOsc2) return;
        // Alternating wailing siren frequencies
        const freq1 = 600 + Math.sin(t) * 350;
        const freq2 = 850 + Math.cos(t) * 250;
        sirenOsc1.frequency.setValueAtTime(freq1, audioCtx.currentTime);
        sirenOsc2.frequency.setValueAtTime(freq2, audioCtx.currentTime);
        t += 0.2;
      }, 60);

    } catch (e) {
      console.warn("Web Audio Siren error", e);
    }
  }

  function stopSiren() {
    if (sirenIntervalId) {
      clearInterval(sirenIntervalId);
      sirenIntervalId = null;
    }
    try {
      if (sirenOsc1) {
        sirenOsc1.stop();
        sirenOsc1.disconnect();
        sirenOsc1 = null;
      }
      if (sirenOsc2) {
        sirenOsc2.stop();
        sirenOsc2.disconnect();
        sirenOsc2 = null;
      }
      if (sirenGain) {
        sirenGain.disconnect();
        sirenGain = null;
      }
    } catch(e) {}
  }

  // Simulator state variables
  let simTimerId: any = null;
  let exitDelaySecondsLeft = $state(15);
  let entryDelaySecondsLeft = $state(15);
  let armedModeBeforeSimDelay = 'disarmed';

  $effect(() => {
    if (!isSimMode) {
      if (alarm_state === 'arming_away' || alarm_state === 'arming_home') {
        exitDelaySecondsLeft = 15;
        const interval = setInterval(() => {
          if (exitDelaySecondsLeft > 0) {
            exitDelaySecondsLeft--;
          } else {
            clearInterval(interval);
          }
        }, 1000);
        return () => clearInterval(interval);
      }
    }
  });

  $effect(() => {
    if (!isSimMode) {
      if (alarm_state === 'pending') {
        entryDelaySecondsLeft = 15;
        const interval = setInterval(() => {
          if (entryDelaySecondsLeft > 0) {
            entryDelaySecondsLeft--;
          } else {
            clearInterval(interval);
          }
        }, 1000);
        return () => clearInterval(interval);
      }
    }
  });

  function stopAllSimulation() {
    if (simTimerId) {
      clearInterval(simTimerId);
      simTimerId = null;
    }
    if (beepIntervalId) {
      clearTimeout(beepIntervalId);
      beepIntervalId = null;
    }
    stopSiren();
  }

  function toggleSimMode() {
    isSimMode = !isSimMode;
    stopAllSimulation();
    
    if (isSimMode) {
      updateSystemInfo({
        deviceName: 'Alarma Hub (SIMULATOR)',
        version: 'v1.2.6-demo',
        free_heap: 124500,
        wifi_ssid: 'SimulatedNet_5G',
        wifi_rssi: -58,
        nfc_connected: true,
        mqtt_connected: true
      });
      setAlarmStateLocal('disarmed');
      addSimLog("Simulation Mode enabled. Connected to virtual state machine.");
    } else {
      updateSystemInfo({
        deviceName: '',
        version: '',
        free_heap: 0,
        wifi_ssid: '',
        wifi_rssi: 0,
        nfc_connected: false,
        mqtt_connected: false
      });
      if (ws && ws.connected) {
        ws.send({ type: 'sysinfo' });
      }
    }
  }

  function setAlarmStateLocal(state: string) {
    stopAllSimulation();
    systemInfo.alarm_state = state;
    systemInfo.alarm_zones = [false, false, false, false, false, false, false, false];
    systemInfo.alarm_bypassed = [false, false, false, false, false, false, false, false];
  }

  const setAlarmState = (state: string) => {
    if (isSimMode) {
      if (state === 'DISARMED') {
        setAlarmStateLocal('disarmed');
        addSimLog("System disarmed via Web Console.");
        playShortBeep(523.25, 0.1);
        setTimeout(() => playShortBeep(659.25, 0.15), 100);
      } else if (state === 'ARMED_AWAY') {
        setAlarmStateLocal('arming_away');
        addSimLog("Arming Away started. Exit delay active (15s).");
        exitDelaySecondsLeft = 15;
        simTimerId = setInterval(() => {
          exitDelaySecondsLeft--;
          playShortBeep(880, 0.04);
          if (exitDelaySecondsLeft <= 0) {
            setAlarmStateLocal('armed_away');
            addSimLog("System armed successfully in Away mode.");
            playShortBeep(880, 0.1);
            setTimeout(() => playShortBeep(880, 0.1), 150);
          }
        }, 1000);
      } else if (state === 'ARMED_HOME') {
        setAlarmStateLocal('arming_home');
        addSimLog("Arming Home started. Exit delay active (15s).");
        exitDelaySecondsLeft = 15;
        simTimerId = setInterval(() => {
          exitDelaySecondsLeft--;
          playShortBeep(880, 0.04);
          if (exitDelaySecondsLeft <= 0) {
            setAlarmStateLocal('armed_home');
            addSimLog("System armed successfully in Home mode.");
            playShortBeep(880, 0.1);
            setTimeout(() => playShortBeep(880, 0.1), 150);
          }
        }, 1000);
      }
    } else {
      if (ws && ws.connected) {
        ws.send({ type: 'set_alarm_state', data: state });
      }
    }
  };

  function toggleBypass(index: number) {
    const isBypassed = alarm_bypassed[index];
    if (ws && ws.connected) {
      ws.send({
        type: 'set_zone_bypass',
        zone: index,
        bypass: !isBypassed
      });
    } else if (isSimMode) {
      const newBypassed = [...systemInfo.alarm_bypassed];
      newBypassed[index] = !newBypassed[index];
      systemInfo.alarm_bypassed = newBypassed;
      addSimLog(`Zone ${index + 1} bypass toggled to ${!isBypassed}`, 3, 'SIMULATOR');
    }
  }

  function handleZoneClickSim(index: number) {
    if (!isSimMode) return;
    
    // Toggle sensor state
    const newZones = [...systemInfo.alarm_zones];
    newZones[index] = !newZones[index];
    systemInfo.alarm_zones = newZones;

    const zoneOpen = newZones[index];
    
    // Feedback beep
    playShortBeep(zoneOpen ? 587.33 : 440, 0.05);
    addSimLog(`Zone ${index + 1} (${index === 0 ? 'Front Door' : 'Sensor'}) is ${zoneOpen ? 'OPEN' : 'CLOSED'}.`);

    if (zoneOpen && systemInfo.alarm_bypassed[index]) {
      addSimLog(`Zone ${index + 1} is bypassed. Ignoring alarm rules.`, 3, 'SIMULATOR');
      return;
    }

    // Run rules
    const state = systemInfo.alarm_state;
    if (zoneOpen && (state === 'armed_away' || state === 'armed_home')) {
      if (index === 0) {
        // Trigger Entry Delay on Zone 1
        armedModeBeforeSimDelay = state;
        systemInfo.alarm_state = 'pending';
        entryDelaySecondsLeft = 15;
        addSimLog("Zone 1 (Front Door) breached. Starting entry delay countdown (15s)...");
        
        let secondsPassed = 0;
        simTimerId = setInterval(() => {
          secondsPassed++;
          entryDelaySecondsLeft = 15 - secondsPassed;
          
          if (entryDelaySecondsLeft <= 0) {
            stopAllSimulation();
            systemInfo.alarm_state = 'triggered';
            addSimLog("Entry delay countdown expired. INTRUSION DETECTADA - SIREN ACTIVE!", 4);
            startSiren();
          }
        }, 1000);

        // warning beep loops that accelerate
        const triggerBeepLoop = () => {
          if (systemInfo.alarm_state !== 'pending') return;
          playShortBeep(987.77, 0.06);
          const currentLeft = 15 - secondsPassed;
          const nextBeepDelay = currentLeft <= 5 ? 250 : 1000;
          beepIntervalId = setTimeout(triggerBeepLoop, nextBeepDelay);
        };
        triggerBeepLoop();

      } else {
        // Instant Zone Breach triggers siren immediately
        stopAllSimulation();
        systemInfo.alarm_state = 'triggered';
        addSimLog(`Intrusion detected on Zone ${index + 1} (Instant sensor)! SIREN ACTIVE!`, 4);
        startSiren();
      }
    }
  }

  onDestroy(() => {
    stopAllSimulation();
  });

  const getAlarmStateStyles = (state: string) => {
    switch (state) {
      case 'disarmed':
        return {
          bg: 'bg-emerald-950/20 border-emerald-500/30 text-emerald-400',
          badge: 'badge-success border-emerald-500/30 bg-emerald-500/10 text-emerald-400',
          text: 'text-emerald-400',
          iconColor: 'text-emerald-400',
          label: 'DISARMED',
          desc: 'Security system is disarmed. Access is open.'
        };
      case 'arming_away':
        return {
          bg: 'bg-amber-950/20 border-amber-500/30 text-amber-400 animate-pulse',
          badge: 'badge-warning border-amber-500/30 bg-amber-500/10 text-amber-400',
          text: 'text-amber-400',
          iconColor: 'text-amber-400',
          label: 'ARMING AWAY',
          desc: 'Exit delay active. Please exit the premises.'
        };
      case 'arming_home':
        return {
          bg: 'bg-amber-950/20 border-amber-500/30 text-amber-400 animate-pulse',
          badge: 'badge-warning border-amber-500/30 bg-amber-500/10 text-amber-400',
          text: 'text-amber-400',
          iconColor: 'text-amber-400',
          label: 'ARMING HOME',
          desc: 'Exit delay active. Arming perimeter zones.'
        };
      case 'armed_away':
        return {
          bg: 'bg-indigo-950/20 border-indigo-500/30 text-indigo-400',
          badge: 'badge-primary border-indigo-500/30 bg-indigo-500/10 text-indigo-400',
          text: 'text-indigo-400',
          iconColor: 'text-indigo-400',
          label: 'ARMED AWAY',
          desc: 'All security zones and sensors are fully armed.'
        };
      case 'armed_home':
        return {
          bg: 'bg-sky-950/20 border-sky-500/30 text-sky-400',
          badge: 'badge-info border-sky-500/30 bg-sky-500/10 text-sky-400',
          text: 'text-sky-400',
          iconColor: 'text-sky-400',
          label: 'ARMED HOME',
          desc: 'Perimeter zones armed. Living areas bypass enabled.'
        };
      case 'pending':
        return {
          bg: 'bg-amber-950/30 border-amber-500/50 text-amber-300 animate-pulse',
          badge: 'badge-warning border-amber-500/40 bg-amber-500/20 text-amber-300 font-bold',
          text: 'text-amber-300 font-bold',
          iconColor: 'text-amber-300',
          label: 'ENTRY DELAY',
          desc: 'Entry door opened! Tap HomeKey or Disarm.'
        };
      case 'triggered':
        return {
          bg: 'bg-rose-950/35 border-rose-500/50 text-rose-400 animate-bounce',
          badge: 'badge-error border-rose-500/50 bg-rose-500/20 text-rose-400 font-black animate-ping',
          text: 'text-rose-400 font-black',
          iconColor: 'text-rose-400',
          label: 'ALARM TRIGGERED',
          desc: '🚨 INTRUSION BREACH! Siren active!'
        };
      default:
        return {
          bg: 'bg-slate-900/40 border-white/5 text-slate-400',
          badge: 'badge-ghost border-white/5 text-slate-400',
          text: 'text-slate-400',
          iconColor: 'text-slate-400',
          label: 'OFFLINE',
          desc: 'Connection offline.'
        };
    }
  };

  let heapHistory = $state<number[]>([]);
  let rssiHistory = $state<number[]>([]);

  $effect(() => {
    const heap = systemInfo.free_heap;
    if (heap > 0) {
      untrack(() => {
        if (heapHistory.length === 0 || heapHistory[heapHistory.length - 1] !== heap) {
          heapHistory = [...heapHistory, heap];
          if (heapHistory.length > 20) heapHistory.shift();
        }
      });
    }
  });

  $effect(() => {
    const rssi = systemInfo.wifi_rssi;
    if (rssi !== 0) {
      untrack(() => {
        if (rssiHistory.length === 0 || rssiHistory[rssiHistory.length - 1] !== rssi) {
          rssiHistory = [...rssiHistory, rssi];
          if (rssiHistory.length > 20) rssiHistory.shift();
        }
      });
    }
  });

  let heapPathData = $derived.by(() => {
    if (heapHistory.length < 2) return { line: '', area: '', lastX: 0, lastY: 0, min: 0, max: 0 };
    const min = Math.min(...heapHistory);
    const max = Math.max(...heapHistory);
    const range = max - min || 1;

    const points = heapHistory.map((val, i) => {
      const x = (i / (heapHistory.length - 1)) * 100;
      const y = 35 - ((val - min) / range) * 25;
      return { x, y };
    });

    const line = 'M ' + points.map(p => `${p.x} ${p.y}`).join(' L ');
    const area = line + ` L 100 40 L 0 40 Z`;
    const last = points[points.length - 1];

    return { line, area, lastX: last.x, lastY: last.y, min, max };
  });

  let rssiPathData = $derived.by(() => {
    if (rssiHistory.length < 2) return { line: '', area: '', lastX: 0, lastY: 0, min: 0, max: 0 };
    const min = Math.min(...rssiHistory);
    const max = Math.max(...rssiHistory);
    const range = max - min || 1;

    const points = rssiHistory.map((val, i) => {
      const x = (i / (rssiHistory.length - 1)) * 100;
      const y = 35 - ((val - min) / range) * 25;
      return { x, y };
    });

    const line = 'M ' + points.map(p => `${p.x} ${p.y}`).join(' L ');
    const area = line + ` L 100 40 L 0 40 Z`;
    const last = points[points.length - 1];

    return { line, area, lastX: last.x, lastY: last.y, min, max };
  });

  let stateStyles = $derived(getAlarmStateStyles(alarm_state));
</script>

<div class="w-full py-6 relative">
  <div class="flex flex-col sm:flex-row justify-between items-start sm:items-center gap-4 mb-6">
    <div>
      <h1 class="text-2xl font-bold tracking-tight bg-gradient-to-r from-white to-slate-400 bg-clip-text text-transparent">Security Control Center</h1>
      <p class="text-slate-400 text-sm mt-0.5">
        Real-time status monitoring, zone breaches, and alarm keypad.
      </p>
    </div>
    {#if __DEV__}
    <div class="flex items-center gap-2">
      <button 
        onclick={toggleSimMode} 
        class="btn btn-sm rounded-full border transition-all duration-300 font-semibold px-4 shadow-lg py-2 h-auto {isSimMode ? 'bg-[#8b5cf6]/20 border-[#8b5cf6]/40 text-[#a855f7]' : 'bg-white/5 border-white/10 text-slate-400 hover:text-white hover:bg-white/10'}"
      >
        <span class="inline-block w-2.5 h-2.5 rounded-full mr-2 {isSimMode ? 'bg-[#a855f7] animate-pulse' : 'bg-slate-500'}"></span>
        {isSimMode ? 'Simulation Active' : 'Start Simulation'}
      </button>
    </div>
    {/if}
  </div>

  {#if __DEV__ && isSimMode}
    <div class="mb-6 bg-[#8b5cf6]/10 border border-[#8b5cf6]/30 rounded-2xl p-4 text-sm text-slate-300 flex gap-3 items-start animate-fade-in relative overflow-hidden">
      <div class="absolute inset-0 bg-gradient-to-r from-[#8b5cf6]/5 to-transparent pointer-events-none"></div>
      <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-5 text-[#a855f7] flex-shrink-0 mt-0.5">
        <path stroke-linecap="round" stroke-linejoin="round" d="M11.25 11.25l.041-.02a.75.75 0 111.085 1.086L12.5 13.5v.75H12m0 2.25h.008v.008H12v-.008zM21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
      </svg>
      <div>
        <p class="font-bold text-[#a855f7]">Offline Simulation Active</p>
        <p class="text-xs opacity-90 mt-1">Click on zone cards to simulate triggers. Arm/disarm to test exits & siren effects.</p>
      </div>
    </div>
  {/if}

  {#if displayError}
    <div class="mb-6 bg-rose-950/20 border border-rose-500/30 rounded-2xl p-4 text-sm text-rose-400 flex gap-3 items-start animate-pulse">
      <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-5 flex-shrink-0 mt-0.5">
        <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
      </svg>
      <div>
        <p class="font-bold">Device Error</p>
        <p class="text-xs opacity-90 mt-0.5">{displayError}</p>
      </div>
    </div>
  {/if}

  <div class="grid grid-cols-1 lg:grid-cols-3 gap-6 max-w-6xl items-start mb-6">
    <!-- COLUMN 1 & 2: Alarm Control Panel -->
    <div class="lg:col-span-2 card bg-[#0e0e15]/40 backdrop-blur-xl shadow-2xl border {stateStyles.bg} transition-all duration-300 rounded-[2rem] relative overflow-hidden">
      <div class="absolute inset-0 bg-gradient-to-br from-white/[0.02] to-transparent pointer-events-none"></div>
      <div class="card-body p-6">
        <div class="flex flex-col sm:flex-row justify-between items-start sm:items-center gap-4 mb-6">
          <div class="flex items-center gap-4">
            <div class="w-14 h-14 rounded-2xl bg-white/5 border border-white/10 flex items-center justify-center shadow-lg relative overflow-hidden">
              <div class="absolute inset-0 bg-gradient-to-br from-white/10 to-transparent"></div>
              {#if alarm_state === 'disarmed'}
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-7 {stateStyles.iconColor}">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75 11.25 15 15 9.75m-3-7.036A11.959 11.959 0 0 1 3.598 6 11.99 11.99 0 0 0 3 9.749c0 5.592 3.824 10.29 9 11.623 5.176-1.332 9-6.03 9-11.622 0-1.31-.21-2.57-.599-3.75A11.952 11.952 0 0 1 12 2.714Z" />
                </svg>
              {:else if alarm_state === 'triggered'}
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2.5" stroke="currentColor" class="size-7 {stateStyles.iconColor} animate-pulse">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M14.857 17.082a9.04 9.04 0 0 1-5.714 0m5.714 0a3 3 0 1 1-5.714 0M3.124 7.5A8.969 8.969 0 0 1 5.292 3m13.416 0a8.969 8.969 0 0 1 2.168 4.5M19.186 10.122A3 3 0 0 0 21 8.25c0-2.428-2.017-4.4-4.5-4.4-.99 0-1.89.327-2.617.882M12 12v.01M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm0 0v.01M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm0 0v.01M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6Z" />
                </svg>
              {:else}
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="2" stroke="currentColor" class="size-7 {stateStyles.iconColor}">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M16.5 10.5V6.75a4.5 4.5 0 1 0-9 0v3.75m-.75 11.25h10.5a2.25 2.25 0 0 0 2.25-2.25v-6.75a2.25 2.25 0 0 0-2.25-2.25H6.75a2.25 2.25 0 0 0-2.25 2.25v6.75a2.25 2.25 0 0 0 2.25 2.25Z" />
                </svg>
              {/if}
            </div>
            <div>
              <div class="flex items-center gap-2">
                <span class="text-lg font-bold uppercase tracking-wider {stateStyles.text}">{stateStyles.label}</span>
                <span class="badge {stateStyles.badge} border-none font-bold text-[10px] uppercase">{alarm_state === 'pending' ? 'entry delay' : alarm_state}</span>
              </div>
              <p class="text-xs text-slate-300 mt-0.5">{stateStyles.desc}</p>
            </div>
          </div>
        </div>

        <!-- Countdowns visualization screens -->
        {#if alarm_state === 'pending'}
          <div class="mb-6 flex flex-col items-center justify-center p-6 bg-amber-500/10 border border-amber-500/25 rounded-2xl animate-pulse text-center relative overflow-hidden">
            <div class="absolute inset-0 bg-gradient-to-b from-amber-500/[0.03] to-transparent"></div>
            <span class="text-xs font-semibold tracking-widest text-amber-400 uppercase">Entry Delay Remaining</span>
            <span class="text-5xl font-black text-amber-300 mt-2 font-mono">{entryDelaySecondsLeft}s</span>
            <span class="text-[11px] text-amber-400/80 mt-2 font-medium">Disarm the alarm immediately before intrusion trigger.</span>
          </div>
        {/if}

        {#if alarm_state === 'arming_away' || alarm_state === 'arming_home'}
          <div class="mb-6 flex flex-col items-center justify-center p-6 bg-[#8b5cf6]/10 border border-[#8b5cf6]/25 rounded-2xl animate-pulse text-center relative overflow-hidden">
            <div class="absolute inset-0 bg-gradient-to-b from-[#8b5cf6]/[0.03] to-transparent"></div>
            <span class="text-xs font-semibold tracking-widest text-[#a855f7] uppercase">Exit Delay Remaining</span>
            <span class="text-5xl font-black text-[#a855f7] mt-2 font-mono">{exitDelaySecondsLeft}s</span>
            <span class="text-[11px] text-[#a855f7]/80 mt-2 font-medium">System is arming. Please secure the premises.</span>
          </div>
        {/if}

        <div class="border-t border-white/5 my-2 pt-4">
          <h3 class="text-xs font-bold text-slate-400 mb-4 uppercase tracking-wider text-center">Control Panel Mode</h3>
          
          <!-- iOS 26 Segmented Slider Controller -->
          <div class="relative bg-white/5 border border-white/10 rounded-full p-1 flex w-full max-w-md mx-auto items-center overflow-hidden">
            <!-- Sliding Indicator Pill -->
            <div 
              class="absolute top-1 bottom-1 rounded-full bg-white/10 backdrop-blur-md shadow-lg border border-white/15 transition-all duration-300 ease-out z-0"
              style="width: calc(33.33% - 6px); left: {
                alarm_state === 'disarmed' ? '3px' : 
                alarm_state === 'armed_home' || alarm_state === 'arming_home' ? 'calc(33.33% + 3px)' : 
                'calc(66.66% + 3px)'
              };"
            ></div>

            <!-- Segment Actions -->
            <button 
              onclick={() => setAlarmState('DISARMED')}
              class="w-1/3 py-2.5 rounded-full text-xs font-bold text-center z-10 transition-all duration-300 {alarm_state === 'disarmed' ? 'text-emerald-400 font-extrabold' : 'text-slate-400 hover:text-white'}"
            >
              Disarm
            </button>
            <button 
              onclick={() => setAlarmState('ARMED_HOME')}
              class="w-1/3 py-2.5 rounded-full text-xs font-bold text-center z-10 transition-all duration-300 {alarm_state === 'armed_home' || alarm_state === 'arming_home' ? 'text-sky-400 font-extrabold' : 'text-slate-400 hover:text-white'}"
            >
              Arm Home
            </button>
            <button 
              onclick={() => setAlarmState('ARMED_AWAY')}
              class="w-1/3 py-2.5 rounded-full text-xs font-bold text-center z-10 transition-all duration-300 {alarm_state === 'armed_away' || alarm_state === 'arming_away' ? 'text-indigo-400 font-extrabold' : 'text-slate-400 hover:text-white'}"
            >
              Arm Away
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- COLUMN 3: Live Zone Status -->
    <div class="card bg-[#0e0e15]/40 backdrop-blur-xl shadow-2xl border border-white/5 rounded-[2rem] h-full relative overflow-hidden">
      <div class="absolute inset-0 bg-gradient-to-br from-white/[0.02] to-transparent pointer-events-none"></div>
      <div class="card-body p-5">
        <h2 class="card-title text-sm font-bold uppercase tracking-wider text-slate-300 flex justify-between items-center mb-2">
          Security Zones
          <span class="badge border-none font-bold text-[9px] bg-white/5 text-slate-400 uppercase tracking-widest px-2.5 py-1.5 h-auto">Live Status</span>
        </h2>
        
        <div class="space-y-2.5 mt-2">
          {#each Array.from({ length: 8 }) as _, i}
            <!-- svelte-ignore a11y_click_events_have_key_events -->
            <!-- svelte-ignore a11y_no_static_element_interactions -->
            <div 
              onclick={() => handleZoneClickSim(i)}
              class="flex items-center justify-between p-3.5 rounded-2xl border transition-all duration-300 select-none {alarm_disabled[i] ? 'bg-black/10 border-white/5 opacity-50' : (alarm_zones[i] && !alarm_bypassed[i] ? 'bg-rose-500/10 border-rose-500/30 shadow-[0_0_15px_rgba(239,68,68,0.05)]' : alarm_bypassed[i] ? 'bg-yellow-500/5 border-yellow-500/20 opacity-70' : 'bg-white/[0.02] border-white/5')} {isSimMode ? 'cursor-pointer hover:bg-white/5 active:scale-98' : ''}"
            >
              <div class="flex items-center gap-3">
                <div class="w-9 h-9 rounded-xl flex items-center justify-center font-bold text-xs relative overflow-hidden {alarm_disabled[i] ? 'bg-white/5 text-slate-500 border border-white/5' : (alarm_zones[i] && !alarm_bypassed[i] ? 'bg-rose-500 text-white animate-pulse' : alarm_bypassed[i] ? 'bg-yellow-500/20 text-yellow-400 border border-yellow-500/20' : 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/25')}">
                  {#if !alarm_zones[i] && !alarm_bypassed[i] && !alarm_disabled[i]}
                    <div class="absolute inset-0 bg-gradient-to-br from-emerald-500/5 to-transparent"></div>
                  {/if}
                  Z{i + 1}
                </div>
                <div>
                  <span class="text-xs font-bold {alarm_disabled[i] ? 'text-slate-500' : 'text-slate-200'}">Zone {i + 1}</span>
                  <span class="text-[9px] text-slate-400 block mt-0.5">{i === 0 ? 'Entry Delay (Front Door)' : 'Instant Sensor'}</span>
                </div>
              </div>
              <div class="flex items-center gap-3">
                {#if alarm_disabled[i]}
                  <span class="text-[9px] font-bold text-slate-400 uppercase tracking-widest bg-white/5 px-2 py-0.5 rounded-md border border-white/10">Disabled</span>
                {/if}
                {#if alarm_bypassed[i]}
                  <span class="text-[9px] font-bold text-yellow-400 uppercase tracking-widest bg-yellow-500/10 px-2 py-0.5 rounded-md border border-yellow-500/20">Bypassed</span>
                {/if}
                <div class="tooltip tooltip-left" data-tip={alarm_bypassed[i] ? "Restore zone" : "Bypass zone"}>
                  <input
                    type="checkbox"
                    checked={alarm_bypassed[i]}
                    onclick={(e) => { e.stopPropagation(); toggleBypass(i); }}
                    class="toggle toggle-warning toggle-xs"
                    disabled={alarm_state !== 'disarmed'}
                  />
                </div>
                {#if alarm_disabled[i]}
                  <span class="text-[10px] font-bold text-slate-400 uppercase tracking-widest bg-white/5 px-2.5 py-1 rounded-full">Inactive</span>
                {:else if alarm_zones[i]}
                  <span class="text-[10px] font-bold text-rose-400 uppercase tracking-widest flex items-center gap-1.5 bg-rose-500/15 px-2.5 py-1 rounded-full">
                    <span class="w-1.5 h-1.5 rounded-full bg-rose-500 animate-ping"></span>
                    Open
                  </span>
                {:else}
                  <span class="text-[10px] font-bold text-emerald-400 uppercase tracking-widest bg-emerald-500/15 px-2.5 py-1 rounded-full">Secured</span>
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
    <div class="card bg-[#0e0e15]/40 backdrop-blur-xl shadow-2xl border border-white/5 rounded-[2rem] relative overflow-hidden">
      <div class="absolute inset-0 bg-gradient-to-br from-white/[0.02] to-transparent pointer-events-none"></div>
      <div class="card-body p-5">
        <div class="flex items-center gap-3 mb-4">
          <div class="w-10 h-10 rounded-xl bg-primary/10 border border-primary/20 flex items-center justify-center">
            <svg
              xmlns="http://www.w3.org/2000/svg"
              class="size-5 text-primary"
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
            <h2 class="card-title text-sm font-bold text-slate-300 uppercase tracking-wider">HomeKey Details</h2>
            <p class="text-[10px] text-slate-400">Registered Devices & GID</p>
          </div>
        </div>

        <div class="space-y-2">
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Reader GID</span>
            <span class="text-xs font-medium font-mono text-primary">{hkInfo?.group_identifier || (isSimMode ? "GID_SIMULATED_2026" : "N/A")}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Reader ID</span>
            <span class="text-xs font-medium font-mono text-primary">{hkInfo?.unique_identifier || (isSimMode ? "RID_SIMULATED_93B3" : "N/A")}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Registered Issuers</span>
            <span class="text-xs font-bold text-slate-200">{hkInfo?.issuers?.length || (isSimMode ? 1 : 0)}</span>
          </div>
        </div>

        <!-- Issuers Section inside HomeKey Card -->
        {#if (hkInfo?.issuers && hkInfo.issuers.length > 0) || isSimMode}
          <div class="mt-4 space-y-1">
            {#each (hkInfo?.issuers || [{ issuerId: "ISSUER_SIMULATED_ADMIN_77A", endpoints: [{ endpointId: "DEVICE_SIMULATED_IPHONE_14" }] }]) as issuer, index (`issuer-${index}`)}
              <div class="collapse collapse-arrow bg-[#0e0e15]/30 border border-white/5 rounded-2xl">
                <input type="checkbox" name="info-collapse-{index}" />
                <div class="collapse-title font-bold text-xs py-3 flex items-center gap-2 text-slate-300">
                  <svg xmlns="http://www.w3.org/2000/svg" class="h-4.5 w-4.5 text-slate-400" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z" />
                  </svg>
                  <span>Issuer {index + 1}</span>
                </div>
                <div class="collapse-content text-xs">
                  <div class="py-2.5 px-3 bg-[#0e0e15]/40 rounded-xl mb-2 border border-white/5">
                    <span class="text-[9px] text-slate-400 block mb-1">Issuer ID</span>
                    <span class="text-xs font-mono break-all text-slate-300">{issuer.issuerId || "N/A"}</span>
                  </div>
                  {#if issuer.endpoints && issuer.endpoints.length > 0}
                    <div class="mt-2">
                      <span class="text-[9px] text-slate-400 block mb-2">Registered Devices (Endpoints)</span>
                      <ul class="space-y-1.5">
                        {#each issuer.endpoints as endpoint, epIndex (`ep-${epIndex}`)}
                          <li class="flex items-center gap-2 py-2 px-3 bg-[#0e0e15]/40 rounded-xl border border-white/5">
                            <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" width="24" height="24" fill="currentColor" class="size-3.5 text-primary">
                              <path d="M12 1C16.9706 1 21 5.02944 21 10V14C21 18.9706 16.9706 23 12 23C10.9137 23 9.8724 22.8076 8.90826 22.4549C9.03638 22.2782 9.15938 22.0977 9.27703 21.9134L9.44782 21.633C10.388 20.0636 10.9461 18.2391 10.9963 16.2884L11 16V9H13V16C13 17.7724 12.6453 19.4619 12.0031 21.0015C12.7954 21 13.5599 20.8673 14.2724 20.6229C14.7147 19.2616 14.966 17.8148 14.9968 16.3138L15 16L14.9998 12.999H16.9998L17 16C17 17.0885 16.8977 18.1531 16.7022 19.1847C18.0583 17.9552 18.9297 16.2 18.9959 14.2407L19 14V10C19 6.13401 15.866 3 12 3C10.4277 3 8.97638 3.51841 7.8078 4.39364L6.38282 2.96769C7.92242 1.73631 9.87522 1 12 1ZM7 10C7 7.23858 9.23858 5 12 5C14.7614 5 17 7.23858 17 10V11H15V10C15 8.34315 13.6569 7 12 7C10.4023 7 9.09634 8.24892 9.00509 9.82373L9 10V16C9 17.5669 8.5996 19.0402 7.89554 20.3233L7.87214 20.3627C7.64284 20.7771 7.38087 21.1711 7.09037 21.5417C6.6495 21.2545 6.23541 20.9297 5.85264 20.5719L5.5445 20.2711C3.96956 18.65 3 16.4382 3 14V10C3 7.87522 3.73631 5.92242 4.96769 4.38282L6.39364 5.8078C5.56325 6.91652 5.05405 8.27971 5.00406 9.75935L5 10V14C5 15.6748 5.58816 17.2122 6.56918 18.4169C6.82239 17.7351 6.97017 17.0034 6.99594 16.2407L7 16V10Z" />
                            </svg>
                            <span class="text-xs font-mono text-slate-300">{endpoint.endpointId || "N/A"}</span>
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
    <div class="card bg-[#0e0e15]/40 backdrop-blur-xl shadow-2xl border border-white/5 rounded-[2rem] relative overflow-hidden">
      <div class="absolute inset-0 bg-gradient-to-br from-white/[0.02] to-transparent pointer-events-none"></div>
      <div class="card-body p-5">
        <div class="flex items-center gap-3 mb-4">
          <div class="w-10 h-10 rounded-xl bg-info/10 border border-info/20 flex items-center justify-center">
            <svg
              viewBox="0 0 24 24"
              xmlns="http://www.w3.org/2000/svg"
              width="24"
              height="24"
              fill="currentColor"
              class="size-5 text-info"
            >
              <path
                d="M6 18H18V6H6V18ZM14 20H10V22H8V20H5C4.44772 20 4 19.5523 4 19V16H2V14H4V10H2V8H4V5C4 4.44772 4.44772 4 5 4H8V2H10V4H14V2H16V4H19C19.5523 4 20 4.44772 20 5V8H22V10H20V14H22V16H20V19C20 19.5523 19.5523 20 19 20H16V22H14V20ZM8 8H16V16H8V8Z"
              >
              </path>
            </svg>
          </div>
          <div>
            <h2 class="card-title text-sm font-bold text-slate-300 uppercase tracking-wider">System Status</h2>
            <p class="text-[10px] text-slate-400">Firmware & Hardware Metrics</p>
          </div>
        </div>

        <div class="space-y-2">
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Version</span>
            <span class="text-xs font-bold text-slate-250">{systemInfo?.version || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">UI Version</span>
            <span class="text-xs font-bold text-slate-250">{version}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Device Name</span>
            <span class="text-xs font-bold text-slate-250">{systemInfo?.deviceName || "N/A"}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Uptime</span>
            <span class="text-xs font-bold text-slate-250">{systemInfo?.uptime || 0}s</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Free Heap</span>
            <span class="text-xs font-mono text-slate-200">{systemInfo?.free_heap || 0} bytes</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">WiFi SSID</span>
            <span class="text-xs font-bold text-slate-250">{systemInfo?.wifi_ssid || "N/A"}</span>
          </div>
          {#if !systemInfo?.eth_enabled}
            <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
              <span class="text-xs text-slate-300 font-medium">WiFi RSSI</span>
              <span class="text-xs font-bold text-slate-250">{systemInfo?.wifi_rssi || 0} dBm ({wifi_signal})</span>
            </div>
          {/if}
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">Ethernet enabled</span>
            <span class="text-xs font-bold text-slate-250">{systemInfo?.eth_enabled ? "Yes" : "No"}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">NFC Module</span>
            <span class="text-xs font-bold" class:text-emerald-400={systemInfo?.nfc_connected} class:text-rose-450={!systemInfo?.nfc_connected}>{systemInfo?.nfc_connected ? "Connected" : "Disconnected"}</span>
          </div>
          <div class="flex items-center justify-between py-2.5 px-3 bg-[#0e0e15]/30 border border-white/5 rounded-xl">
            <span class="text-xs text-slate-300 font-medium">MQTT broker</span>
            <span class="text-xs font-bold" class:text-emerald-400={systemInfo?.mqtt_connected} class:text-rose-450={!systemInfo?.mqtt_connected}>{systemInfo?.mqtt_connected ? "Connected" : "Disconnected"}</span>
          </div>
        </div>

        <div class="mt-4 border-t border-white/5 pt-4 space-y-4">
          <!-- Heap Memory Graph -->
          <div>
            <div class="flex justify-between items-center mb-1.5 text-[10px]">
              <span class="text-slate-400 font-medium">Heap Memory Stability</span>
              <span class="text-slate-400 font-mono">
                Min: {heapPathData.min.toLocaleString()} | Max: {heapPathData.max.toLocaleString()} B
              </span>
            </div>
            <div class="h-16 w-full bg-[#07060f]/60 rounded-xl border border-white/5 relative overflow-hidden p-1">
              {#if heapHistory.length >= 2}
                <svg viewBox="0 0 100 40" class="w-full h-full overflow-visible" preserveAspectRatio="none">
                  <defs>
                    <linearGradient id="heap-grad" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="0%" stop-color="#38bdf8" stop-opacity="0.25"/>
                      <stop offset="100%" stop-color="#38bdf8" stop-opacity="0.0"/>
                    </linearGradient>
                  </defs>
                  <path d={heapPathData.area} fill="url(#heap-grad)" />
                  <path d={heapPathData.line} fill="none" stroke="#38bdf8" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
                  <circle cx={heapPathData.lastX} cy={heapPathData.lastY} r="1.5" fill="#38bdf8" class="animate-pulse" />
                </svg>
              {:else}
                <div class="flex items-center justify-center h-full text-[10px] text-slate-500">
                  Gathering heap statistics...
                </div>
              {/if}
            </div>
          </div>

          <!-- WiFi RSSI Graph -->
          {#if !systemInfo?.eth_enabled}
            <div>
              <div class="flex justify-between items-center mb-1.5 text-[10px]">
                <span class="text-slate-400 font-medium">Wi-Fi Signal Strength (RSSI)</span>
                <span class="text-slate-400 font-mono">
                  Min: {rssiPathData.min} | Max: {rssiPathData.max} dBm
                </span>
              </div>
              <div class="h-16 w-full bg-[#07060f]/60 rounded-xl border border-white/5 relative overflow-hidden p-1">
                {#if rssiHistory.length >= 2}
                  <svg viewBox="0 0 100 40" class="w-full h-full overflow-visible" preserveAspectRatio="none">
                    <defs>
                      <linearGradient id="rssi-grad" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0%" stop-color="#10b981" stop-opacity="0.25"/>
                        <stop offset="100%" stop-color="#10b981" stop-opacity="0.0"/>
                      </linearGradient>
                    </defs>
                    <path d={rssiPathData.area} fill="url(#rssi-grad)" />
                    <path d={rssiPathData.line} fill="none" stroke="#10b981" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
                    <circle cx={rssiPathData.lastX} cy={rssiPathData.lastY} r="1.5" fill="#10b981" class="animate-pulse" />
                  </svg>
                {:else}
                  <div class="flex items-center justify-center h-full text-[10px] text-slate-500">
                    Gathering RSSI statistics...
                  </div>
                {/if}
              </div>
            </div>
          {/if}
        </div>
      </div>
    </div>
  </div>

  <!-- Recent Activity Card -->
  <div class="card bg-[#0e0e15]/40 backdrop-blur-xl shadow-2xl border border-white/5 rounded-[2rem] mt-6 relative overflow-hidden max-w-6xl">
    <div class="absolute inset-0 bg-gradient-to-br from-white/[0.02] to-transparent pointer-events-none"></div>
    <div class="card-body p-5">
      <h2 class="card-title text-sm font-bold uppercase tracking-wider text-slate-300 flex justify-between items-center mb-2">
        Recent Activity
        <span class="badge border-none font-bold text-[9px] bg-white/5 text-slate-400 uppercase tracking-widest px-2.5 py-1.5 h-auto">Logs</span>
      </h2>
      <div class="space-y-2 mt-2 max-h-60 overflow-y-auto">
        {#if logs.length === 0}
          <div class="text-center py-6 text-slate-400 text-xs">
            No recent events logged. Trigger a sensor or arm state to see activity.
          </div>
        {:else}
          {#each logs.slice(0, 5) as log (log.id)}
            <div class="flex flex-col sm:flex-row sm:items-center justify-between p-3 rounded-xl bg-white/[0.01] border border-white/5 gap-2 text-xs transition-all duration-300 hover:bg-white/5">
              <div class="flex items-center gap-3">
                <span class="font-mono text-slate-400">
                  {new Date(log.localts || Date.now()).toLocaleTimeString()}
                </span>
                <span class="badge border-none text-[9px] font-bold uppercase tracking-wider px-2 py-0.5 h-auto {
                  log.tag === 'SIMULATOR' ? 'bg-pink-500/10 text-pink-400 border border-pink-500/20' : 
                  log.tag === 'NFC' ? 'bg-sky-500/10 text-sky-400 border border-sky-500/20' : 
                  log.tag === 'MQTT' ? 'bg-indigo-500/10 text-indigo-400 border border-indigo-500/20' : 
                  'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20'
                }">
                  {log.tag || 'SYSTEM'}
                </span>
                <span class="text-slate-200 font-medium break-all">{log.msg}</span>
              </div>
              <span class="text-[10px] text-slate-400/80 font-mono">
                Level {log.level}
              </span>
            </div>
          {/each}
        {/if}
      </div>
    </div>
  </div>
</div>
