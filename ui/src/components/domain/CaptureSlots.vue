<script setup lang="ts">
// Les commandes a capturer d'un appareil, une ligne chacune : etat, bouton Capturer / Recapturer, et pour un
// clone, « deplacer » une trame rangee sur la mauvaise commande. Les commandes viennent du type (profiles.json
// types[].capture) : trois pour un volet, une pour un portail.
import OpButton from '../base/OpButton.vue';
import OpSelect, { type SelectOption } from '../base/OpSelect.vue';
import OpProgress from '../base/OpProgress.vue';
import { BUTTON, COMMAND, isCommand, type Command } from '../../domain';

export interface CaptureAction { cmd: Command; label: string }
const props = withDefaults(defineProps<{
  actions: CaptureAction[];
  learned?: Partial<Record<Command, string | true>>;   // commande -> detail de la trame (ou true)
  busy?: Command | '';
  replay?: boolean;
  disabled?: boolean;
}>(), { learned: () => ({}), busy: '' });
const emit = defineEmits<{ capture: [cmd: Command]; move: [from: Command, to: Command] }>();
const SYMBOL: Partial<Record<Command, string>> = { [COMMAND.up]: '▲', [COMMAND.stop]: '■', [COMMAND.down]: '▼' };
const moveTargets = (cmd: Command): SelectOption[] =>
  [{ value: '', label: 'Déplacer vers…' }, ...props.actions.filter(x => x.cmd !== cmd).map(x => ({ value: x.cmd, label: `→ ${x.label}` }))];
function onMove(from: Command, value: string | number): void { if (typeof value === 'string' && isCommand(value)) emit('move', from, value); }
</script>

<template>
  <div class="slots">
    <div v-for="a in actions" :key="a.cmd" class="slot" :class="{ done: learned[a.cmd], current: busy === a.cmd }">
      <span class="sym" aria-hidden="true">{{ SYMBOL[a.cmd] || '●' }}</span>
      <div class="text">
        <b>{{ a.label }}</b>
        <span v-if="busy === a.cmd" class="state">Appuie maintenant sur « {{ a.label }} » de ta télécommande, à moins d'un mètre…</span>
        <span v-else-if="learned[a.cmd]" class="state ok">✓ {{ replay ? 'captée' : 'apprise' }}<template v-if="learned[a.cmd] !== true"> · <code>{{ learned[a.cmd] }}</code></template></span>
        <span v-else class="state">à capturer</span>
        <OpProgress v-if="busy === a.cmd" label="Capture en cours" />
      </div>
      <div class="actions">
        <OpSelect v-if="!replay && learned[a.cmd] && actions.length > 1" :model-value="''" :options="moveTargets(a.cmd)" class="move"
                  :aria-label="`Déplacer la trame de ${a.label}`" @update:model-value="value => onMove(a.cmd, value)" />
        <OpButton :variant="learned[a.cmd] ? BUTTON.normal : BUTTON.primary" small :disabled="disabled" @click="emit('capture', a.cmd)">
          {{ learned[a.cmd] ? 'Recapturer' : 'Capturer' }}</OpButton>
      </div>
    </div>
  </div>
</template>

<style scoped>
.slots{display:grid;gap:8px}
.slot{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:12px;padding:10px 12px;border:1px solid var(--border);border-radius:var(--radius-sm);background:var(--surface-2)}
.slot.done{border-color:color-mix(in srgb,var(--ok) 35%,var(--border))}
.slot.current{border-color:var(--accent)}
.sym{display:grid;place-items:center;width:34px;height:34px;border-radius:10px;background:var(--surface);border:1px solid var(--border);font-size:.95rem}
.done .sym{color:var(--ok)}
.text{display:grid;gap:3px;min-width:0}
.state{font-size:.8rem;color:var(--muted)}
.state.ok{color:var(--ok)}
.state code{color:var(--muted)}
.actions{display:flex;gap:8px;align-items:center}
.move{width:auto;font-size:.8rem;padding-top:5px;padding-bottom:5px}
@media (max-width:520px){
  .slot{grid-template-columns:auto 1fr}
  .actions{grid-column:1 / -1;justify-content:flex-end}
}
</style>
