<script lang="ts">
// Calibrer la position : le moteur ne renvoie pas sa position, on mesure le temps de course pour estimer le
// pourcentage. Au chronometre (demarrer, puis « arrive ») ou a la main. Le chronometre part AU CLIC, et « Arrive »
// envoie STOP : comme l'interface d'avant, sans attendre la radio (un STOP ne doit pas etre avale).
import { ref } from 'vue';
import { COMMAND } from '../../domain';

type Direction = typeof COMMAND.up | typeof COMMAND.down;
/* Le chronometre en cours vit hors de l'ecran, qui est demonte a chaque changement d'onglet : le volet roule, et
   « Arrive » doit encore pouvoir arreter le chrono et envoyer STOP au retour, comme dans l'ancienne interface, ou le
   panneau restait dans la page. Il garde le volet mesure : STOP et le temps vont toujours a celui-la. */
const stopwatch = ref<{ direction: Direction; start: number; id: string } | null>(null);
</script>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, reactive, watch } from 'vue';
import { state, refresh, toast, typeOf } from '../../store';
import { navigate } from '../../router';
import api from '../../api/index';
import { BUTTON, SECTION, TAB, TONE } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSelect from '../../components/base/OpSelect.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';

const DEFAULT_TRAVEL_S = { min: 1, max: 180 };
const TICK_MS = 100;
const shutters = computed(() => (state.status?.volets ?? []).filter(d => !d.central && !d.zone && typeOf(d).positional));
const id = ref<string | number>(stopwatch.value?.id ?? '');
watch(shutters, list => { const first = list[0]; if (!id.value && first) id.value = first.id; }, { immediate: true });
const shutter = computed(() => shutters.value.find(x => x.id === id.value) ?? null);
const times = reactive<Record<Direction, string>>({ up: '', down: '' });   // en secondes, texte des champs
/* Les champs reprennent les temps enregistres quand on change de volet, pas a chaque relecture du statut (sinon
   une saisie ou un chrono en cours serait efface). */
watch(() => !!shutter.value && id.value, () => {
  const x = shutter.value;
  if (x) Object.assign(times, { up: x.travel_up_ms ? (x.travel_up_ms / 1000).toFixed(1) : '', down: x.travel_down_ms ? (x.travel_down_ms / 1000).toFixed(1) : '' });
}, { immediate: true });

const elapsed = ref(0);
let timer: ReturnType<typeof setInterval> | undefined;
function follow(start: number): void {
  clearInterval(timer);
  elapsed.value = (performance.now() - start) / 1000;
  timer = setInterval(() => { elapsed.value = (performance.now() - start) / 1000; }, TICK_MS);
}
onMounted(() => { if (stopwatch.value) follow(stopwatch.value.start); });   // chrono lance avant un changement d'onglet
onUnmounted(() => clearInterval(timer));
function start(direction: Direction): void {
  const run = { direction, start: performance.now(), id: String(id.value) };
  stopwatch.value = run;
  follow(run.start);
  api.sendShutter({ id: run.id, cmd: direction }).catch(() => toast('Radio occupée, réessaie dans un instant'));
}
function arrived(): void {
  clearInterval(timer);
  const run = stopwatch.value;
  if (!run) return;
  if (String(id.value) === run.id) times[run.direction] = ((performance.now() - run.start) / 1000).toFixed(1);
  stopwatch.value = null;
  api.sendShutter({ id: run.id, cmd: COMMAND.stop }).catch(() => {});
}
/* Bornes du temps de course, declarees par le type dans profiles.json (travel_s). */
const bounds = computed(() => (shutter.value ? typeOf(shutter.value).travel_s : undefined) ?? DEFAULT_TRAVEL_S);
const valid = computed(() => Number(times.up) > 0 && Number(times.down) > 0);
async function save(): Promise<void> {
  const { min, max } = bounds.value;
  const outOfBounds = [times.up, times.down].some(t => Number(t) < min || Number(t) > max);
  if (outOfBounds) { toast(`Temps de course entre ${min} et ${max} s`); return; }
  try {
    await api.calibrate({ id: String(id.value), travel_up_ms: Math.round(Number(times.up) * 1000), travel_down_ms: Math.round(Number(times.down) * 1000) });
    toast('Calibration enregistrée');
  } catch { toast('Refusé : calibration non enregistrée'); }
  refresh();
}
const seconds = (x: number): string => `${x.toFixed(1).replace('.', ',')} s`;
const STOPWATCHES: { direction: Direction; symbol: string; go: string; stop: string }[] = [
  { direction: COMMAND.down, symbol: '▼', go: 'Démarrer la descente', stop: 'Arrivé en bas' },
  { direction: COMMAND.up, symbol: '▲', go: 'Démarrer la montée', stop: 'Arrivé en haut' },
];
const shown = (direction: Direction): string => (stopwatch.value?.direction === direction ? seconds(elapsed.value)
  : (times[direction] ? `${String(times[direction]).replace('.', ',')} s` : '0,0 s'));
</script>

<template>
  <OpCard title="Calibrer la position" sub="Le moteur ne renvoie pas sa position : on mesure le temps de course pour estimer le pourcentage.">
    <div class="calibration">
      <OpStatline v-if="state.status && !state.status.listening" :tone="TONE.warning"><b>Suivi de position inactif.</b> Active « Écoute RF permanente »
        dans <button type="button" class="back-link" @click="navigate(`${TAB.system}/${SECTION.device}`)">Système › Appareil</button> : sans elle, la calibration ne sert à rien.</OpStatline>
      <OpField label="Volet" control="calib-shutter">
        <OpSelect id="calib-shutter" v-model="id" :options="shutters.map(x => ({ value: x.id, label: x.id }))" :disabled="!!stopwatch" />
      </OpField>
      <ol class="steps">
        <li>Volet en <b>butée haute</b> : <b>Démarrer la descente</b>, puis <b>Arrivé en bas</b> quand il s'arrête.</li>
        <li>Pareil pour la montée, depuis la butée basse.</li>
      </ol>
      <div class="stopwatches">
        <div v-for="c in STOPWATCHES" :key="c.direction" class="stopwatch" :class="{ running: stopwatch?.direction === c.direction }">
          <OpButton :disabled="!!stopwatch || state.radioBusy || !shutter" @click="start(c.direction)">{{ c.symbol }} {{ c.go }}</OpButton>
          <OpButton :variant="stopwatch?.direction === c.direction ? BUTTON.primary : BUTTON.normal" :disabled="stopwatch?.direction !== c.direction" @click="arrived">
            {{ c.stop }} · <b class="mono">{{ shown(c.direction) }}</b></OpButton>
        </div>
      </div>
      <OpField label="Ou saisis les temps à la main (secondes)">
        <div class="grid two">
          <input v-model="times.up" class="op-input mono" type="number" step="0.1" min="0" placeholder="Montée (s)" aria-label="Temps de montée en secondes">
          <input v-model="times.down" class="op-input mono" type="number" step="0.1" min="0" placeholder="Descente (s)" aria-label="Temps de descente en secondes">
        </div>
      </OpField>
      <div class="row end"><OpButton :variant="BUTTON.primary" :disabled="!valid || !!stopwatch" @click="save">Enregistrer la calibration</OpButton></div>
    </div>
  </OpCard>
</template>

<style scoped>
.calibration{display:grid;gap:14px}
.steps{margin:0;padding-left:1.3em;display:grid;gap:4px;font-size:.88rem;color:var(--muted)}
.stopwatches{display:grid;gap:9px}
.stopwatch{display:flex;flex-wrap:wrap;gap:9px}
.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
</style>
