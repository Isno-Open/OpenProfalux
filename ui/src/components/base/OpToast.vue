<script setup>
import { etat } from '../../store.js';
</script>

<template>
  <div class="op-toast" :class="{ visible: etat.toast }" role="status" aria-live="polite">{{ etat.toast && etat.toast.texte }}</div>
</template>

<style scoped>
.op-toast{position:fixed;z-index:50;left:50%;bottom:22px;transform:translate(-50%,20px);opacity:0;pointer-events:none;background:var(--text);color:var(--bg);padding:10px 16px;border-radius:var(--radius-sm);font-size:.88rem;font-weight:600;box-shadow:var(--shadow);transition:.2s}
.op-toast.visible{opacity:1;transform:translate(-50%,0)}
@media (max-width:640px){.op-toast{bottom:92px}}
</style>
