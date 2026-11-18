<script setup>
// « Caler sur ma telecommande » au niveau d'une radio : le boitier balaie autour de sa porteuse pendant
// que la telecommande emet (commutateur sur P), puis propose d'appliquer la frequence trouvee.
import { ref } from 'vue';
import { avecRadio } from '../../store.js';
import api from '../../api/index.js';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import OpProgress from '../base/OpProgress.vue';
const props = defineProps({ radio: { type: Number, required: true } });
const emit = defineEmits(['appliquer']);
const etatCal = ref(null);   // { ton, texte, cours, khz }
async function caler() {
  etatCal.value = { ton: 'neutre', texte: 'Télécommande sur P, à 1 ou 2 m du boîtier, sans appuyer sur STOP. Laisse-la sur P jusqu\'à la fin.', cours: true };
  try {
    const d = await avecRadio(() => api.caler(props.radio));
    const mhz = k => `${(k / 1000).toFixed(3).replace('.', ',')} MHz`;
    const niveaux = `signal ${Math.round(d.peak_dbm)} dBm, bruit ${Math.round(d.floor_dbm)} dBm`;
    if (d.status === 0 && d.center_khz) {
      const ecart = d.center_khz - d.from_khz;
      etatCal.value = { ton: 'ok', khz: d.center_khz, texte: `Porteuse de la télécommande : ${mhz(d.center_khz)} (${ecart >= 0 ? '+' : ''}${ecart} kHz par rapport à la fréquence actuelle) · ${niveaux}` };
    } else if (d.status === -2) etatCal.value = { ton: 'danger', texte: `Signal trop faible (${niveaux}) : rapproche la télécommande, vérifie qu'elle est bien sur P, et recommence.` };
    else if (d.status === -3) etatCal.value = { ton: 'danger', texte: 'Porteuse hors de la plage balayée (±150 kHz) : choisis l\'autre profil radio et recommence.' };
    else etatCal.value = { ton: 'danger', texte: `Échec du balayage (${d.status})` };
  } catch (e) {
    etatCal.value = e === 'occupee' ? null : { ton: 'danger', texte: e === 'lancement' ? 'Lancement impossible' : 'Pas de réponse du boîtier' };
  }
}
</script>

<template>
  <div class="sweep">
    <OpButton icon="telec" small :disabled="etatCal && etatCal.cours" @click="caler">Caler sur ma télécommande</OpButton>
    <OpProgress v-if="etatCal && etatCal.cours" label="Recherche de la porteuse" />
    <OpStatline v-if="etatCal" :tone="etatCal.ton">{{ etatCal.texte }}
      <OpButton v-if="etatCal.khz" small variant="primary" @click="emit('appliquer', etatCal.khz); etatCal = null">Appliquer cette fréquence</OpButton></OpStatline>
  </div>
</template>

<style scoped>
.sweep{display:grid;gap:8px;justify-items:start}
.sweep :deep(.op-progress),.sweep :deep(.op-statline){justify-self:stretch}
</style>
