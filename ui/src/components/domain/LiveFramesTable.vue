<script setup lang="ts">
// Les trames RF captees, les plus recentes d'abord, chargees par pages quand on descend. Le nom de la telecommande
// (ou du volet) et le bouton appris sont resolus depuis le statut ; chaque trame se rejoue. La premiere page se
// relit toutes les 5 s tant qu'on n'a pas defile (sinon on garde la position).
import { onMounted, onUnmounted, ref } from 'vue';
import { state, toast } from '../../store';
import { navigate } from '../../router';
import api from '../../api/index';
import type { RfFrame } from '../../api/types';
import { COMMAND, EXISTING_DEVICE, MOVE_COMMANDS, SECTION, TAB, TONE, type MoveCommand } from '../../domain';
import OpTable, { type Column } from '../base/OpTable.vue';
import OpBadge from '../base/OpBadge.vue';

const PAGE = 50;
const REFRESH_MS = 5000;
const frames = ref<RfFrame[]>([]);
const total = ref(0);
let loading = false;
async function loadMore(fromStart: boolean): Promise<void> {
  if (loading || (!fromStart && frames.value.length >= total.value && total.value)) return;
  loading = true;
  const d = await api.rf(fromStart ? 0 : frames.value.length, PAGE).catch(() => null);
  loading = false;
  if (!d) return;
  total.value = d.total;
  frames.value = fromStart ? d.frames : [...frames.value, ...d.frames];
}
let timer: ReturnType<typeof setInterval> | undefined;
onMounted(() => {
  loadMore(true);
  timer = setInterval(() => { if (!document.hidden && frames.value.length <= PAGE) loadMore(true); }, REFRESH_MS);
});
onUnmounted(() => clearInterval(timer));
defineExpose({ reload: () => loadMore(true) });

const devices = () => state.status?.volets ?? [];
const deviceOf = (serial: string): string => devices().find(d => d.serials.includes(serial))?.id ?? '';
const nameOf = (serial: string): string => state.status?.remotes[serial] || deviceOf(serial);
const BUTTON_LABEL: Record<MoveCommand, string> = { [COMMAND.up]: 'Haut', [COMMAND.down]: 'Bas', [COMMAND.stop]: 'Stop' };
function buttonLabel(serial: string, hex: string): string {
  const b = parseInt(hex, 16);
  for (const d of devices()) {
    if (!d.serials.includes(serial)) continue;
    for (const cmd of MOVE_COMMANDS) if (d.cmd[cmd]?.b === b) return BUTTON_LABEL[cmd];
  }
  return '';
}
const time = (t: number | undefined): string => new Date((t ?? 0) * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit' });
async function replay(f: RfFrame): Promise<void> {
  const r = await api.replayFrame(f.serial, f.hop ?? '').catch(() => null);
  toast(r && r.ok ? 'Trame rejouée' : 'Échec du rejeu');
}
const openDevice = (serial: string): void => navigate(`${TAB.remotes}/${SECTION.learn}/${EXISTING_DEVICE}/${encodeURIComponent(deviceOf(serial))}`);
const COLUMNS: Column[] = [
  { key: 't', label: 'Heure', mono: true }, { key: 'serial', label: 'Télécommande' }, { key: 'button', label: 'Bouton' },
  { key: 'hop', label: 'Hopping', mono: true }, { key: 'rssi', label: 'RSSI', mono: true, right: true }, { key: 'replay', label: '' },
];
</script>

<template>
  <OpTable :columns="COLUMNS" :rows="frames" :row-key="(f, i) => f.hop ?? i" height="380px" empty="Aucune trame captée." @bottom="loadMore(false)">
    <template #cell-t="{ row }">{{ time(row.t) }}</template>
    <template #cell-serial="{ row }">
      <button v-if="deviceOf(row.serial)" type="button" class="link" :title="`${row.serial} : paramétrer ${deviceOf(row.serial)}`"
              @click="openDevice(row.serial)">{{ nameOf(row.serial) }}</button>
      <span v-else-if="nameOf(row.serial)" :title="row.serial">{{ nameOf(row.serial) }}</span>
      <code v-else class="m" title="télécommande non nommée">{{ row.serial }}</code></template>
    <template #cell-button="{ row }"><OpBadge :tone="buttonLabel(row.serial, row.button) ? TONE.accent : TONE.neutral">{{ buttonLabel(row.serial, row.button) || `0x${row.button}` }}</OpBadge></template>
    <template #cell-hop="{ row }">0x{{ row.hop }}</template>
    <template #cell-rssi="{ row }">{{ row.rssi }} dBm</template>
    <template #cell-replay="{ row }"><button type="button" class="replay" title="Rejouer cette trame" aria-label="Rejouer cette trame" @click="replay(row)">▶</button></template>
  </OpTable>
  <p class="hint">{{ total ? `${Math.min(frames.length, total)} / ${total} trames${frames.length < total ? ' · descends pour charger la suite' : ''}` : '' }}</p>
</template>

<style scoped>
.m{font-family:var(--mono);font-size:.8rem;color:var(--muted)}
.replay{border:1px solid var(--border);background:var(--surface);color:var(--text);border-radius:7px;width:30px;height:26px;cursor:pointer;font-size:.7rem}
.replay:hover{border-color:var(--accent);color:var(--accent)}
.link{background:none;border:0;padding:0;color:var(--accent);font:inherit;cursor:pointer;text-decoration:underline}
</style>
