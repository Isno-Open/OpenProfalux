<script setup>
defineProps({ tone: { type: String, default: 'ok' } });   // ok | alerte | danger | neutre
</script>

<template>
  <div class="op-statline" :class="tone"><span class="point" /><div class="texte"><slot /></div></div>
</template>

<style scoped>
.op-statline{display:flex;align-items:flex-start;gap:10px;padding:10px 12px;border-radius:var(--radius-sm);background:var(--surface-2);border:1px solid var(--border);font-size:.86rem}
.point{flex:none;width:9px;height:9px;margin-top:.45em;border-radius:50%;background:var(--muted)}
.ok .point{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 22%,transparent)}
.alerte .point{background:var(--warn)}
.danger .point{background:var(--danger)}
.alerte{border-color:color-mix(in srgb,var(--warn) 35%,var(--border))}
.danger{border-color:color-mix(in srgb,var(--danger) 35%,var(--border))}
</style>
