<script setup lang="ts">
// Les reglages d'emission PAR APPAREIL : porteuse, TE, bit de repetition, RMT, format de trame Somfy. Un seul
// composant a l'ajout (pre-rempli depuis le profil) et a l'edition (depuis la fiche). Les champs viennent du
// profil (txFields), jamais d'une liste en dur. « Caler sur ma telecommande » cherche la porteuse et la reporte
// dans le champ Frequence.
import { computed, reactive, ref, useId, watch } from 'vue';
import { txFields, defaultSummary, mhz, type ProfileView } from '../../profiles';
import { withRadio, radioFor } from '../../store';
import api from '../../api/index';
import { BUTTON, FAILURE, FIELD_KIND, SWEEP_STATUS, TONE, TRISTATE, TX_FIELD, type Tone, type TxFieldName } from '../../domain';
import { ICON } from '../../icons';
import OpDetails from '../base/OpDetails.vue';
import OpField from '../base/OpField.vue';
import OpSwitch from '../base/OpSwitch.vue';
import OpSelect from '../base/OpSelect.vue';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import OpProgress from '../base/OpProgress.vue';

export type TxValues = Partial<Record<TxFieldName, number>>;
const props = withDefaults(defineProps<{
  profile: ProfileView | null;
  values?: TxValues;                                    // surcharges de la fiche (0 = heriter du profil)
  open?: boolean;
  tune?: boolean;                                       // propose « Caler sur ma telecommande »
  onSave?: ((values: TxValues) => Promise<unknown>) | null;   // absent : l'ecran lit les valeurs (read)
  saveLabel?: string;
}>(), { values: () => ({}), onSave: null, saveLabel: 'Enregistrer les réglages' });

interface Message { tone: Tone; text: string; running?: boolean }
const fields = computed(() => txFields(props.profile));
const numbers = reactive<Partial<Record<TxFieldName, number>>>({});
const toggles = reactive<Partial<Record<TxFieldName, boolean>>>({});
const opened = ref(props.open);
const message = ref<Message | null>(null);
const tuning = ref<Message | null>(null);
const uid = useId();

function fill(fromProfile: boolean): void {
  for (const f of fields.value) {
    const saved = fromProfile ? 0 : (props.values[f.name] ?? 0);
    if (f.kind === FIELD_KIND.toggle) toggles[f.name] = saved ? saved === TRISTATE.yes : !!f.defaultValue;
    else numbers[f.name] = saved || Number(f.defaultValue);
  }
}
/* Le statut est relu toutes les 3 s : on ne reremplit que si le profil ou la fiche ont vraiment change, sinon la
   saisie en cours serait effacee. */
watch(() => JSON.stringify([props.profile?.id, props.values]), () => fill(false), { immediate: true });

/* Valeurs a envoyer : tri-etat non / oui pour les interrupteurs, comme la fiche du firmware. */
function read(): TxValues {
  const out: TxValues = {};
  for (const f of fields.value) {
    if (f.fixed) continue;
    out[f.name] = f.kind === FIELD_KIND.toggle ? (toggles[f.name] ? TRISTATE.yes : TRISTATE.no) : Number(numbers[f.name]) || 0;
  }
  return out;
}
defineExpose({ read });

function reset(): void {
  fill(true);
  message.value = { tone: TONE.neutral, text: 'Valeurs du profil remises. Enregistre pour les appliquer.' };
}
async function save(): Promise<void> {
  if (!props.onSave) return;
  message.value = null;
  try { await props.onSave(read()); message.value = { tone: TONE.ok, text: 'Réglages enregistrés.' }; }
  catch { message.value = { tone: TONE.danger, text: 'Échec de l\'enregistrement.' }; }
}
async function tuneToRemote(): Promise<void> {
  tuning.value = { tone: TONE.neutral, text: 'Appuie et tiens un bouton de ta télécommande, à 1 ou 2 m du boîtier…', running: true };
  try {
    const r = await withRadio(async () => api.tuneToRemote(await radioFor(props.profile?.band)));
    if (!(r && r.status === SWEEP_STATUS.found && r.center_khz)) throw FAILURE.nothing;
    numbers[TX_FIELD.frequency] = r.center_khz;
    opened.value = true;
    tuning.value = { tone: TONE.ok, text: `Porteuse trouvée : ${mhz(r.center_khz)}, reportée dans les réglages avancés.` };
  } catch (e) {
    tuning.value = e === FAILURE.busy ? null : { tone: TONE.danger, text: 'Rien trouvé. Rapproche la télécommande, tiens le bouton appuyé, recommence.' };
  }
}
const canTune = computed(() => props.tune && fields.value.some(f => f.name === TX_FIELD.frequency && !f.fixed));
</script>

<template>
  <div v-if="profile && fields.length" class="tx-params">
    <div v-if="canTune" class="tuning">
      <OpButton :icon="ICON.remote" :disabled="!!tuning?.running" @click="tuneToRemote">Caler sur ma télécommande</OpButton>
      <OpProgress v-if="tuning?.running" label="Recherche de la porteuse" />
      <OpStatline v-if="tuning" :tone="tuning.tone">{{ tuning.text }}</OpStatline>
    </div>
    <OpDetails v-model:open="opened" :title="`Réglages avancés d'émission · ${profile.brand || profile.name}`" :icon="ICON.system">
      <p class="hint">Pré-réglé depuis le profil <b>{{ profile.brand || profile.name }}</b> : {{ fields.map(defaultSummary).join(' · ') }}.
        Une valeur changée ne vaut que pour cet appareil, même sur la même radio.</p>
      <div class="fields">
        <OpField v-for="f in fields" :key="f.name" :label="f.label" :hint="f.help || ''" :control="`${uid}-${f.name}`">
          <p v-if="f.fixed" class="fixed"><b>{{ defaultSummary(f) }}</b> · fixe pour cette marque</p>
          <input v-else-if="f.kind === FIELD_KIND.number" :id="`${uid}-${f.name}`" v-model.number="numbers[f.name]" class="op-input mono" type="number" step="1">
          <OpSwitch v-else-if="f.kind === FIELD_KIND.toggle" v-model="toggles[f.name]">{{ toggles[f.name] ? 'Activé' : 'Désactivé' }}</OpSwitch>
          <OpSelect v-else-if="f.kind === FIELD_KIND.choice" :id="`${uid}-${f.name}`" v-model="numbers[f.name]" :options="f.options ?? []" />
        </OpField>
      </div>
      <div class="row">
        <OpButton v-if="onSave" :variant="BUTTON.primary" @click="save">{{ saveLabel }}</OpButton>
        <OpButton :variant="BUTTON.ghost" title="Remet les champs sur les valeurs du profil" @click="reset">↺ Valeurs du profil</OpButton>
      </div>
      <OpStatline v-if="message" :tone="message.tone">{{ message.text }}</OpStatline>
    </OpDetails>
  </div>
</template>

<style scoped>
.tx-params{display:grid;gap:12px}
.tuning{display:grid;gap:8px;justify-items:start}
.tuning :deep(.op-progress),.tuning :deep(.op-statline){justify-self:stretch}
.fields{display:grid;gap:14px}
@media (min-width:620px){.fields{grid-template-columns:1fr 1fr}}
.fixed{margin:0;font-size:.88rem}
</style>
