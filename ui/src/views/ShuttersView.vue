<script setup lang="ts">
// Onglet Volets : la grille des appareils, et leur rangement : glisser-deposer a la souris, fleches au doigt (le
// glisser HTML5 ne marche pas sur telephone). Le pilotage est neutralise pendant le rangement.
import { computed, ref } from 'vue';
import { state, saveOrder } from '../store';
import ShutterTile from '../components/domain/ShutterTile.vue';
import OpButton from '../components/base/OpButton.vue';

const arranging = ref(false);
/* Ordre choisi par l'utilisateur (champ order du firmware) ; -1 = jamais range, apres les autres. */
const sorted = computed(() => [...(state.status?.volets ?? [])].sort((a, b) => {
  const oa = a.order ?? -1, ob = b.order ?? -1;
  if (oa >= 0 && ob >= 0) return oa - ob;
  if (oa >= 0) return -1;
  if (ob >= 0) return 1;
  return (b.central ? 1 : 0) - (a.central ? 1 : 0);
}));
function move(i: number, step: number): void {
  const ids = sorted.value.map(d => d.id);
  const j = i + step;
  const a = ids[i], b = ids[j];
  if (a === undefined || b === undefined) return;
  ids[i] = b; ids[j] = a;
  saveOrder(ids);
}
/* Depot : la tuile glissee prend la place de la cible (insertion, le reste se decale). */
const dragged = ref<string | null>(null), target = ref<string | null>(null);
function drop(id: string): void {
  const ids = sorted.value.map(d => d.id);
  const moving = dragged.value;
  const from = moving ? ids.indexOf(moving) : -1;
  if (!moving || from < 0 || moving === id) return;
  ids.splice(from, 1);
  ids.splice(ids.indexOf(id), 0, moving);
  saveOrder(ids);
}
function onDragStart(e: DragEvent, id: string): void { dragged.value = id; if (e.dataTransfer) e.dataTransfer.effectAllowed = 'move'; }
function onDragOver(e: DragEvent, id: string): void { if (dragged.value && dragged.value !== id) { e.preventDefault(); target.value = id; } }
</script>

<template>
  <div class="shutters">
    <div v-if="sorted.length > 1" class="row end">
      <OpButton @click="arranging = !arranging">{{ arranging ? '✓ Terminé' : '↕ Réorganiser' }}</OpButton>
    </div>
    <p v-if="arranging" class="hint"><b>Glisse une carte</b> à l'endroit voulu, ou utilise les flèches ▲▼ (pratique au doigt). Le pilotage est désactivé le temps du rangement.</p>
    <div class="grid two">
      <ShutterTile v-for="(d, i) in sorted" :key="d.id" :device="d" :arranging="arranging" :first="i === 0"
                   :last="i === sorted.length - 1" :hovered="target === d.id" :draggable="arranging" @move="step => move(i, step)"
                   @dragstart="(e: DragEvent) => onDragStart(e, d.id)"
                   @dragend="dragged = null; target = null"
                   @dragover="(e: DragEvent) => onDragOver(e, d.id)"
                   @dragleave="target === d.id && (target = null)"
                   @drop.prevent="drop(d.id); target = null" />
    </div>
    <p v-if="state.status && !sorted.length" class="hint">Aucun appareil. Va dans <b>Appareils → Ajouter un appareil</b> pour en ajouter un.</p>
  </div>
</template>

<style scoped>
.shutters{display:grid;gap:var(--gap)}
</style>
