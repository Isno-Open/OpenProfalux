<script setup lang="ts">
// Une liste d'etiquettes avec une croix chacune, et un champ pour en ajouter (Entree).
import { ref } from 'vue';
withDefaults(defineProps<{ tags: string[]; placeholder?: string; mono?: boolean }>(), { placeholder: '' });
const emit = defineEmits<{ add: [tag: string]; remove: [tag: string] }>();
const draft = ref('');
function submit(): void {
  const t = draft.value.trim();
  if (t) emit('add', t);
  draft.value = '';
}
</script>

<template>
  <div class="op-tagbox">
    <span v-for="t in tags" :key="t" class="tag" :class="{ mono }">{{ t }}
      <button type="button" :aria-label="`Retirer ${t}`" title="Retirer" @click="emit('remove', t)">×</button></span>
    <input v-model="draft" :placeholder="placeholder" spellcheck="false" autocomplete="off" :class="{ mono }" @keydown.enter.prevent="submit">
  </div>
</template>

<style scoped>
.op-tagbox{display:flex;flex-wrap:wrap;align-items:center;gap:6px;padding:6px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.tag{display:inline-flex;align-items:center;gap:4px;padding:3px 4px 3px 9px;border-radius:999px;background:var(--surface);border:1px solid var(--border);font-size:.82rem}
.tag button{border:0;background:none;color:var(--muted);font-size:1rem;line-height:1;padding:0 4px;cursor:pointer;border-radius:50%}
.tag button:hover{color:var(--danger)}
input{flex:1;min-width:130px;border:0;background:transparent;color:var(--text);font:inherit;font-size:.88rem;padding:4px 6px}
input:focus{outline:none}
.mono{font-family:var(--mono)}
</style>
