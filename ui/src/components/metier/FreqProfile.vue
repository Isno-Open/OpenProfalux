<script setup>
// La frequence d'ecoute d'une radio : un profil nomme (Profalux, Eveno), ou une frequence personnalisee
// bornee a la bande de la radio. Les profils proposes viennent du firmware (ceux qui tombent dans la bande).
import { computed, useId } from 'vue';
import OpField from '../base/OpField.vue';
import OpSelect from '../base/OpSelect.vue';
import SweepControl from './SweepControl.vue';
const profil = defineModel('profil', { type: String, default: 'custom' });
const freq = defineModel('freq', { type: [Number, String], default: '' });
const props = defineProps({ radio: { type: Object, required: true } });
const uid = useId();
const NOMS = { profalux: 'Profalux (868,425 MHz)', eveno: 'Eveno (868,330 MHz)', custom: 'Fréquence personnalisée' };
const options = computed(() => (props.radio.profiles || ['custom']).map(p => ({ value: p, label: NOMS[p] || p })));
const mhz = k => `${(k / 1000).toFixed(3).replace('.', ',')} MHz`;
const emit = defineEmits(['appliquer']);
const appliquer = khz => emit('appliquer', khz);
</script>

<template>
  <OpField label="Fréquence d'écoute" :pour="`${uid}-p`">
    <OpSelect v-if="options.length > 1" :id="`${uid}-p`" v-model="profil" :options="options" />
    <input v-if="profil === 'custom'" v-model.number="freq" class="op-input mono" type="number" step="1"
           :min="radio.band_low_khz" :max="radio.band_high_khz" :aria-label="`Fréquence de la radio ${radio.band} MHz en kHz`">
    <template #hint>
      Active : <b>{{ mhz(radio.freq_khz) }}</b><template v-if="profil === 'custom'"> · saisie en kHz, entre {{ radio.band_low_khz }} et {{ radio.band_high_khz }}</template>.
      Le boîtier écoute ici en permanence (suivi de position, capture). L'émission, elle, suit la fréquence de chaque appareil.
    </template>
  </OpField>
  <SweepControl :radio="radio.idx" @appliquer="appliquer" />
</template>
