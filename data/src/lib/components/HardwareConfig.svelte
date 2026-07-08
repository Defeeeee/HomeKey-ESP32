<script lang="ts">
	import type { EthConfig, NfcGpioPinsPreset } from '$lib/types/api';
	import { route } from 'sv-router/generated';

	interface Props {
		nfcGpioPins: [number, number, number, number];
		nfcPinsPreset: number;
		nfcPresets: NfcGpioPinsPreset | null;
		nfcReaderType: number;
		nfcIrqPin: number;
		nfcVenPin: number;
		ethernetEnabled: boolean;
		ethActivePreset: number;
		ethPhyType: number;
		ethSpiBus: number;
		ethRmiiConfig: [number, number, number, number, number];
		ethSpiConfig: [number, number, number, number, number, number, number];
		ethConfig: EthConfig | null;
		nfcConnected?: boolean;
		loading?: boolean;
		nfcFastPollingEnabled?: boolean;
		dscClockPin: number;
		dscReadPin: number;
		dscWritePin: number;
		zonePin1: number;
		zonePin2: number;
		zonePin3: number;
		zonePin4: number;
		zonePin5: number;
		zonePin6: number;
		zonePin7: number;
		zonePin8: number;
		zoneDisabled1: boolean;
		zoneDisabled2: boolean;
		zoneDisabled3: boolean;
		zoneDisabled4: boolean;
		zoneDisabled5: boolean;
		zoneDisabled6: boolean;
		zoneDisabled7: boolean;
		zoneDisabled8: boolean;
	}

	let {
		nfcGpioPins = $bindable(),
		nfcPinsPreset = $bindable(),
		nfcPresets,
		nfcReaderType = $bindable(),
		nfcIrqPin = $bindable(),
		nfcVenPin = $bindable(),
		ethernetEnabled = $bindable(),
		ethActivePreset = $bindable(),
		ethPhyType = $bindable(),
		ethSpiBus = $bindable(),
		ethRmiiConfig = $bindable(),
		ethSpiConfig = $bindable(),
		ethConfig,
		nfcFastPollingEnabled = $bindable(false),
		nfcConnected = false,
		loading = false,
		dscClockPin = $bindable(21),
		dscReadPin = $bindable(18),
		dscWritePin = $bindable(19),
		zonePin1 = $bindable(13),
		zonePin2 = $bindable(17),
		zonePin3 = $bindable(14),
		zonePin4 = $bindable(25),
		zonePin5 = $bindable(26),
		zonePin6 = $bindable(27),
		zonePin7 = $bindable(32),
		zonePin8 = $bindable(255),
		zoneDisabled1 = $bindable(false),
		zoneDisabled2 = $bindable(false),
		zoneDisabled3 = $bindable(false),
		zoneDisabled4 = $bindable(false),
		zoneDisabled5 = $bindable(false),
		zoneDisabled6 = $bindable(false),
		zoneDisabled7 = $bindable(false),
		zoneDisabled8 = $bindable(false),
	}: Props = $props();

	const isCaptivePortal = $derived(route.pathname.startsWith('/captive-portal'));

	let currentEthChip = $derived(() => {
		if (ethPhyType !== undefined && ethConfig?.supportedChips) {
			return ethConfig.supportedChips[ethPhyType];
		}
		return null;
	});
</script>

<div class="space-y-4">
  {#if ethernetEnabled && !currentEthChip()?.emac}
    <div class="alert alert-warning alert-sm mb-2">
      <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4 shrink-0 stroke-current" fill="none" viewBox="0 0 24 24">
        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
      </svg>
      <span class="text-xs">
        Note: {(ethConfig?.numSpiBuses ?? 1) == 1 ? "Only one SPI bus available" : "Two SPI buses available"} and Ethernet is assigned to the
        {ethSpiBus === 1 ? "first (SPI2) bus." : "second (SPI3) bus."}
        <br />
        Ensure that the PN532 pins {ethSpiBus === 1 ? "(except CS/SS) match" : "do not conflict with"} the Ethernet pins as they will {ethSpiBus === 1 ? "share the same bus." : "be initialized on separate buses."}
      </span>
    </div>
  {/if}
	<!-- NFC Reader -->
	<div class="py-2 px-3 bg-base-100 rounded-lg">
		<div class="flex items-center justify-between mb-2">
			<p class="text-sm font-medium">NFC Reader</p>
			{#if nfcConnected !== undefined && !isCaptivePortal}
				<div class="flex items-center gap-2">
					<span class="relative flex h-2.5 w-2.5">
						{#if nfcConnected}
							<span class="animate-ping absolute inline-flex h-full w-full rounded-full bg-success opacity-75"></span>
							<span class="relative inline-flex rounded-full h-2.5 w-2.5 bg-success"></span>
						{:else}
							<span class="relative inline-flex rounded-full h-2.5 w-2.5 bg-error"></span>
						{/if}
					</span>
					<span class="text-xs font-medium {nfcConnected ? 'text-success' : 'text-error'}">
						{nfcConnected ? 'Connected' : 'Disconnected'}
					</span>
				</div>
			{/if}
		</div>
		<div class="form-control mb-2">
			<label class="label" for="nfcReaderType">
				<span class="label-text text-xs">Reader Type</span>
			</label>
			<select
				id="nfcReaderType"
				bind:value={nfcReaderType}
				class="select select-sm select-bordered w-full"
				disabled={loading}
			>
				<option value={0}>PN532</option>
				<option value={1}>PN7161</option>
			</select>
		</div>
		<div class="form-control mb-2">
			<label class="label" for="nfcPreset">
				<span class="label-text text-xs">Preset</span>
			</label>
			<select
				id="nfcPreset"
				bind:value={nfcPinsPreset}
				class="select select-sm select-bordered w-full"
				disabled={loading}
			>
				{#if nfcPresets?.presets}
					{#each nfcPresets.presets as preset, i}
						{#if preset.type === nfcReaderType}
							<option value={i}>{preset.name}</option>
						{/if}
					{/each}
				{/if}
				<option value={255}>Custom</option>
			</select>
		</div>
		<div class="grid grid-cols-4 gap-2 mb-2">
			<div class="form-control">
				<label class="label" for="nfcSsPin">
					<span class="label-text text-xs">SS Pin</span>
				</label>
				<input
					id="nfcSsPin"
					type="number"
					disabled={nfcPinsPreset !== 255 || loading}
					bind:value={nfcGpioPins[0]}
					class="input input-sm input-bordered w-full"
				/>
			</div>
			<div class="form-control">
				<label class="label" for="nfcSckPin">
					<span class="label-text text-xs">SCK Pin</span>
				</label>
				<input
					id="nfcSckPin"
					type="number"
					disabled={nfcPinsPreset !== 255 || loading}
					bind:value={nfcGpioPins[1]}
					class="input input-sm input-bordered w-full"
				/>
			</div>
			<div class="form-control">
				<label class="label" for="nfcMisoPin">
					<span class="label-text text-xs">MISO Pin</span>
				</label>
				<input
					id="nfcMisoPin"
					type="number"
					disabled={nfcPinsPreset !== 255 || loading}
					bind:value={nfcGpioPins[2]}
					class="input input-sm input-bordered w-full"
				/>
			</div>
			<div class="form-control">
				<label class="label" for="nfcMosiPin">
					<span class="label-text text-xs">MOSI Pin</span>
				</label>
				<input
					id="nfcMosiPin"
					type="number"
					disabled={nfcPinsPreset !== 255 || loading}
					bind:value={nfcGpioPins[3]}
					class="input input-sm input-bordered w-full"
				/>
			</div>
		</div>
		{#if nfcReaderType === 1}
			<div class="grid grid-cols-2 gap-2 mb-2">
				<div class="form-control">
					<label class="label" for="nfcIrqPin">
						<span class="label-text text-xs">IRQ Pin</span>
					</label>
					<input
						id="nfcIrqPin"
						type="number"
						disabled={nfcPinsPreset !== 255 || loading}
						bind:value={nfcIrqPin}
						class="input input-sm input-bordered w-full"
					/>
				</div>
				<div class="form-control">
					<label class="label" for="nfcVenPin">
						<span class="label-text text-xs">VEN Pin</span>
					</label>
					<input
						id="nfcVenPin"
						type="number"
						disabled={nfcPinsPreset !== 255 || loading}
						bind:value={nfcVenPin}
						class="input input-sm input-bordered w-full"
					/>
				</div>
			</div>
		{/if}
    <div class="flex items-center justify-between py-2 px-3 bg-base-200 rounded-lg">
      <div>
        <p class="text-sm font-medium">Fast NFC Polling</p>
        <p class="text-xs text-base-content/60">Reduces the delay between poll cycles for quicker tag detection.</p>
      </div>
      <input
        type="checkbox"
        bind:checked={nfcFastPollingEnabled}
        class="toggle toggle-primary toggle-sm"
		disabled={loading}
      />
    </div>
	</div>

	<!-- Ethernet Configuration -->
	<div class="py-2 px-3 bg-base-100 rounded-lg">
		<p class="text-sm font-medium mb-2">Ethernet Configuration</p>

		<div class="flex items-center justify-between mb-3">
			<div>
				<p class="text-sm font-medium">Enable Ethernet</p>
				<p class="text-xs text-base-content/60">Use wired Ethernet instead of WiFi</p>
			</div>
			<input
				type="checkbox"
				bind:checked={ethernetEnabled}
				class="toggle toggle-primary toggle-sm"
				disabled={loading}
			/>
		</div>

		{#if ethernetEnabled}
			<div class="form-control">
				<label class="label" for="ethPreset">
					<span class="label-text text-xs">Board Preset</span>
				</label>
				<select
					id="ethPreset"
					bind:value={ethActivePreset}
					class="select select-sm select-bordered w-full"
					disabled={loading}
				>
					{#if ethConfig?.boardPresets}
						{#each ethConfig.boardPresets as preset, i}
							<option value={i}>{preset.name}</option>
						{/each}
					{/if}
					<option value={255}>Custom</option>
				</select>
			</div>

			<div class="form-control">
				<label class="label" for="ethPhyType">
					<span class="label-text text-xs">PHY Type</span>
				</label>
				<select
					id="ethPhyType"
					bind:value={ethPhyType}
					disabled={ethActivePreset !== 255 || loading}
					class="select select-sm select-bordered w-full"
				>
					{#if ethConfig?.supportedChips}
						{#each ethConfig.supportedChips as chip}
							<option value={chip.phy_type}>{chip.name}</option>
						{/each}
					{/if}
				</select>
			</div>

			{#if currentEthChip()?.emac}
				<div class="py-2 px-3 bg-base-100 rounded-lg">
					<p class="text-sm font-medium mb-2">RMII Configuration</p>
					<div class="grid grid-cols-2 md:grid-cols-3 gap-2">
						<div class="form-control">
							<label class="label" for="ethPhyAddr">
								<span class="label-text text-xs">PHY Address</span>
							</label>
							<input
								id="ethPhyAddr"
								type="number"
								bind:value={ethRmiiConfig[0]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethPinMdc">
								<span class="label-text text-xs">Pin MDC</span>
							</label>
							<input
								id="ethPinMdc"
								type="number"
								bind:value={ethRmiiConfig[1]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethPinMdio">
								<span class="label-text text-xs">Pin MDIO</span>
							</label>
							<input
								id="ethPinMdio"
								type="number"
								bind:value={ethRmiiConfig[2]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethPinPower">
								<span class="label-text text-xs">Pin Power</span>
							</label>
							<input
								id="ethPinPower"
								type="number"
								bind:value={ethRmiiConfig[3]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethRmiiClock">
								<span class="label-text text-xs">RMII Clock</span>
							</label>
							<select
								id="ethRmiiClock"
								bind:value={ethRmiiConfig[4]}
								disabled={ethActivePreset !== 255 || loading}
								class="select select-sm select-bordered w-full"
							>
								<option value={0}>GPIO0_IN</option>
								<option value={1}>GPIO0_OUT</option>
								<option value={2}>GPIO16_OUT</option>
								<option value={3}>GPIO17_OUT</option>
							</select>
						</div>
					</div>
				</div>
			{:else}
				<div class="py-2 px-3 bg-base-100 rounded-lg">
					<p class="text-sm font-medium mb-2">SPI Configuration</p>
					<div class="grid grid-cols-2 md:grid-cols-4 gap-2">
						<div class="form-control">
							<label class="label" for="ethSpiBus">
								<span class="label-text text-xs">SPI Bus</span>
							</label>
							<select
								id="ethSpiBus"
								bind:value={ethSpiBus}
								disabled={ethActivePreset !== 255 || loading || ethConfig?.numSpiBuses === 1}
								class="select select-sm select-bordered w-full"
							>
								<option value={1}>SPI2</option>
								{#if ethConfig?.numSpiBuses === 2}
									<option value={2}>SPI3</option>
								{/if}
							</select>
						</div>
						<div class="form-control">
							<label class="label" for="ethSpiFreq">
								<span class="label-text text-xs">Freq (MHz)</span>
							</label>
							<input
								id="ethSpiFreq"
								type="number"
								bind:value={ethSpiConfig[0]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethCsPin">
								<span class="label-text text-xs">CS Pin</span>
							</label>
							<input
								id="ethCsPin"
								type="number"
								bind:value={ethSpiConfig[1]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethIrqPin">
								<span class="label-text text-xs">IRQ Pin</span>
							</label>
							<input
								id="ethIrqPin"
								type="number"
								bind:value={ethSpiConfig[2]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethRstPin">
								<span class="label-text text-xs">RST Pin</span>
							</label>
							<input
								id="ethRstPin"
								type="number"
								bind:value={ethSpiConfig[3]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethSckPin">
								<span class="label-text text-xs">SCK Pin</span>
							</label>
							<input
								id="ethSckPin"
								type="number"
								bind:value={ethSpiConfig[4]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethMisoPin">
								<span class="label-text text-xs">MISO Pin</span>
							</label>
							<input
								id="ethMisoPin"
								type="number"
								bind:value={ethSpiConfig[5]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
						<div class="form-control">
							<label class="label" for="ethMosiPin">
								<span class="label-text text-xs">MOSI Pin</span>
							</label>
							<input
								id="ethMosiPin"
								type="number"
								bind:value={ethSpiConfig[6]}
								disabled={ethActivePreset !== 255 || loading}
								class="input input-sm input-bordered w-full"
							/>
						</div>
					</div>
				</div>
			{/if}
		{/if}
	</div>

	<!-- DSC Keybus Pins -->
	<div class="py-2 px-3 bg-base-100 rounded-lg">
		<p class="text-sm font-medium mb-2">DSC Keybus Pins</p>
		<div class="grid grid-cols-3 gap-2">
			<div class="form-control">
				<label class="label" for="dscClockPin">
					<span class="label-text text-xs">Clock Pin</span>
				</label>
				<input
					id="dscClockPin"
					type="number"
					disabled={loading}
					bind:value={dscClockPin}
					class="input input-sm input-bordered w-full"
				/>
			</div>
			<div class="form-control">
				<label class="label" for="dscReadPin">
					<span class="label-text text-xs">Read Pin</span>
				</label>
				<input
					id="dscReadPin"
					type="number"
					disabled={loading}
					bind:value={dscReadPin}
					class="input input-sm input-bordered w-full"
				/>
			</div>
			<div class="form-control">
				<label class="label" for="dscWritePin">
					<span class="label-text text-xs">Write Pin</span>
				</label>
				<input
					id="dscWritePin"
					type="number"
					disabled={loading}
					bind:value={dscWritePin}
					class="input input-sm input-bordered w-full"
				/>
			</div>
		</div>
	</div>

	<!-- Physical Zone Pins -->
	<div class="py-2 px-3 bg-base-100 rounded-lg">
		<p class="text-sm font-medium mb-2">Physical Zone GPIO Pins</p>
		<div class="grid grid-cols-2 md:grid-cols-4 gap-2">
			<!-- Zone 1 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin1">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 1 Pin</span>
					</label>
					<input
						id="zonePin1"
						type="number"
						disabled={loading || zoneDisabled1}
						bind:value={zonePin1}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled1}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 2 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin2">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 2 Pin</span>
					</label>
					<input
						id="zonePin2"
						type="number"
						disabled={loading || zoneDisabled2}
						bind:value={zonePin2}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled2}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 3 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin3">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 3 Pin</span>
					</label>
					<input
						id="zonePin3"
						type="number"
						disabled={loading || zoneDisabled3}
						bind:value={zonePin3}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled3}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 4 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin4">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 4 Pin</span>
					</label>
					<input
						id="zonePin4"
						type="number"
						disabled={loading || zoneDisabled4}
						bind:value={zonePin4}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled4}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 5 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin5">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 5 Pin</span>
					</label>
					<input
						id="zonePin5"
						type="number"
						disabled={loading || zoneDisabled5}
						bind:value={zonePin5}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled5}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 6 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin6">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 6 Pin</span>
					</label>
					<input
						id="zonePin6"
						type="number"
						disabled={loading || zoneDisabled6}
						bind:value={zonePin6}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled6}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 7 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin7">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 7 Pin</span>
					</label>
					<input
						id="zonePin7"
						type="number"
						disabled={loading || zoneDisabled7}
						bind:value={zonePin7}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled7}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>

			<!-- Zone 8 -->
			<div class="form-control bg-base-200/50 p-2 rounded-xl border border-white/5 flex flex-col justify-between">
				<div>
					<label class="label p-0 pb-1.5" for="zonePin8">
						<span class="label-text text-[11px] font-semibold text-slate-300">Zone 8 Pin</span>
					</label>
					<input
						id="zonePin8"
						type="number"
						disabled={loading || zoneDisabled8}
						bind:value={zonePin8}
						class="input input-sm input-bordered w-full text-center"
					/>
				</div>
				<div class="flex items-center justify-between mt-2 pt-1.5 border-t border-white/5">
					<span class="text-[10px] text-base-content/60 font-medium">Disabled</span>
					<input
						type="checkbox"
						disabled={loading}
						bind:checked={zoneDisabled8}
						class="checkbox checkbox-xs checkbox-error"
					/>
				</div>
			</div>
		</div>
	</div>
</div>
