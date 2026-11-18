<script setup lang="ts">
// Un interrupteur oui / non : une vraie case a cocher (clavier, lecteur d'ecran), habillee.
const model = defineModel<boolean>({ default: false });
defineProps<{ disabled?: boolean }>();
</script>

<template>
  <label class="op-switch" :class="{ off: disabled }">
    <input v-model="model" type="checkbox" role="switch" :disabled="disabled">
    <span class="track" aria-hidden="true" />
    <span class="text"><slot /></span>
  </label>
</template>

<style scoped>
.op-switch{display:inline-flex;align-items:center;gap:10px;cursor:pointer;font-size:.9rem}
.op-switch.off{opacity:.55;cursor:default}
input{position:absolute;opacity:0;width:1px;height:1px}
.track{flex:none;position:relative;width:38px;height:22px;border-radius:999px;background:var(--surface-2);border:1px solid var(--border);transition:.15s}
.track::after{content:"";position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:50%;background:var(--muted);transition:.15s}
input:checked + .track{background:var(--accent);border-color:var(--accent)}
input:checked + .track::after{left:18px;background:#fff}
input:focus-visible + .track{outline:2px solid var(--accent);outline-offset:2px}
</style>
