<script setup lang="ts">
// Le gain de reception d'une radio (AGCCTRL2), et la calibration automatique : le boitier essaie chaque gain
// pendant qu'on presse la telecommande, et garde le premier qui capte une trame valide.
import { ref, useId } from 'vue';
import { withRadio } from '../../store';
import api from '../../api/index';
import { FAILURE } from '../../domain';
import OpField from '../base/OpField.vue';
import OpSelect from '../base/OpSelect.vue';
import OpButton from '../base/OpButton.vue';
const gain = defineModel<string | number>({ default: 39 });
/* save enregistre le gain trouve : une fonction appelee directement, pas un evenement. La calibration dure ~25 s ; si
   l'ecran est quitte entre-temps, Vue ignore l'evenement d'un composant demonte, et le gain, que le firmware n'applique
   qu'en RAM, serait perdu au redemarrage. */
const props = defineProps<{ radio: number; save: (gain: number) => Promise<boolean> }>();
const uid = useId();
const GAINS = [
  { value: 7, label: '0x07 · gain max (le plus sensible, capte aussi le bruit)' },
  { value: 39, label: '0x27 · défaut (−9 dB)' },
  { value: 47, label: '0x2F · gain réduit' },
  { value: 55, label: '0x37 · gain réduit +' },
  { value: 63, label: '0x3F · mordant (−17 dB), si signal fort mais bruité' },
];
const hex = (n: number | undefined): string => `0x${Number(n ?? 0).toString(16).toUpperCase().padStart(2, '0')}`;
const message = ref('');
const running = ref(false);
async function calibrate(): Promise<void> {
  running.value = true; message.value = 'Appuie sur ta télécommande, et répète les appuis…';
  try {
    const r = await withRadio(() => api.calibrateGain(props.radio, g => { message.value = `Appuie… (essai du gain ${hex(g)})`; }));
    const result = r.result ?? Number(gain.value);
    gain.value = result;
    message.value = await props.save(result) ? `✓ Calibré : gain ${hex(result)} (enregistré)` : `Calibré : gain ${hex(result)}, mais pas enregistré.`;
  } catch (e) {
    message.value = e === FAILURE.busy ? '' : e === FAILURE.start ? '✗ Calibration non lancée par le boîtier : réessaie dans un instant.'
      : '✗ Aucune trame captée. Vérifie l\'antenne, ou essaie un autre module CC1101.';
  }
  finally { running.value = false; }
}
</script>

<template>
  <OpField label="Gain de réception (AGCCTRL2)" :control="`${uid}-g`"
           hint="Réglage de diagnostic. La calibration dure environ 25 s : presse ta télécommande en continu, à moins d'un mètre.">
    <OpSelect :id="`${uid}-g`" v-model="gain" :options="GAINS" />
    <div class="row">
      <OpButton small :disabled="running" @click="calibrate">Calibrer la réception (auto)</OpButton>
      <span class="hint" role="status">{{ message }}</span>
    </div>
  </OpField>
</template>
