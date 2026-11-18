<script setup>
// Capture brute et rejeu, radio par radio : enregistre le signal d'une telecommande a code fixe que le
// firmware ne decode pas (portail, capteur), et le rejoue a l'identique. Multi : chaque pression
// distincte est gardee pendant 15 s (le rapport Support s'en sert).
import { onMounted, ref } from 'vue';
import { avecRadio, radios } from '../../store.js';
import api from '../../api/index.js';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import OpProgress from '../base/OpProgress.vue';
const props = defineProps({ multi: Boolean, rejeu: Boolean });
const emit = defineEmits(['capture']);
const liste = ref([]);
const statut = ref(null);   // { ton, texte, cours }
const derniere = ref(null);
onMounted(async () => {
  liste.value = (await radios()) || [{ idx: 0, band: '' }];
  if (props.rejeu) { const c = await api.derniereCapture().catch(() => null); if (c && c.n) derniere.value = c; }
});
const mhz = k => `${(k / 1000).toFixed(3).replace('.', ',')} MHz`;
async function capturer(r) {
  statut.value = { ton: 'neutre', cours: true, texte: props.multi
    ? `Presse ta télécommande ${r.band} MHz plusieurs fois pendant 15 s (chaque pression distincte est gardée)…`
    : `Presse ta télécommande ${r.band} MHz maintenant (15 s au plus)…` };
  try {
    const res = await avecRadio(() => (props.multi ? api.captureMulti(r.idx) : api.captureBrute(r.idx)));
    const n = props.multi ? res.nframes : res.n;
    if (!n) throw 'rien';
    if (props.rejeu) derniere.value = await api.derniereCapture();
    /* En mode rejeu, la ligne « Derniere capture » dit deja tout. */
    statut.value = props.rejeu ? null : { ton: 'ok', texte: props.multi ? `${n} trame${n > 1 ? 's' : ''} captée${n > 1 ? 's' : ''}. ${n > 1 ? 'Toutes seront jointes' : 'Elle sera jointe'} au rapport.` : 'Capturé.' };
    emit('capture', n);
  } catch (e) {
    statut.value = e === 'occupee' ? null : { ton: 'danger', texte: `Rien capté en 15 s sur la radio ${r.band} MHz. Vérifie la bande, l'antenne et la distance (moins d'un mètre).` };
  }
}
async function rejouer() {
  const r = await avecRadio(() => api.rejouerCapture()).catch(() => null);
  statut.value = r && r.ok ? { ton: 'ok', texte: 'Rejoué. Ton récepteur a-t-il réagi ?' } : { ton: 'danger', texte: 'Rejeu échoué (radio occupée ?).' };
}
</script>

<template>
  <div class="capture">
    <div class="rangee">
      <OpButton v-for="r in liste" :key="r.idx" icon="telec" :disabled="statut && statut.cours" @click="capturer(r)">Capturer {{ r.band ? `${r.band} MHz` : '' }}</OpButton>
    </div>
    <OpProgress v-if="statut && statut.cours" label="Capture en cours" />
    <OpStatline v-if="statut" :tone="statut.ton">{{ statut.texte }}</OpStatline>
    <OpStatline v-if="rejeu && derniere" tone="ok">Dernière capture : <b>{{ derniere.n }} impulsions</b> sur la radio {{ derniere.band }} MHz
      à {{ mhz(derniere.freq_khz) }}, RSSI {{ derniere.rssi }} dBm.
      <OpButton small variant="primary" @click="rejouer">▶ Rejouer</OpButton></OpStatline>
  </div>
</template>

<style scoped>
.capture{display:grid;gap:10px}
</style>
