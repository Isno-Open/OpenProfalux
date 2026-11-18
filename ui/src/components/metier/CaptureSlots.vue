<script setup>
// Les commandes a capturer d'un appareil, une ligne chacune : etat, bouton Capturer / Recapturer, et pour
// un clone, « deplacer » une trame rangee sur la mauvaise commande. Les commandes viennent du type
// (profiles.json types[].capture) : trois pour un volet, une pour un portail.
import OpButton from '../base/OpButton.vue';
import OpSelect from '../base/OpSelect.vue';
import OpProgress from '../base/OpProgress.vue';

const props = defineProps({
  actions: { type: Array, required: true },    // [{ cmd, label }]
  appris: { type: Object, default: () => ({}) },  // cmd -> texte du detail (ou true)
  enCours: { type: String, default: '' },
  rejeu: Boolean,
  disabled: Boolean,
});
const emit = defineEmits(['capturer', 'deplacer']);
const SYM = { up: '▲', stop: '■', down: '▼' };
const vers = a => [{ value: '', label: 'Déplacer vers…' }, ...props.actions.filter(x => x.cmd !== a).map(x => ({ value: x.cmd, label: `→ ${x.label}` }))];
function deplacer(de, val) { if (val) emit('deplacer', de, val); }
</script>

<template>
  <div class="slots">
    <div v-for="a in actions" :key="a.cmd" class="slot" :class="{ fait: appris[a.cmd], cours: enCours === a.cmd }">
      <span class="sym" aria-hidden="true">{{ SYM[a.cmd] || '●' }}</span>
      <div class="texte">
        <b>{{ a.label }}</b>
        <span v-if="enCours === a.cmd" class="etat">Appuie maintenant sur « {{ a.label }} » de ta télécommande, à moins d'un mètre…</span>
        <span v-else-if="appris[a.cmd]" class="etat ok">✓ {{ rejeu ? 'captée' : 'apprise' }}<template v-if="appris[a.cmd] !== true"> · <code>{{ appris[a.cmd] }}</code></template></span>
        <span v-else class="etat">à capturer</span>
        <OpProgress v-if="enCours === a.cmd" label="Capture en cours" />
      </div>
      <div class="actions">
        <OpSelect v-if="!rejeu && appris[a.cmd] && actions.length > 1" :model-value="''" :options="vers(a.cmd)" class="deplacer"
                  :aria-label="`Déplacer la trame de ${a.label}`" @update:model-value="val => deplacer(a.cmd, val)" />
        <OpButton :variant="appris[a.cmd] ? 'normal' : 'primary'" small :disabled="disabled" @click="emit('capturer', a.cmd)">
          {{ appris[a.cmd] ? 'Recapturer' : 'Capturer' }}</OpButton>
      </div>
    </div>
  </div>
</template>

<style scoped>
.slots{display:grid;gap:8px}
.slot{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:12px;padding:10px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.slot.fait{border-color:color-mix(in srgb,var(--ok) 35%,var(--border))}
.slot.cours{border-color:var(--accent)}
.sym{display:grid;place-items:center;width:34px;height:34px;border-radius:10px;background:var(--surface);border:1px solid var(--border);font-size:.95rem}
.fait .sym{color:var(--ok)}
.texte{display:grid;gap:3px;min-width:0}
.etat{font-size:.8rem;color:var(--muted)}
.etat.ok{color:var(--ok)}
.etat code{color:var(--muted)}
.actions{display:flex;gap:8px;align-items:center}
.deplacer{width:auto;font-size:.8rem;padding-top:5px;padding-bottom:5px}
@media (max-width:520px){
  .slot{grid-template-columns:auto 1fr}
  .actions{grid-column:1 / -1;justify-content:flex-end}
}
</style>
