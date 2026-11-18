<script setup>
// Choisir un ou plusieurs elements parmi des puces (un volet, les membres d'une centrale, un filtre).
// multiple : le modele est une liste ; sinon une valeur. ajout : une puce « + ... » en pointilles.
import OpChip from '../base/OpChip.vue';
const modele = defineModel({ type: [Array, String], default: '' });
const props = defineProps({
  options: { type: Array, required: true },   // [{ value, label, icon }]
  multiple: Boolean,
  ajout: { type: String, default: '' },
  vide: { type: String, default: 'Rien à choisir pour l\'instant.' },
  label: { type: String, default: '' },
});
const choisi = val => (props.multiple ? (modele.value || []).includes(val) : modele.value === val);
function basculer(val) {
  if (!props.multiple) { modele.value = val; return; }
  const l = [...(modele.value || [])];
  const i = l.indexOf(val);
  if (i >= 0) l.splice(i, 1); else l.push(val);
  modele.value = l;
}
</script>

<template>
  <div class="chip-picker" role="group" :aria-label="label || null">
    <OpChip v-for="o in options" :key="o.value" :on="choisi(o.value)" :icon="o.icon || ''" @click="basculer(o.value)">{{ o.label }}</OpChip>
    <OpChip v-if="ajout" ajout :on="choisi('__nouveau')" @click="basculer('__nouveau')">{{ ajout }}</OpChip>
    <span v-if="!options.length && !ajout" class="hint">{{ vide }}</span>
  </div>
</template>

<style scoped>
.chip-picker{display:flex;flex-wrap:wrap;gap:7px}
</style>
