<script setup lang="ts">
import OpIcon from './OpIcon.vue';
import { BUTTON, type ButtonVariant } from '../../domain';
import type { IconName } from '../../icons';
withDefaults(defineProps<{
  variant?: ButtonVariant;
  icon?: IconName;
  wide?: boolean;
  small?: boolean;
  type?: 'button' | 'submit';
  disabled?: boolean;
}>(), { variant: BUTTON.normal, icon: undefined, type: 'button' });
</script>

<template>
  <button :type="type" class="op-btn" :class="[variant, { wide, small }]" :disabled="disabled">
    <OpIcon v-if="icon" :name="icon" size="1.15em" /><slot />
  </button>
</template>

<style scoped>
.op-btn{appearance:none;font:inherit;font-weight:600;font-size:.9rem;padding:10px 18px;border-radius:var(--radius-btn);border:1px solid var(--border);background:var(--surface);color:var(--text);cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:8px;transition:border-color .14s,filter .14s,background .14s}
.op-btn:hover:not(:disabled){border-color:var(--accent-line)}
.op-btn.primary{background:var(--accent);border-color:var(--accent);color:var(--accent-fg);box-shadow:0 6px 16px color-mix(in srgb,var(--accent) 30%,transparent)}
.op-btn.primary:hover:not(:disabled){filter:brightness(1.07)}
.op-btn.ghost{background:transparent}
.op-btn.danger{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 35%,var(--border))}
.op-btn.danger:hover:not(:disabled){background:color-mix(in srgb,var(--danger) 10%,transparent)}
.op-btn.wide{width:100%}
.op-btn.small{padding:5px 11px;font-size:.8rem}
.op-btn:disabled{opacity:.5;cursor:default}
</style>
