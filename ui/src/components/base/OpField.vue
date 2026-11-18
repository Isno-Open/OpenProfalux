<script setup lang="ts">
// Un champ de formulaire : libelle, controle (fente) et aide. Le controle porte la classe op-input.
withDefaults(defineProps<{
  label?: string;
  hint?: string;
  control?: string;   // id du controle, pour que le libelle le designe
}>(), { label: '', hint: '', control: undefined });
</script>

<template>
  <div class="op-field">
    <label v-if="label" class="lbl" :for="control">{{ label }}</label>
    <slot />
    <p v-if="hint || $slots.hint" class="hint"><slot name="hint">{{ hint }}</slot></p>
  </div>
</template>

<style scoped>
.op-field{display:grid;gap:6px;min-width:0;align-content:start}
.lbl{font-size:.85rem;font-weight:600;color:var(--text)}
.hint{font-size:.8rem}
</style>
