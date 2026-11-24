<script setup lang="ts">
// Une liste deroulante.
export interface SelectOption { value: string | number; label: string; disabled?: boolean }
const model = defineModel<string | number>({ default: '' });
withDefaults(defineProps<{ options: SelectOption[]; id?: string; disabled?: boolean }>(), { id: undefined });
</script>

<template>
  <select :id="id" v-model="model" class="op-input op-select" :disabled="disabled">
    <option v-for="o in options" :key="o.value" :value="o.value" :disabled="o.disabled">{{ o.label }}</option>
  </select>
</template>

<style scoped>
.op-select{appearance:none;padding-right:34px;cursor:pointer;background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);background-position:calc(100% - 17px) 55%,calc(100% - 12px) 55%;background-size:5px 5px;background-repeat:no-repeat}
</style>
