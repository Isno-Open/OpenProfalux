<script setup lang="ts">
import { TONE, type Tone } from '../../domain';
withDefaults(defineProps<{ tone?: Tone }>(), { tone: TONE.ok });
</script>

<template>
  <div class="op-statline" :class="tone"><span class="dot" /><div class="text"><slot /></div></div>
</template>

<style scoped>
.op-statline{display:flex;align-items:flex-start;gap:10px;padding:10px 12px;border-radius:var(--radius-sm);background:var(--surface-2);border:1px solid var(--border);font-size:.86rem}
.dot{flex:none;width:9px;height:9px;margin-top:.45em;border-radius:50%;background:var(--muted)}
.ok .dot{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 22%,transparent)}
.warning .dot{background:var(--warn)}
.danger .dot{background:var(--danger)}
.warning{border-color:color-mix(in srgb,var(--warn) 35%,var(--border))}
.danger{border-color:color-mix(in srgb,var(--danger) 35%,var(--border))}
</style>
