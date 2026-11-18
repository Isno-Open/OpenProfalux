<script setup>
// Une methode d'ajout (un profil de profiles.json), en carte cliquable : la pyramide et la detection
// montrent la meme carte et menent au meme parcours. La phrase vient du champ description du profil.
import { PHRASE_METHODE, BANDE } from '../../profils.js';
import OpBadge from '../base/OpBadge.vue';
defineProps({ profil: { type: Object, required: true }, propose: Boolean });
defineEmits(['choisir']);
</script>

<template>
  <button type="button" class="choix" :class="{ propose }" @click="$emit('choisir', profil)">
    <span class="haut"><b>{{ profil.nom }}</b><OpBadge v-if="profil.band">{{ BANDE(profil.band) }}</OpBadge><OpBadge v-if="propose" tone="accent">proposé</OpBadge></span>
    <span class="phrase">{{ profil.description || PHRASE_METHODE[profil.flow] || '' }}</span>
    <span class="aller" aria-hidden="true">›</span>
  </button>
</template>

<style scoped>
.choix{position:relative;display:grid;gap:5px;width:100%;text-align:left;padding:16px 44px 16px 18px;border:1px solid var(--border);border-radius:14px;background:var(--surface-2);color:var(--text);font:inherit;cursor:pointer;transition:.12s}
.choix:hover{border-color:var(--accent)}
.choix.propose{border-color:var(--accent-line);background:var(--accent-weak)}
.haut{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
.phrase{font-size:.84rem;color:var(--muted)}
.aller{position:absolute;right:15px;top:50%;transform:translateY(-50%);font-size:1.4rem;color:var(--muted)}
</style>
