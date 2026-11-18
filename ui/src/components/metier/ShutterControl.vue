<script setup>
// Le pave d'un appareil a trois commandes (volet : montee / stop / descente). Les libelles viennent du
// type declare dans profiles.json. A : symboles seuls, comme aujourd'hui ; B : symbole et libelle.
defineProps({
  libelles: { type: Object, required: true },   // { up, stop, down }
  envoi: { type: String, default: '' },          // commande en cours d'emission
  disabled: Boolean,
});
defineEmits(['ordre']);
const CMDS = [{ cmd: 'up', sym: '▲' }, { cmd: 'stop', sym: '■' }, { cmd: 'down', sym: '▼' }];
</script>

<template>
  <div class="pave">
    <button v-for="c in CMDS" :key="c.cmd" type="button" :class="{ envoi: envoi === c.cmd }" :disabled="disabled && envoi !== c.cmd"
            :title="libelles[c.cmd]" :aria-label="libelles[c.cmd]" @click="$emit('ordre', c.cmd)">
      <span class="sym" aria-hidden="true">{{ c.sym }}</span><span class="lbl">{{ libelles[c.cmd] }}</span>
    </button>
  </div>
</template>

<style scoped>
.pave{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
button{display:flex;flex-direction:column;align-items:center;gap:2px;padding:11px 0 9px;font-size:1.1rem;border:1px solid transparent;background:color-mix(in srgb,var(--muted) 12%,transparent);color:var(--text);border-radius:14px;cursor:pointer;transition:.12s}
button:hover:not(:disabled){border-color:var(--accent-line);color:var(--accent)}
button:active:not(:disabled){background:var(--accent-weak)}
button:disabled{opacity:.45;cursor:not-allowed;filter:grayscale(.35)}
button.envoi{background:var(--accent);color:var(--accent-fg);border-color:var(--accent);animation:pulse 1s ease-in-out infinite}
.lbl{display:block;font-size:.72rem;font-weight:600;color:var(--muted)}
button.envoi .lbl{color:inherit}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
</style>
