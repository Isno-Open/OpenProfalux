<script setup lang="ts" generic="Id extends string">
withDefaults(defineProps<{ items: { id: Id; label: string }[]; active?: Id | null }>(), { active: null });
defineEmits<{ choose: [id: Id] }>();
</script>

<template>
  <nav class="op-subtabs" aria-label="Sections">
    <button v-for="s in items" :key="s.id" type="button" class="subtab" :class="{ active: s.id === active }"
            :aria-current="s.id === active ? 'page' : undefined" @click="$emit('choose', s.id)">{{ s.label }}</button>
  </nav>
</template>

<style scoped>
.op-subtabs{display:flex;flex-wrap:nowrap;gap:8px;overflow-x:auto;scrollbar-width:none}
.subtab{flex:none;padding:7px 14px;border:1px solid var(--border);background:var(--surface);color:var(--muted);font-weight:600;font-size:.85rem;border-radius:999px;cursor:pointer;white-space:nowrap}
.subtab:hover{color:var(--text)}
.subtab.active{background:var(--text);color:var(--bg);border-color:var(--text)}
</style>
