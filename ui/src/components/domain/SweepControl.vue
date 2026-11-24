<script setup lang="ts">
// « Caler sur ma telecommande » au niveau d'une radio : le boitier balaie autour de sa porteuse pendant que la
// telecommande emet (commutateur sur P), puis propose d'appliquer la frequence trouvee.
import { ref } from 'vue';
import { withRadio } from '../../store';
import { mhz } from '../../profiles';
import api from '../../api/index';
import { BUTTON, FAILURE, SWEEP_STATUS, TONE, type Tone } from '../../domain';
import { ICON } from '../../icons';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import OpProgress from '../base/OpProgress.vue';
const props = defineProps<{ radio: number }>();
const emit = defineEmits<{ apply: [khz: number] }>();
interface SweepMessage { tone: Tone; text: string; running?: boolean; khz?: number }
const sweep = ref<SweepMessage | null>(null);
async function tune(): Promise<void> {
  sweep.value = { tone: TONE.neutral, text: 'Télécommande sur P, à 1 ou 2 m du boîtier, sans appuyer sur STOP. Laisse-la sur P jusqu\'à la fin.', running: true };
  try {
    const d = await withRadio(() => api.tuneToRemote(props.radio));
    const levels = `signal ${Math.round(d.peak_dbm)} dBm, bruit ${Math.round(d.floor_dbm)} dBm`;
    if (d.status === SWEEP_STATUS.found && d.center_khz) {
      const offset = d.center_khz - d.from_khz;
      sweep.value = { tone: TONE.ok, khz: d.center_khz, text: `Porteuse de la télécommande : ${mhz(d.center_khz)} (${offset >= 0 ? '+' : ''}${offset} kHz par rapport à la fréquence actuelle) · ${levels}` };
    } else if (d.status === SWEEP_STATUS.tooWeak) sweep.value = { tone: TONE.danger, text: `Signal trop faible (${levels}) : rapproche la télécommande, vérifie qu'elle est bien sur P, et recommence.` };
    else if (d.status === SWEEP_STATUS.outOfRange) sweep.value = { tone: TONE.danger, text: 'Porteuse hors de la plage balayée (±150 kHz) : choisis l\'autre profil radio et recommence.' };
    else sweep.value = { tone: TONE.danger, text: `Échec du balayage (${d.status})` };
  } catch (e) {
    sweep.value = e === FAILURE.busy ? null : { tone: TONE.danger, text: e === FAILURE.start ? 'Lancement impossible' : 'Pas de réponse du boîtier' };
  }
}
function apply(khz: number): void { emit('apply', khz); sweep.value = null; }
</script>

<template>
  <div class="sweep">
    <OpButton :icon="ICON.remote" small :disabled="!!sweep?.running" @click="tune">Caler sur ma télécommande</OpButton>
    <OpProgress v-if="sweep?.running" label="Recherche de la porteuse" />
    <OpStatline v-if="sweep" :tone="sweep.tone">{{ sweep.text }}
      <OpButton v-if="sweep.khz" small :variant="BUTTON.primary" @click="apply(sweep.khz)">Appliquer cette fréquence</OpButton></OpStatline>
  </div>
</template>

<style scoped>
.sweep{display:grid;gap:8px;justify-items:start}
.sweep :deep(.op-progress),.sweep :deep(.op-statline){justify-self:stretch}
</style>
