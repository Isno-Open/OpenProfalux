<script setup>
// Calibrer la position : le moteur ne renvoie pas sa position, on mesure le temps de course pour estimer
// le pourcentage. Au chronometre (demarrer, puis « arrive ») ou a la main. Le chronometre part AU CLIC, et
// « Arrive » envoie STOP : comme l'interface d'avant, sans attendre la radio (un STOP ne doit pas etre avale).
import { computed, onUnmounted, ref, watch } from 'vue';
import { etat, rafraichir, toast, typeDe } from '../../store.js';
import { aller } from '../../router.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSelect from '../../components/base/OpSelect.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';

const volets = computed(() => ((etat.statut && etat.statut.volets) || []).filter(v => !v.central && !v.zone && typeDe(v).positional));
const id = ref('');
watch(volets, l => { if (!id.value && l.length) id.value = l[0].id; }, { immediate: true });
const v = computed(() => volets.value.find(x => x.id === id.value) || null);
const temps = ref({ up: '', down: '' });   // en secondes, texte des champs
/* Les champs reprennent les temps enregistres quand on change de volet, pas a chaque relecture du statut
   (sinon une saisie ou un chrono en cours serait efface). */
watch(() => !!v.value && id.value, () => {
  const x = v.value;
  if (x) temps.value = { up: x.travel_up_ms ? (x.travel_up_ms / 1000).toFixed(1) : '', down: x.travel_down_ms ? (x.travel_down_ms / 1000).toFixed(1) : '' };
}, { immediate: true });

const chrono = ref(null);    // { sens, debut }
const ecoule = ref(0);
let minuteur = null;
onUnmounted(() => clearInterval(minuteur));
function demarrer(sens) {
  chrono.value = { sens, debut: performance.now() };
  ecoule.value = 0;
  clearInterval(minuteur);
  minuteur = setInterval(() => { ecoule.value = (performance.now() - chrono.value.debut) / 1000; }, 100);
  api.ordre({ id: id.value, cmd: sens }).catch(() => toast('Radio occupée, réessaie dans un instant'));
}
function arrive() {
  clearInterval(minuteur);
  temps.value[chrono.value.sens] = ((performance.now() - chrono.value.debut) / 1000).toFixed(1);
  chrono.value = null;
  api.ordre({ id: id.value, cmd: 'stop' }).catch(() => {});
}
/* Bornes du temps de course, declarees par le type dans profiles.json (travel_s). */
const bornes = computed(() => (v.value && typeDe(v.value).travel_s) || { min: 1, max: 180 });
const valide = computed(() => Number(temps.value.up) > 0 && Number(temps.value.down) > 0);
async function enregistrer() {
  const { min, max } = bornes.value;
  const hors = ['up', 'down'].some(k => Number(temps.value[k]) < min || Number(temps.value[k]) > max);
  if (hors) { toast(`Temps de course entre ${min} et ${max} s`); return; }
  await api.calibrer({ id: id.value, travel_up_ms: Math.round(Number(temps.value.up) * 1000), travel_down_ms: Math.round(Number(temps.value.down) * 1000) });
  toast('Calibration enregistrée');
  rafraichir();
}
const s = x => `${x.toFixed(1).replace('.', ',')} s`;
</script>

<template>
  <OpCard title="Calibrer la position" sub="Le moteur ne renvoie pas sa position : on mesure le temps de course pour estimer le pourcentage.">
    <div class="calib">
      <OpStatline v-if="etat.statut && !etat.statut.listening" tone="alerte"><b>Suivi de position inactif.</b> Active « Écoute RF permanente »
        dans <button type="button" class="lien-retour" @click="aller('sys/device')">Système › Appareil</button> : sans elle, la calibration ne sert à rien.</OpStatline>
      <OpField label="Volet" pour="calib-volet">
        <OpSelect id="calib-volet" v-model="id" :options="volets.map(x => ({ value: x.id, label: x.id }))" />
      </OpField>
      <ol class="etapes">
        <li>Volet en <b>butée haute</b> : <b>Démarrer la descente</b>, puis <b>Arrivé en bas</b> quand il s'arrête.</li>
        <li>Pareil pour la montée, depuis la butée basse.</li>
      </ol>
      <div class="chronos">
        <div v-for="c in [{ sens: 'down', sym: '▼', go: 'Démarrer la descente', fin: 'Arrivé en bas' }, { sens: 'up', sym: '▲', go: 'Démarrer la montée', fin: 'Arrivé en haut' }]"
             :key="c.sens" class="chrono" :class="{ cours: chrono && chrono.sens === c.sens }">
          <OpButton :disabled="!!chrono || etat.radioOccupee || !v" @click="demarrer(c.sens)">{{ c.sym }} {{ c.go }}</OpButton>
          <OpButton :variant="chrono && chrono.sens === c.sens ? 'primary' : 'normal'" :disabled="!chrono || chrono.sens !== c.sens" @click="arrive">
            {{ c.fin }} · <b class="mono">{{ chrono && chrono.sens === c.sens ? s(ecoule) : (temps[c.sens] ? `${String(temps[c.sens]).replace('.', ',')} s` : '0,0 s') }}</b></OpButton>
        </div>
      </div>
      <OpField label="Ou saisis les temps à la main (secondes)">
        <div class="grille deux">
          <input v-model="temps.up" class="op-input mono" type="number" step="0.1" min="0" placeholder="Montée (s)" aria-label="Temps de montée en secondes">
          <input v-model="temps.down" class="op-input mono" type="number" step="0.1" min="0" placeholder="Descente (s)" aria-label="Temps de descente en secondes">
        </div>
      </OpField>
      <div class="rangee fin"><OpButton variant="primary" :disabled="!valide || !!chrono" @click="enregistrer">Enregistrer la calibration</OpButton></div>
    </div>
  </OpCard>
</template>

<style scoped>
.calib{display:grid;gap:14px}
.etapes{margin:0;padding-left:1.3em;display:grid;gap:4px;font-size:.88rem;color:var(--muted)}
.chronos{display:grid;gap:9px}
.chrono{display:flex;flex-wrap:wrap;gap:9px}
.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
</style>
