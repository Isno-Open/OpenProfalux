<script setup lang="ts">
// Une puce cliquable, choisie ou non (filtres, choix d'un volet, membres d'une centrale).
import OpIcon from './OpIcon.vue';
import type { IconName } from '../../icons';
defineProps<{ on?: boolean; icon?: IconName; disabled?: boolean; add?: boolean }>();
</script>

<template>
  <button type="button" class="op-chip" :class="{ on, add }" :aria-pressed="add ? undefined : on" :disabled="disabled">
    <OpIcon v-if="icon" :name="icon" size="1.1em" /><slot />
  </button>
</template>

<style scoped>
.op-chip{display:inline-flex;align-items:center;gap:6px;padding:7px 14px;border-radius:999px;border:1px solid var(--border);background:var(--surface-2);color:var(--text);font-size:.85rem;font-weight:550;cursor:pointer;transition:.12s}
.op-chip:hover:not(:disabled){border-color:var(--accent-line)}
.op-chip.on{background:var(--accent-weak);border-color:var(--accent);color:var(--accent)}
.op-chip.add{border-style:dashed;color:var(--accent)}
.op-chip:disabled{opacity:.45;cursor:default}
</style>
