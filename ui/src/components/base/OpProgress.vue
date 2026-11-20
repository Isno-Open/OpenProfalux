<script setup lang="ts">
// Une barre d'avancement. value entre 0 et 1 ; sans valeur, elle defile (duree inconnue).
withDefaults(defineProps<{ value?: number | null; label?: string }>(), { value: null, label: '' });
</script>

<template>
  <div class="op-progress" role="progressbar" :aria-label="label || undefined" aria-valuemin="0" aria-valuemax="100"
       :aria-valuenow="value == null ? undefined : Math.round(value * 100)">
    <span class="bar" :class="{ unknown: value == null }" :style="value == null ? undefined : { width: `${Math.round(value * 100)}%` }" />
  </div>
</template>

<style scoped>
.op-progress{height:6px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);overflow:hidden}
.bar{display:block;height:100%;background:var(--accent);border-radius:inherit;transition:width .25s linear}
.bar.unknown{width:35%;animation:slide 1.2s ease-in-out infinite}
@keyframes slide{0%{transform:translateX(-100%)}100%{transform:translateX(300%)}}
</style>
