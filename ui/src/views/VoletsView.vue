<script setup>
// Onglet Volets : la grille des appareils, et leur rangement : glisser-deposer a la souris, fleches au
// doigt (le glisser HTML5 ne marche pas sur telephone). Le pilotage est neutralise pendant le rangement.
import { computed, ref } from 'vue';
import { etat, ranger } from '../store.js';
import ShutterTile from '../components/metier/ShutterTile.vue';
import OpButton from '../components/base/OpButton.vue';

const rangement = ref(false);
/* Ordre choisi par l'utilisateur (champ order du firmware) ; -1 = jamais range, apres les autres. */
const tries = computed(() => [...((etat.statut && etat.statut.volets) || [])].sort((a, b) => {
  const oa = a.order ?? -1, ob = b.order ?? -1;
  if (oa >= 0 && ob >= 0) return oa - ob;
  if (oa >= 0) return -1;
  if (ob >= 0) return 1;
  return (b.central ? 1 : 0) - (a.central ? 1 : 0);
}));
function deplacer(i, sens) {
  const ids = tries.value.map(v => v.id);
  const j = i + sens;
  if (j < 0 || j >= ids.length) return;
  [ids[i], ids[j]] = [ids[j], ids[i]];
  ranger(ids);
}
/* Depot : la tuile glissee prend la place de la cible (insertion, le reste se decale). */
const glisse = ref(null), cible = ref(null);
function deposer(id) {
  const ids = tries.value.map(v => v.id);
  const de = ids.indexOf(glisse.value);
  if (de < 0 || glisse.value === id) return;
  ids.splice(de, 1);
  ids.splice(ids.indexOf(id), 0, glisse.value);
  ranger(ids);
}
</script>

<template>
  <div class="volets">
    <div v-if="tries.length > 1" class="rangee fin">
      <OpButton @click="rangement = !rangement">{{ rangement ? '✓ Terminé' : '↕ Réorganiser' }}</OpButton>
    </div>
    <p v-if="rangement" class="hint"><b>Glisse une carte</b> à l'endroit voulu, ou utilise les flèches ▲▼ (pratique au doigt). Le pilotage est désactivé le temps du rangement.</p>
    <div class="grille deux">
      <ShutterTile v-for="(v, i) in tries" :key="v.id" :v="v" :rangement="rangement" :premier="i === 0"
                   :dernier="i === tries.length - 1" :survol="cible === v.id" :draggable="rangement" @deplacer="s => deplacer(i, s)"
                   @dragstart="e => { glisse = v.id; e.dataTransfer.effectAllowed = 'move'; }"
                   @dragend="glisse = null; cible = null"
                   @dragover="e => { if (glisse && glisse !== v.id) { e.preventDefault(); cible = v.id; } }"
                   @dragleave="cible === v.id && (cible = null)"
                   @drop.prevent="deposer(v.id); cible = null" />
    </div>
    <p v-if="etat.statut && !tries.length" class="hint">Aucun appareil. Va dans <b>Appareils → Ajouter un appareil</b> pour en ajouter un.</p>
  </div>
</template>

<style scoped>
.volets{display:grid;gap:var(--gap)}
</style>
