<script setup>
// Une barre d'avancement. valeur entre 0 et 1 ; sans valeur, elle defile (duree inconnue).
defineProps({ valeur: { type: Number, default: null }, label: { type: String, default: '' } });
</script>

<template>
  <div class="op-progress" role="progressbar" :aria-label="label || null" aria-valuemin="0" aria-valuemax="100"
       :aria-valuenow="valeur == null ? null : Math.round(valeur * 100)">
    <span class="barre" :class="{ inconnue: valeur == null }" :style="valeur == null ? null : { width: `${Math.round(valeur * 100)}%` }" />
  </div>
</template>

<style scoped>
.op-progress{height:6px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);overflow:hidden}
.barre{display:block;height:100%;background:var(--accent);border-radius:inherit;transition:width .25s linear}
.barre.inconnue{width:35%;animation:defile 1.2s ease-in-out infinite}
@keyframes defile{0%{transform:translateX(-100%)}100%{transform:translateX(300%)}}
</style>
