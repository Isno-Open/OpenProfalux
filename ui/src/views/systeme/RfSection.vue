<script setup>
// Systeme › RF (debug) : les trames captees en direct, et le dataset des trames distinctes memorisees par
// telecommande (avec l'ecoute RF permanente).
import { onMounted, ref } from 'vue';
import { nomTelec } from '../../store.js';
import api from '../../api/index.js';
import { telecharger } from '../../fichiers.js';
import OpCard from '../../components/base/OpCard.vue';
import OpButton from '../../components/base/OpButton.vue';
import LiveFramesTable from '../../components/metier/LiveFramesTable.vue';

const MAX_AFFICHES = 300;
const table = ref(null);
const dataset = ref(null);   // { serial: { count, frames: [{ hop, button, t }] } }
async function charger() {
  dataset.value = null;
  const d = await api.dataset().catch(() => null);
  dataset.value = (d && d.trames) || {};
}
onMounted(charger);
const exporter = () => telecharger('trames', 'openprofalux-trames.json', async () => JSON.stringify(await api.dataset(), null, 2));
</script>

<template>
  <div class="grille">
    <OpCard title="Trames en direct" sub="Chaque commande RF captée : télécommandes et nos rejeux. Outil de diagnostic.">
      <template #actions><OpButton small variant="ghost" @click="table && table.recharger()">↻ Actualiser</OpButton></template>
      <LiveFramesTable ref="table" />
    </OpCard>
    <OpCard title="Dataset des trames enregistrées" sub="Hoppings distincts mémorisés par télécommande (avec « Écoute RF permanente »).">
      <div class="dataset">
        <div class="rangee"><OpButton small variant="ghost" @click="charger">↻ Charger</OpButton><OpButton small @click="exporter">📥 Exporter (JSON)</OpButton></div>
        <p v-if="!dataset" class="hint">Chargement…</p>
        <p v-else-if="!Object.keys(dataset).length" class="hint">Aucune trame enregistrée (active « Écoute RF permanente » puis presse une télécommande).</p>
        <div v-for="(info, serial) in dataset || {}" :key="serial" class="telec">
          <div><b>{{ nomTelec(serial) }}</b> <span class="hint">· {{ info.count }} trame(s) distincte(s) · hop + bouton + t</span></div>
          <div class="hops">
            <code v-for="f in (info.frames || []).slice(0, MAX_AFFICHES)" :key="f.hop + f.t" :title="`bouton ${f.button}`">{{ f.hop }}</code>
            <span v-if="(info.frames || []).length > MAX_AFFICHES" class="hint">+{{ info.frames.length - MAX_AFFICHES }} autres</span>
          </div>
        </div>
      </div>
    </OpCard>
  </div>
</template>

<style scoped>
.dataset{display:grid;gap:10px}
.telec{display:grid;gap:6px;padding:10px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.hops{display:flex;flex-wrap:wrap;gap:4px;max-height:160px;overflow:auto}
.hops code{font-family:var(--mono);font-size:.74rem;padding:1px 5px;border-radius:5px;background:var(--surface);border:1px solid var(--border)}
</style>
