<script setup lang="ts">
// La frequence d'ecoute d'une radio : un profil nomme (Profalux, Eveno), ou une frequence personnalisee bornee a la
// bande de la radio. Les profils proposes viennent du firmware (ceux qui tombent dans la bande).
import { computed, useId } from 'vue';
import { mhz } from '../../profiles';
import type { RadioDetail } from '../../api/types';
import { RADIO_PROFILE } from '../../domain';
import OpField from '../base/OpField.vue';
import OpSelect from '../base/OpSelect.vue';
import SweepControl from './SweepControl.vue';
const profile = defineModel<string | number>('profile', { default: RADIO_PROFILE.custom });
const frequency = defineModel<number | ''>('frequency', { default: '' });
const props = defineProps<{ radio: RadioDetail }>();
const emit = defineEmits<{ apply: [khz: number] }>();
const uid = useId();
const PROFILE_NAMES: Record<string, string> = {
  [RADIO_PROFILE.profalux]: 'Profalux (868,425 MHz)', [RADIO_PROFILE.eveno]: 'Eveno (868,330 MHz)', [RADIO_PROFILE.custom]: 'Fréquence personnalisée',
};
const options = computed(() => (props.radio.profiles.length ? props.radio.profiles : [RADIO_PROFILE.custom]).map(p => ({ value: p, label: PROFILE_NAMES[p] ?? p })));
</script>

<template>
  <OpField label="Fréquence d'écoute" :control="`${uid}-p`">
    <OpSelect v-if="options.length > 1" :id="`${uid}-p`" v-model="profile" :options="options" />
    <input v-if="profile === RADIO_PROFILE.custom" v-model.number="frequency" class="op-input mono" type="number" step="1"
           :min="radio.band_low_khz" :max="radio.band_high_khz" :aria-label="`Fréquence de la radio ${radio.band} MHz en kHz`">
    <template #hint>
      Active : <b>{{ mhz(radio.freq_khz) }}</b><template v-if="profile === RADIO_PROFILE.custom"> · saisie en kHz, entre {{ radio.band_low_khz }} et {{ radio.band_high_khz }}</template>.
      Le boîtier écoute ici en permanence (suivi de position, capture). L'émission, elle, suit la fréquence de chaque appareil.
    </template>
  </OpField>
  <SweepControl :radio="radio.idx" @apply="khz => emit('apply', khz)" />
</template>
