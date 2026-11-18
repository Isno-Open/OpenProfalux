<script setup>
// Un bloc repliable (reglages avances...) : l'element details du navigateur, accessible tel quel.
const ouvert = defineModel('open', { type: Boolean, default: false });
defineProps({ titre: { type: String, required: true }, icon: { type: String, default: '' } });
import OpIcon from './OpIcon.vue';
</script>

<template>
  <details class="op-details" :open="ouvert" @toggle="e => (ouvert = e.target.open)">
    <summary><OpIcon v-if="icon" :name="icon" size="1.1em" /><span>{{ titre }}</span></summary>
    <div class="corps"><slot /></div>
  </details>
</template>

<style scoped>
.op-details{border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
summary{display:flex;align-items:center;gap:8px;padding:10px 12px;cursor:pointer;font-weight:600;font-size:.88rem;list-style:none}
summary::-webkit-details-marker{display:none}
summary::after{content:"";margin-left:auto;width:7px;height:7px;border-right:2px solid var(--muted);border-bottom:2px solid var(--muted);transform:rotate(45deg);transition:.15s}
.op-details[open] summary::after{transform:rotate(225deg)}
.corps{display:grid;gap:12px;padding:2px 12px 14px}
</style>
