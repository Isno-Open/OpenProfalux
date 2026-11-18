<script setup>
// Une liste deroulante. options : [{ value, label, disabled }].
const modele = defineModel({ type: [String, Number], default: '' });
defineProps({ options: { type: Array, required: true }, id: { type: String, default: null } });
</script>

<template>
  <select :id="id" v-model="modele" class="op-input op-select">
    <option v-for="o in options" :key="o.value" :value="o.value" :disabled="o.disabled">{{ o.label }}</option>
  </select>
</template>

<style scoped>
.op-select{appearance:none;padding-right:34px;cursor:pointer;background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);background-position:calc(100% - 17px) 55%,calc(100% - 12px) 55%;background-size:5px 5px;background-repeat:no-repeat}
</style>
