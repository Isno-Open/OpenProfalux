<script setup lang="ts">
// Choisir un fichier (firmware .bin, sauvegarde .json) : un vrai champ fichier, habille en bouton, avec le nom
// et la taille du fichier choisi.
import { ref } from 'vue';
import OpIcon from './OpIcon.vue';
import { ICON } from '../../icons';
withDefaults(defineProps<{ accept?: string; label?: string; id?: string }>(), { accept: '', label: 'Choisir un fichier', id: undefined });
const emit = defineEmits<{ chosen: [file: File | null] }>();
const file = ref<File | null>(null);
function onChange(e: Event): void {
  file.value = e.target instanceof HTMLInputElement ? (e.target.files?.[0] ?? null) : null;
  emit('chosen', file.value);
}
const sizeLabel = (bytes: number): string => (bytes > 1048576 ? `${(bytes / 1048576).toFixed(2).replace('.', ',')} Mo` : `${Math.round(bytes / 1024)} Ko`);
</script>

<template>
  <label class="op-file">
    <input :id="id" type="file" :accept="accept" @change="onChange">
    <span class="button"><OpIcon :name="ICON.ota" size="1.1em" />{{ label }}</span>
    <span class="name" :class="{ empty: !file }">{{ file ? `${file.name} · ${sizeLabel(file.size)}` : 'aucun fichier choisi' }}</span>
  </label>
</template>

<style scoped>
.op-file{display:flex;align-items:center;gap:10px;flex-wrap:wrap;cursor:pointer}
input{position:absolute;opacity:0;width:1px;height:1px}
.button{display:inline-flex;align-items:center;gap:8px;padding:9px 15px;border-radius:var(--radius-btn);border:1px dashed var(--accent-line);background:var(--surface-2);font-weight:600;font-size:.9rem}
.op-file:hover .button{border-color:var(--accent)}
input:focus-visible + .button{outline:2px solid var(--accent);outline-offset:2px}
.name{font-size:.85rem;font-family:var(--mono);min-width:0;overflow-wrap:anywhere}
.name.empty{color:var(--muted);font-family:var(--sans)}
</style>
