<script setup lang="ts">
// Un bloc repliable (reglages avances...) : l'element details du navigateur, accessible tel quel.
import OpIcon from './OpIcon.vue';
import type { IconName } from '../../icons';
const open = defineModel<boolean>('open', { default: false });
defineProps<{ title: string; icon?: IconName }>();
function onToggle(e: Event): void { if (e.target instanceof HTMLDetailsElement) open.value = e.target.open; }
</script>

<template>
  <details class="op-details" :open="open" @toggle="onToggle">
    <summary><OpIcon v-if="icon" :name="icon" size="1.1em" /><span>{{ title }}</span></summary>
    <div class="body"><slot /></div>
  </details>
</template>

<style scoped>
.op-details{border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
summary{display:flex;align-items:center;gap:8px;padding:10px 12px;cursor:pointer;font-weight:600;font-size:.88rem;list-style:none}
summary::-webkit-details-marker{display:none}
summary::after{content:"";margin-left:auto;width:7px;height:7px;border-right:2px solid var(--muted);border-bottom:2px solid var(--muted);transform:rotate(45deg);transition:.15s}
.op-details[open] summary::after{transform:rotate(225deg)}
.body{display:grid;gap:12px;padding:2px 12px 14px}
</style>
