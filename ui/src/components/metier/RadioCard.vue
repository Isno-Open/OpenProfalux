<script setup>
// Les reglages de reception d'une radio : frequence d'ecoute (profil ou personnalisee, calage) et gain.
// Une carte par radio declaree, la primaire comme la secondaire : chaque puce est une radio entiere.
import { ref, watch } from 'vue';
import { toast } from '../../store.js';
import api from '../../api/index.js';
import OpButton from '../base/OpButton.vue';
import OpBadge from '../base/OpBadge.vue';
import FreqProfile from './FreqProfile.vue';
import RxGain from './RxGain.vue';
const props = defineProps({ radio: { type: Object, required: true }, plusieurs: Boolean });
const emit = defineEmits(['enregistre']);
const profil = ref('custom');
const freq = ref('');
const gain = ref(39);
watch(() => props.radio.idx, () => {
  profil.value = props.radio.radio_profile || 'custom';
  freq.value = props.radio.freq_custom_khz || props.radio.freq_khz || '';
  gain.value = props.radio.rx_gain || 39;
}, { immediate: true });
/* La radio primaire garde les champs a plat de /api/config (cles NVS historiques) ; une secondaire porte
   son numero (radio: N). Le firmware refuse (400) une frequence hors de la plage de SA radio. */
const corpsRadio = champs => (props.radio.primary ? champs : { radio: props.radio.idx, ...champs });
async function envoyer(champs, ok) {
  try {
    const res = await api.enregistrerConfig(corpsRadio(champs));
    toast(res && res.radio_applied === 0 ? 'Enregistré, mais la radio n\'a pas pris la nouvelle fréquence (voir Diagnostic)' : ok);
    emit('enregistre');
    return true;
  } catch (e) {
    toast(e === 400 ? 'Refusé : profil radio ou fréquence invalide, rien n\'a été enregistré' : 'Échec de l\'enregistrement');
    return false;
  }
}
async function enregistrer() {
  const r = props.radio, champs = { radio_profile: profil.value, rx_gain: gain.value };
  if (profil.value === 'custom') {
    const f = Number(freq.value);
    if (!(f >= r.band_low_khz && f <= r.band_high_khz)) { toast(`Fréquence invalide : entre ${r.band_low_khz} et ${r.band_high_khz} kHz`); return; }
    champs.freq_khz = f;
  }
  envoyer(champs, `Radio ${r.band} MHz enregistrée`);
}
/* Porteuse trouvee par le calage, ou gain trouve par la calibration : appliques tout de suite. */
async function appliquerFrequence(khz) {
  if (await envoyer({ radio_profile: 'custom', freq_khz: khz }, `Fréquence personnalisée : ${khz} kHz`)) { profil.value = 'custom'; freq.value = khz; }
}
const appliquerGain = g => envoyer({ rx_gain: g }, `Gain enregistré`);
</script>

<template>
  <section class="radio-card">
    <header><h3 class="titre">Radio {{ radio.band }} MHz</h3><OpBadge v-if="plusieurs" :tone="radio.primary ? 'accent' : 'neutre'">{{ radio.primary ? 'primaire' : 'secondaire' }}</OpBadge></header>
    <FreqProfile v-model:profil="profil" v-model:freq="freq" :radio="radio" @appliquer="appliquerFrequence" />
    <RxGain v-model="gain" :radio="radio.idx" @calibre="appliquerGain" />
    <div class="rangee fin"><OpButton variant="primary" small @click="enregistrer">Enregistrer cette radio</OpButton></div>
  </section>
</template>

<style scoped>
.radio-card{display:grid;gap:14px;padding:16px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
header{display:flex;align-items:center;gap:10px}
.titre{margin:0;font-size:.95rem;text-transform:none;letter-spacing:0;color:var(--text)}
</style>
