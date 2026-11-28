<script setup lang="ts">
// La tuile d'un appareil dans l'onglet Volets. Le controle est choisi d'apres le TYPE declare (profiles.json) :
// pave a trois commandes pour un volet, bouton unique pour un portail. Une centrale et une zone pilotent leurs
// volets, sans position propre.
import { computed } from 'vue';
import { state, sendCommand, remoteName } from '../../store';
import { typeOf } from '../../profiles';
import type { Device } from '../../api/types';
import { COMMAND, DEVICE_TYPE, TONE, type Command } from '../../domain';
import { ICON, type IconName } from '../../icons';
import OpIcon from '../base/OpIcon.vue';
import OpBadge from '../base/OpBadge.vue';
import ShutterControl from './ShutterControl.vue';
import GateControl from './GateControl.vue';
import PositionBar from './PositionBar.vue';
import BatteryBadge from './BatteryBadge.vue';
import SignalBars from './SignalBars.vue';

const props = defineProps<{
  device: Device;
  arranging?: boolean;    // mode reorganisation : pilotage neutralise
  first?: boolean;
  last?: boolean;
  hovered?: boolean;      // une autre tuile glisse au-dessus (cible du depot)
}>();
defineEmits<{ move: [step: number] }>();

const status = computed(() => state.status);
const type = computed(() => typeOf(props.device));
const isCentral = computed(() => !!props.device.central);
const isZone = computed(() => !isCentral.value && !!props.device.zone);
const isGate = computed(() => type.value.key === DEVICE_TYPE.gate);
const icon = computed<IconName>(() => (isCentral.value ? ICON.central : isZone.value ? ICON.zone : isGate.value ? ICON.gate : ICON.shutter));
/* Position affichee seulement avec l'ecoute permanente : sans elle, un appui sur la vraie telecommande
   desynchronise l'estimation, et un pourcentage faux est pire qu'aucun. */
const positional = computed(() => !!status.value?.listening && !!type.value.positional && !isCentral.value && !isZone.value);
const sending = computed<Command | ''>(() => (state.sending && state.sending.id === props.device.id ? state.sending.cmd : ''));
const busy = computed(() => state.radioBusy || !!props.arranging);
const listOf = (members: string | undefined): string => (members ? members.split(',').join(', ') : 'aucun');
const capturedCount = computed(() => Object.keys(props.device.rejeu_cmds ?? {}).length);
const signal = computed(() => {
  for (const f of status.value?.rf ?? []) if (props.device.serials.includes(f.serial)) return f.rssi;
  return null;
});
const gateLabel = computed(() => type.value.capture[0]?.label);
const send = (cmd: Command, value?: number): Promise<void> =>
  sendCommand(value == null ? { id: props.device.id, cmd } : { id: props.device.id, cmd, value });
</script>

<template>
  <article class="tile" :class="{ arranging, hovered }">
    <header class="top">
      <span class="name">
        <span v-if="arranging" class="grip" title="Glisser pour déplacer" aria-hidden="true">⠿</span>
        <OpIcon :name="icon" size="1.35em" />
        <span class="text">{{ device.id }}</span>
        <OpBadge v-if="isCentral" :tone="TONE.accent">centrale</OpBadge>
        <OpBadge v-else-if="isZone" :tone="TONE.accent">zone</OpBadge>
        <OpBadge v-else-if="isGate">{{ device.marque || 'portail' }}</OpBadge>
      </span>
      <span v-if="arranging" class="arrows">
        <button type="button" :disabled="first" aria-label="Monter" title="Monter" @click="$emit('move', -1)">▲</button>
        <button type="button" :disabled="last" aria-label="Descendre" title="Descendre" @click="$emit('move', 1)">▼</button>
      </span>
    </header>

    <GateControl v-if="isGate" :label="gateLabel" :sending="sending === COMMAND.step" :disabled="busy" @command="cmd => send(cmd)" />
    <ShutterControl v-else :labels="type.dpad" :sending="sending" :disabled="busy" @command="cmd => send(cmd)" />

    <PositionBar v-if="positional" :position="device.position" :disabled="busy" @choose="p => send(COMMAND.position, p)" />

    <footer class="details">
      <template v-if="isCentral">volets : {{ listOf(device.members) }}</template>
      <template v-else-if="isGate">rejeu de la télécommande d'origine · {{ capturedCount }} commande{{ capturedCount > 1 ? 's' : '' }} captée{{ capturedCount > 1 ? 's' : '' }}</template>
      <template v-else>
        <span v-if="device.virt">identité <code>{{ remoteName(device.virt_serial ?? '') }}</code><template v-if="device.serials.length"> · télécommandes :</template></span>
        <span v-for="s in device.serials" :key="s" class="remote"><code>{{ remoteName(s) }}</code><BatteryBadge :battery="status?.remote_battery?.[s] ?? null" /></span>
        <span v-if="!device.serials.length && !device.virt">aucune télécommande</span>
      </template>
      <span v-if="signal != null" class="received">reçu <SignalBars :rssi="signal" /></span>
      <span v-if="isZone" class="line">suiveurs : {{ listOf(device.members) }}</span>
      <span v-else-if="device.follows?.length" class="line">suit : {{ device.follows.join(', ') }}</span>
    </footer>
  </article>
</template>

<style scoped>
.tile{display:grid;gap:14px;background:var(--surface);background-image:var(--surface-grad);border:1px solid var(--border);border-radius:var(--radius);padding:20px;box-shadow:var(--shadow);min-width:0}
.top{display:flex;align-items:center;justify-content:space-between;gap:10px;min-height:28px}
.name{display:flex;align-items:center;gap:9px;font-weight:650;font-size:1.08rem;min-width:0}
.text{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.arrows{display:inline-flex;gap:6px}
.arrows button{width:34px;height:30px;border:1px solid var(--border);background:var(--surface-2);color:var(--text);border-radius:8px;cursor:pointer}
.arrows button:disabled{opacity:.35;cursor:default}
.details{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;font-size:.76rem;color:var(--muted)}
.details code{background:var(--surface-2);padding:1px 6px;border-radius:5px;border:1px solid var(--border)}
.remote{display:inline-flex;align-items:center;gap:5px}
.arranging{outline:1px dashed var(--accent-line);outline-offset:3px;cursor:grab}
.hovered{outline:2px solid var(--accent)}
.grip{color:var(--muted);cursor:grab;font-size:1.1rem;line-height:1}
.received{display:inline-flex;align-items:center;gap:5px}
.line{flex-basis:100%}
</style>
