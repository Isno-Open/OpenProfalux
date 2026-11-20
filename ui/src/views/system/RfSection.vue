<script setup lang="ts">
// Systeme › RF (debug) : les trames captees en direct, et le dataset des trames distinctes memorisees par
// telecommande (avec l'ecoute RF permanente).
import { onMounted, ref } from 'vue';
import { remoteName } from '../../store';
import api from '../../api/index';
import { download } from '../../downloads';
import type { Dataset } from '../../api/types';
import { BUTTON, DOWNLOAD } from '../../domain';
import OpCard from '../../components/base/OpCard.vue';
import OpButton from '../../components/base/OpButton.vue';
import LiveFramesTable from '../../components/domain/LiveFramesTable.vue';

const MAX_SHOWN = 300;
const table = ref<InstanceType<typeof LiveFramesTable> | null>(null);
const dataset = ref<Dataset['trames'] | null>(null);
async function load(): Promise<void> {
  dataset.value = null;
  const d = await api.dataset().catch(() => null);
  dataset.value = d?.trames ?? {};
}
onMounted(load);
const exportJson = (): Promise<void> => download(DOWNLOAD.frames, 'openprofalux-trames.json', async () => JSON.stringify(await api.dataset(), null, 2));
</script>

<template>
  <div class="grid">
    <OpCard title="Trames en direct" sub="Chaque commande RF captée : télécommandes et nos rejeux. Outil de diagnostic.">
      <template #actions><OpButton small :variant="BUTTON.ghost" @click="table?.reload()">↻ Actualiser</OpButton></template>
      <LiveFramesTable ref="table" />
    </OpCard>
    <OpCard title="Dataset des trames enregistrées" sub="Hoppings distincts mémorisés par télécommande (avec « Écoute RF permanente »).">
      <div class="dataset">
        <div class="row"><OpButton small :variant="BUTTON.ghost" @click="load">↻ Charger</OpButton><OpButton small @click="exportJson">📥 Exporter (JSON)</OpButton></div>
        <p v-if="!dataset" class="hint">Chargement…</p>
        <p v-else-if="!Object.keys(dataset).length" class="hint">Aucune trame enregistrée (active « Écoute RF permanente » puis presse une télécommande).</p>
        <div v-for="(info, serial) in dataset ?? {}" :key="serial" class="remote">
          <div><b>{{ remoteName(String(serial)) }}</b> <span class="hint">· {{ info.count }} trame(s) distincte(s) · hop + bouton + t</span></div>
          <div class="hops">
            <code v-for="f in info.frames.slice(0, MAX_SHOWN)" :key="f.hop + f.t" :title="`bouton ${f.button}`">{{ f.hop }}</code>
            <span v-if="info.frames.length > MAX_SHOWN" class="hint">+{{ info.frames.length - MAX_SHOWN }} autres</span>
          </div>
        </div>
      </div>
    </OpCard>
  </div>
</template>

<style scoped>
.dataset{display:grid;gap:10px}
.remote{display:grid;gap:6px;padding:10px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.hops{display:flex;flex-wrap:wrap;gap:4px;max-height:160px;overflow:auto}
.hops code{font-family:var(--mono);font-size:.74rem;padding:1px 5px;border-radius:5px;background:var(--surface);border:1px solid var(--border)}
</style>
