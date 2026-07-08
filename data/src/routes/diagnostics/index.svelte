<script lang="ts">
  import { zoneHistory, zoneStats } from "$lib/stores/diagnostics.svelte.js";
  import { systemInfo } from "$lib/stores/system.svelte.js";

  const names = [
    "Main Door", "Living Room Motion Sensor", "Window Flap A", "Window Flap B",
    "Upstairs Curtain", "Zone 6", "Zone 7", "Zone 8"
  ];
</script>

<div class="py-2.5 space-y-2.5 text-xs">
  <h1 class="text-base font-bold text-white">Diagnostics</h1>
  <div class="grid grid-cols-1 lg:grid-cols-3 gap-2.5">
    <div class="lg:col-span-2 space-y-1">
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

    <div class="bg-[#0e0e15]/40 border border-white/5 rounded-xl p-2.5 text-[10px]">
      <h3 class="font-bold text-slate-300 mb-1.5">Live Activity</h3>
      <div class="space-y-1 max-h-64 overflow-y-auto">
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
