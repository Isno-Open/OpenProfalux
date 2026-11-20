<script setup lang="ts">
// Choisir un ou plusieurs elements parmi des puces (un volet, les membres d'une centrale, un filtre).
// multiple : le modele est une liste ; sinon une valeur. add : une puce « + ... » en pointilles (NEW_DEVICE).
import OpChip from '../base/OpChip.vue';
import { NEW_DEVICE } from '../../domain';
import type { IconName } from '../../icons';

export interface ChipOption { value: string; label: string; icon?: IconName }
const model = defineModel<string | string[]>({ default: '' });
const props = withDefaults(defineProps<{
  options: ChipOption[];
  multiple?: boolean;
  add?: string;
  empty?: string;
  label?: string;
  disabled?: boolean;   // pendant une operation (capture) : le choix ne bouge pas
}>(), { add: '', empty: 'Rien à choisir pour l\'instant.', label: '' });
const list = (): string[] => (Array.isArray(model.value) ? model.value : []);
const isChosen = (value: string): boolean => (props.multiple ? list().includes(value) : model.value === value);
function toggle(value: string): void {
  if (!props.multiple) { model.value = value; return; }
  const next = [...list()];
  const i = next.indexOf(value);
  if (i >= 0) next.splice(i, 1); else next.push(value);
  model.value = next;
}
</script>

<template>
  <div class="chip-picker" role="group" :aria-label="label || undefined">
    <OpChip v-for="o in options" :key="o.value" :on="isChosen(o.value)" :icon="o.icon" :disabled="disabled" @click="toggle(o.value)">{{ o.label }}</OpChip>
    <OpChip v-if="add" add :on="isChosen(NEW_DEVICE)" :disabled="disabled" @click="toggle(NEW_DEVICE)">{{ add }}</OpChip>
    <span v-if="!options.length && !add" class="hint">{{ empty }}</span>
  </div>
</template>

<style scoped>
.chip-picker{display:flex;flex-wrap:wrap;gap:7px}
</style>
