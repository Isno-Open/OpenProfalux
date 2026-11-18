<script setup lang="ts">
// Les reglages de reception d'une radio : frequence d'ecoute (profil ou personnalisee, calage) et gain. Une carte
// par radio declaree, la primaire comme la secondaire : chaque puce est une radio entiere.
import { ref, watch } from 'vue';
import { toast } from '../../store';
import api from '../../api/index';
import type { ConfigUpdate, RadioDetail } from '../../api/types';
import { BUTTON, RADIO_PROFILE, TONE } from '../../domain';
import OpButton from '../base/OpButton.vue';
import OpBadge from '../base/OpBadge.vue';
import FreqProfile from './FreqProfile.vue';
import RxGain from './RxGain.vue';
const props = defineProps<{ radio: RadioDetail; several?: boolean }>();
const emit = defineEmits<{ saved: [] }>();
const HTTP_BAD_REQUEST = 400;
const DEFAULT_GAIN = 39;
const profile = ref<string | number>(RADIO_PROFILE.custom);
const frequency = ref<number | ''>('');
const gain = ref<string | number>(DEFAULT_GAIN);
watch(() => props.radio.idx, () => {
  profile.value = props.radio.radio_profile || RADIO_PROFILE.custom;
  frequency.value = props.radio.freq_custom_khz || props.radio.freq_khz || '';
  gain.value = props.radio.rx_gain || DEFAULT_GAIN;
}, { immediate: true });
/* La radio primaire garde les champs a plat de /api/config (cles NVS historiques) ; une secondaire porte son numero
   (radio: N). Le firmware refuse (400) une frequence hors de la plage de SA radio. */
const radioBody = (fields: ConfigUpdate): ConfigUpdate => (props.radio.primary ? fields : { radio: props.radio.idx, ...fields });
async function send(fields: ConfigUpdate, okText: string): Promise<boolean> {
  try {
    const res = await api.saveConfig(radioBody(fields));
    toast(res && res.radio_applied === 0 ? 'Enregistré, mais la radio n\'a pas pris la nouvelle fréquence (voir Diagnostic)' : okText);
    emit('saved');
    return true;
  } catch (e) {
    toast(e === HTTP_BAD_REQUEST ? 'Refusé : profil radio ou fréquence invalide, rien n\'a été enregistré' : 'Échec de l\'enregistrement');
    return false;
  }
}
async function save(): Promise<void> {
  const r = props.radio, fields: ConfigUpdate = { radio_profile: String(profile.value), rx_gain: Number(gain.value) };
  if (profile.value === RADIO_PROFILE.custom) {
    const f = Number(frequency.value);
    if (!(f >= r.band_low_khz && f <= r.band_high_khz)) { toast(`Fréquence invalide : entre ${r.band_low_khz} et ${r.band_high_khz} kHz`); return; }
    fields.freq_khz = f;
  }
  send(fields, `Radio ${r.band} MHz enregistrée`);
}
/* Porteuse trouvee par le calage, ou gain trouve par la calibration : appliques tout de suite. */
async function applyFrequency(khz: number): Promise<void> {
  if (await send({ radio_profile: RADIO_PROFILE.custom, freq_khz: khz }, `Fréquence personnalisée : ${khz} kHz`)) { profile.value = RADIO_PROFILE.custom; frequency.value = khz; }
}
const applyGain = (g: number): Promise<boolean> => send({ rx_gain: g }, 'Gain enregistré');
</script>

<template>
  <section class="radio-card">
    <header><h3 class="title">Radio {{ radio.band }} MHz</h3><OpBadge v-if="several" :tone="radio.primary ? TONE.accent : TONE.neutral">{{ radio.primary ? 'primaire' : 'secondaire' }}</OpBadge></header>
    <FreqProfile v-model:profile="profile" v-model:frequency="frequency" :radio="radio" @apply="applyFrequency" />
    <RxGain v-model="gain" :radio="radio.idx" @calibrated="applyGain" />
    <div class="row end"><OpButton :variant="BUTTON.primary" small @click="save">Enregistrer cette radio</OpButton></div>
  </section>
</template>

<style scoped>
.radio-card{display:grid;gap:14px;padding:16px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
header{display:flex;align-items:center;gap:10px}
.title{margin:0;font-size:.95rem;text-transform:none;letter-spacing:0;color:var(--text)}
</style>
