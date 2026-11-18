<script setup>
// Choisir un fichier (firmware .bin, sauvegarde .json) : un vrai champ fichier, habille en bouton, avec
// le nom et la taille du fichier choisi.
import { ref } from 'vue';
import OpIcon from './OpIcon.vue';
defineProps({ accept: { type: String, default: '' }, label: { type: String, default: 'Choisir un fichier' }, id: { type: String, default: null } });
const emit = defineEmits(['choisi']);
const fichier = ref(null);
function changer(e) {
  fichier.value = e.target.files[0] || null;
  emit('choisi', fichier.value);
}
const taille = o => (o > 1048576 ? `${(o / 1048576).toFixed(2).replace('.', ',')} Mo` : `${Math.round(o / 1024)} Ko`);
</script>

<template>
  <label class="op-file">
    <input :id="id" type="file" :accept="accept" @change="changer">
    <span class="bouton"><OpIcon name="ota" size="1.1em" />{{ label }}</span>
    <span class="nom" :class="{ vide: !fichier }">{{ fichier ? `${fichier.name} · ${taille(fichier.size)}` : 'aucun fichier choisi' }}</span>
  </label>
</template>

<style scoped>
.op-file{display:flex;align-items:center;gap:10px;flex-wrap:wrap;cursor:pointer}
input{position:absolute;opacity:0;width:1px;height:1px}
.bouton{display:inline-flex;align-items:center;gap:8px;padding:9px 15px;border-radius:var(--radius-btn);border:1px dashed var(--accent-line);background:var(--surface-2);font-weight:600;font-size:.9rem}
.op-file:hover .bouton{border-color:var(--accent)}
input:focus-visible + .bouton{outline:2px solid var(--accent);outline-offset:2px}
.nom{font-size:.85rem;font-family:var(--mono);min-width:0;overflow-wrap:anywhere}
.nom.vide{color:var(--muted);font-family:var(--sans)}
</style>
