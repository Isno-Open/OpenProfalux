<script setup lang="ts">
// Le pave d'un appareil a trois commandes (volet : montee / stop / descente). Les libelles viennent du type
// declare dans profiles.json : symbole et libelle.
import { COMMAND, MOVE_COMMANDS, type Command, type MoveCommand } from '../../domain';
withDefaults(defineProps<{
  labels: Partial<Record<Command, string>>;
  sending?: Command | '';     // commande en cours d'emission
  disabled?: boolean;
}>(), { sending: '' });
defineEmits<{ command: [cmd: MoveCommand] }>();
const SYMBOL: Record<MoveCommand, string> = { [COMMAND.up]: '▲', [COMMAND.stop]: '■', [COMMAND.down]: '▼' };
</script>

<template>
  <div class="pad">
    <button v-for="cmd in MOVE_COMMANDS" :key="cmd" type="button" :class="{ sending: sending === cmd }" :disabled="disabled && sending !== cmd"
            :title="labels[cmd]" :aria-label="labels[cmd]" @click="$emit('command', cmd)">
      <span class="sym" aria-hidden="true">{{ SYMBOL[cmd] }}</span><span class="lbl">{{ labels[cmd] }}</span>
    </button>
  </div>
</template>

<style scoped>
.pad{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
button{display:flex;flex-direction:column;align-items:center;gap:2px;padding:11px 0 9px;font-size:1.1rem;border:1px solid transparent;background:color-mix(in srgb,var(--muted) 12%,transparent);color:var(--text);border-radius:14px;cursor:pointer;transition:.12s}
button:hover:not(:disabled){border-color:var(--accent-line);color:var(--accent)}
button:active:not(:disabled){background:var(--accent-weak)}
button:disabled{opacity:.45;cursor:not-allowed;filter:grayscale(.35)}
button.sending{background:var(--accent);color:var(--accent-fg);border-color:var(--accent);animation:pulse 1s ease-in-out infinite}
.lbl{display:block;font-size:.72rem;font-weight:600;color:var(--muted)}
button.sending .lbl{color:inherit}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
</style>
