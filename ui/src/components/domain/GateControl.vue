<script setup lang="ts">
// Le controle d'un portail a bouton unique : un appui du bouton pas a pas, comme celui de la telecommande d'origine.
// Le boitier suit le cycle de la carte du moteur (ouvre, stop, ferme, stop). Le libelle vient du type « portail ».
import { COMMAND, type Command } from '../../domain';
withDefaults(defineProps<{ label?: string; sending?: boolean; disabled?: boolean }>(), { label: 'Bouton du portail' });
defineEmits<{ command: [cmd: Command] }>();
</script>

<template>
  <div class="gate">
    <button type="button" :class="{ sending }" :disabled="disabled && !sending" @click="$emit('command', COMMAND.step)">
      <svg viewBox="0 0 24 24" class="sym" aria-hidden="true"><path d="M7 5.5a7.5 7.5 0 1 0 10 0M12 3v8" /></svg>
      Actionner le portail
    </button>
    <p class="help">{{ label }} : une seule commande, comme la télécommande d'origine.</p>
  </div>
</template>

<style scoped>
.gate{display:grid;gap:8px}
button{display:flex;align-items:center;justify-content:center;gap:10px;padding:13px 0;font-weight:650;border:1px solid transparent;background:color-mix(in srgb,var(--muted) 12%,transparent);color:var(--text);border-radius:14px;cursor:pointer;transition:.12s}
button:hover:not(:disabled){border-color:var(--accent-line);color:var(--accent)}
button:disabled{opacity:.45;cursor:not-allowed}
button.sending{background:var(--accent);color:var(--accent-fg);border-color:var(--accent);animation:pulse 1s ease-in-out infinite}
.sym{width:1.25em;height:1.25em;fill:none;stroke:currentColor;stroke-width:2.2;stroke-linecap:round}
.help{margin:0;font-size:.76rem;color:var(--muted)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
</style>
