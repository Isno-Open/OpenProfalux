<script setup>
// Un champ de formulaire : libelle, controle (fente) et aide. Le controle porte la classe op-input.
defineProps({
  label: { type: String, default: '' },
  hint: { type: String, default: '' },
  pour: { type: String, default: null },   // id du controle, pour que le libelle le designe
});
</script>

<template>
  <div class="op-field">
    <label v-if="label" class="lbl" :for="pour">{{ label }}</label>
    <slot />
    <p v-if="hint || $slots.hint" class="hint"><slot name="hint">{{ hint }}</slot></p>
  </div>
</template>

<style scoped>
.op-field{display:grid;gap:6px;min-width:0;align-content:start}
.lbl{font-size:.85rem;font-weight:600;color:var(--text)}
.hint{font-size:.8rem}
</style>
