<script setup lang="ts">
// Capture brute et rejeu, radio par radio : enregistre le signal d'une telecommande a code fixe que le firmware ne
// decode pas (portail, capteur), et le rejoue a l'identique. Multi : chaque pression distincte est gardee pendant
// 15 s (le rapport Support s'en sert).
import { onMounted, ref } from 'vue';
import { withRadio, radios } from '../../store';
import { mhz } from '../../profiles';
import api from '../../api/index';
import type { CaptureLast, RadioSummary } from '../../api/types';
import { BUTTON, FAILURE, TONE, type Tone } from '../../domain';
import { ICON } from '../../icons';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import OpProgress from '../base/OpProgress.vue';
const props = defineProps<{ multi?: boolean; replay?: boolean }>();
const emit = defineEmits<{ capture: [count: number] }>();
interface Message { tone: Tone; text: string; running?: boolean }
const list = ref<RadioSummary[]>([]);
const message = ref<Message | null>(null);
const last = ref<CaptureLast | null>(null);
onMounted(async () => {
  list.value = (await radios()) || [{ idx: 0, band: '' }];
  if (props.replay) { const c = await api.lastCapture().catch(() => null); if (c && c.n > 0) last.value = c; }
});
async function capture(r: RadioSummary): Promise<void> {
  message.value = { tone: TONE.neutral, running: true, text: props.multi
    ? `Presse ta télécommande ${r.band} MHz plusieurs fois pendant 15 s (chaque pression distincte est gardée)…`
    : `Presse ta télécommande ${r.band} MHz maintenant (15 s au plus)…` };
  try {
    const count = await withRadio(async () => (props.multi ? (await api.multiCapture(r.idx)).nframes : (await api.rawCapture(r.idx)).n));
    /* Un nombre strictement positif : le firmware rend un code negatif quand rien n'a ete recu (-3), et la
       capture precedente ne doit pas passer pour la nouvelle. */
    if (!(typeof count === 'number' && count > 0)) throw FAILURE.nothing;
    if (props.replay) last.value = await api.lastCapture();
    /* En mode rejeu, la ligne « Derniere capture » dit deja tout. */
    message.value = props.replay ? null : { tone: TONE.ok, text: props.multi
      ? `${count} trame${count > 1 ? 's' : ''} captée${count > 1 ? 's' : ''}. ${count > 1 ? 'Toutes seront jointes' : 'Elle sera jointe'} au rapport.`
      : 'Capturé.' };
    emit('capture', count);
  } catch (e) {
    message.value = e === FAILURE.busy ? null : { tone: TONE.danger, text: `Rien capté en 15 s sur la radio ${r.band} MHz. Vérifie la bande, l'antenne et la distance (moins d'un mètre).` };
  }
}
async function replayLast(): Promise<void> {
  const r = await withRadio(() => api.replayCapture()).catch(() => null);
  message.value = r && r.ok ? { tone: TONE.ok, text: 'Rejoué. Ton récepteur a-t-il réagi ?' } : { tone: TONE.danger, text: 'Rejeu échoué (radio occupée ?).' };
}
</script>

<template>
  <div class="capture">
    <div class="row">
      <OpButton v-for="r in list" :key="r.idx" :icon="ICON.remote" :disabled="!!message?.running" @click="capture(r)">Capturer {{ r.band ? `${r.band} MHz` : '' }}</OpButton>
    </div>
    <OpProgress v-if="message?.running" label="Capture en cours" />
    <OpStatline v-if="message" :tone="message.tone">{{ message.text }}</OpStatline>
    <OpStatline v-if="replay && last" :tone="TONE.ok">Dernière capture : <b>{{ last.n }} impulsions</b> sur la radio {{ last.band }} MHz
      à {{ mhz(last.freq_khz ?? 0) }}, RSSI {{ last.rssi }} dBm.
      <OpButton small :variant="BUTTON.primary" @click="replayLast">▶ Rejouer</OpButton></OpStatline>
  </div>
</template>

<style scoped>
.capture{display:grid;gap:10px}
</style>
