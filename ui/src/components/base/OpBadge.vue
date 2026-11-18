<script setup lang="ts">
import { TONE, type Tone } from '../../domain';
withDefaults(defineProps<{ tone?: Tone }>(), { tone: TONE.neutral });
</script>

<template>
  <span class="op-badge" :class="tone"><slot /></span>
</template>

<style scoped>
.op-badge{display:inline-flex;align-items:center;gap:5px;font-size:.72rem;font-weight:600;padding:2px 8px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);color:var(--muted);white-space:nowrap}
.op-badge.accent{background:var(--accent-weak);border-color:var(--accent-line);color:var(--accent)}
.op-badge.ok{color:var(--ok);border-color:color-mix(in srgb,var(--ok) 35%,var(--border))}
.op-badge.warning{color:var(--warn);border-color:color-mix(in srgb,var(--warn) 40%,var(--border))}
.op-badge.danger{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 40%,var(--border));background:color-mix(in srgb,var(--danger) 8%,var(--surface-2))}
</style>
