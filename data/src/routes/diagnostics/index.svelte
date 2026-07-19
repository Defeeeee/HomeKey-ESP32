<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import ws, { type WebSocketEvent } from "$lib/services/ws.js";
  import { zoneHistory, zoneStats } from "$lib/stores/diagnostics.svelte.js";
  import { systemInfo } from "$lib/stores/system.svelte.js";
  import { events, setEvents, formatEvent } from "$lib/stores/eventlog.svelte.js";

  const names = [
    "Main Door", "Living Room Motion Sensor", "Window Flap A", "Window Flap B",
    "Upstairs Curtain", "Zone 6", "Zone 7", "Zone 8"
  ];

  let wsUnsubscribe: (() => void) | undefined;

  function requestLog() {
    if (ws && ws.connected) ws.send({ type: "get_event_log" });
  }

  function clearLog() {
    if (!confirm("¿Borrar el historial persistente de eventos del dispositivo? No se puede deshacer.")) return;
    if (ws && ws.connected) ws.send({ type: "clear_event_log" });
  }

  function fmtUptime(ms?: number): string {
    if (!ms || ms < 0) return "—";
    const s = Math.floor(ms / 1000);
    const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m ${s % 60}s`;
  }

  const toneClass: Record<string, string> = {
    normal: "text-slate-300",
    good: "text-emerald-400",
    warn: "text-amber-400",
    bad: "text-rose-400",
  };

  let requestedOnce = false;
  onMount(() => {
    if (ws && typeof ws.on === "function") {
      wsUnsubscribe = ws.on((event: WebSocketEvent<any>) => {
        if (event.type !== "message") return;
        if (event.data?.type === "event_log") {
          setEvents(event.data.events ?? []);
        } else if (!requestedOnce) {
          // WS may not have been connected at mount; grab the log once it's alive.
          requestedOnce = true;
          requestLog();
        }
      });
    }
    requestLog();
  });

  onDestroy(() => { if (wsUnsubscribe) wsUnsubscribe(); });

  let mqttDownMin = $derived(Math.floor((systemInfo.mqtt_down_ms ?? 0) / 60000));
  let resetTone = $derived(
    systemInfo.reset_reason === "PANIC" || systemInfo.reset_reason === "BROWNOUT" ? "bad"
    : systemInfo.reset_reason === "POWERON" ? "good" : "warn"
  );
</script>

<div class="py-2.5 space-y-2.5 text-xs">
  <div class="flex items-center justify-between">
    <h1 class="text-base font-bold text-white">Diagnostics</h1>
    <div class="flex gap-1.5">
      <button onclick={requestLog} class="btn btn-xs rounded-lg btn-outline border-white/15 text-slate-300">Refrescar</button>
      <button onclick={clearLog} class="btn btn-xs rounded-lg btn-outline border-rose-500/30 text-rose-300 hover:bg-rose-500/15">Borrar log</button>
    </div>
  </div>

  <!-- System Health -->
  <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-2.5">
    <div class="p-2.5 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
      <div class="text-[9px] uppercase tracking-wider text-slate-500">Último reinicio</div>
      <div class="font-bold {toneClass[resetTone]}">{systemInfo.reset_reason ?? "—"}</div>
    </div>
    <div class="p-2.5 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
      <div class="text-[9px] uppercase tracking-wider text-slate-500">Uptime</div>
      <div class="font-bold text-slate-200">{fmtUptime(systemInfo.uptime)}</div>
    </div>
    <div class="p-2.5 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
      <div class="text-[9px] uppercase tracking-wider text-slate-500">Heap libre</div>
      <div class="font-bold text-slate-200">{systemInfo.free_heap ? Math.round(systemInfo.free_heap / 1024) + " KB" : "—"}</div>
    </div>
    <div class="p-2.5 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
      <div class="text-[9px] uppercase tracking-wider text-slate-500">WiFi RSSI</div>
      <div class="font-bold text-slate-200">{systemInfo.wifi_rssi ?? "—"} dBm</div>
    </div>
    <div class="p-2.5 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
      <div class="text-[9px] uppercase tracking-wider text-slate-500">MQTT</div>
      <div class="font-bold {systemInfo.mqtt_connected ? 'text-emerald-400' : 'text-rose-400'}">
        {systemInfo.mqtt_connected ? "conectado" : "caído"}
      </div>
    </div>
    <div class="p-2.5 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
      <div class="text-[9px] uppercase tracking-wider text-slate-500">MQTT caído hace</div>
      <div class="font-bold {mqttDownMin > 0 ? 'text-amber-400' : 'text-slate-200'}">{mqttDownMin > 0 ? mqttDownMin + " min" : "—"}</div>
    </div>
  </div>

  <div class="grid grid-cols-1 lg:grid-cols-3 gap-2.5">
    <div class="lg:col-span-2 space-y-2.5">
      <!-- Zones -->
      <div class="space-y-1">
        {#each names as name, i}
          {@const isD = systemInfo.alarm_disabled[i]}
          {@const isB = systemInfo.alarm_bypassed[i]}
          {@const isO = systemInfo.alarm_zones[i]}
          <div class="flex items-center justify-between p-2 bg-[#0e0e15]/40 border border-white/5 rounded-xl">
            <div class="flex gap-1.5">
              <span class="font-bold text-slate-400">Z{i + 1}</span>
              <span class="text-slate-250">{name}</span>
            </div>
            <div class="flex items-center gap-2">
              <span class="text-[10px] text-slate-400">Triggers: <strong>{zoneStats[i].triggerCount}</strong></span>
              <span class="badge text-[9px] uppercase px-1.5 py-0.2 font-bold {
                isD ? 'bg-white/5 text-slate-500 border-none' :
                isB ? 'bg-amber-500/10 text-amber-400 border border-amber-500/20' :
                isO ? 'bg-rose-500/10 text-rose-450 border border-rose-500/20' :
                'bg-emerald-500/10 text-emerald-450 border border-emerald-500/20'
              }">{isD ? "Disabled" : isB ? "Bypassed" : isO ? "Open" : "Closed"}</span>
            </div>
          </div>
        {/each}
      </div>

      <!-- Persistent Event Log (survives reboots) -->
      <div class="bg-[#0e0e15]/40 border border-white/5 rounded-xl p-2.5">
        <h3 class="font-bold text-slate-300 mb-1.5 flex items-center gap-1.5">
          Historial persistente
          <span class="text-[9px] font-normal text-slate-500">(sobrevive reinicios · {events.length})</span>
        </h3>
        <div class="space-y-1 max-h-96 overflow-y-auto">
          {#each events as e}
            {@const f = formatEvent(e)}
            <div class="flex items-center gap-2 p-1.5 rounded bg-white/[0.01] border border-white/5">
              <span class="text-sm w-5 text-center">{f.icon}</span>
              <div class="flex-1 min-w-0">
                <div class="font-bold {toneClass[f.tone]} truncate">{f.label}{#if f.detail}<span class="font-normal text-slate-500"> · {f.detail}</span>{/if}</div>
                <div class="text-[9px] text-slate-500">{f.time}</div>
              </div>
            </div>
          {:else}
            <div class="text-slate-500 text-[10px] p-2 text-center">Sin eventos registrados todavía.</div>
          {/each}
        </div>
      </div>
    </div>

    <!-- Live client-side activity (this session only) -->
    <div class="bg-[#0e0e15]/40 border border-white/5 rounded-xl p-2.5 text-[10px]">
      <h3 class="font-bold text-slate-300 mb-1.5">Live Activity <span class="text-[9px] font-normal text-slate-500">(esta sesión)</span></h3>
      <div class="space-y-1 max-h-96 overflow-y-auto">
        {#each zoneHistory as a}
          <div class="flex items-center justify-between p-1 rounded bg-white/[0.01] border border-white/5">
            <span class="text-slate-400">{a.time}</span>
            <span class="font-bold text-slate-300">Z{a.zone}</span>
            <span class="badge border-none text-[8px] px-1 py-0.2 {a.state === 'OPEN' ? 'bg-rose-500/10 text-rose-450' : 'bg-emerald-500/10 text-emerald-455'}">{a.state}</span>
          </div>
        {/each}
      </div>
    </div>
  </div>
</div>
