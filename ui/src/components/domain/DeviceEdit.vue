<script setup lang="ts">
// L'edition d'un appareil, sous sa ligne dans « Mes appareils » : nom, type, profil (marque), orientation et zone
// pour un volet, reglages d'emission, suppression. Les choix possibles viennent de profiles.json.
import { computed, ref, watch } from 'vue';
import { state, refresh, toast } from '../../store';
import { navigate } from '../../router';
import { profiles, types, typeKey, kindOf, profileOf, mechanism, isSomfy, hasComma } from '../../profiles';
import api from '../../api/index';
import type { Device } from '../../api/types';
import { BUTTON, EXISTING_DEVICE, SECTION, TAB, TONE } from '../../domain';
import { ICON } from '../../icons';
import OpField from '../base/OpField.vue';
import OpSelect from '../base/OpSelect.vue';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import ChipPicker from './ChipPicker.vue';
import TxParamsPanel, { type TxValues } from './TxParamsPanel.vue';

const props = defineProps<{ device: Device }>();
const emit = defineEmits<{ renamed: [name: string]; deleted: [] }>();
const NAME_EXISTS = 'exists';   // refus du firmware : un appareil porte deja ce nom

const kind = computed(() => kindOf(props.device));
const uid = props.device.id.replace(/\W/g, '') || 'dev';
const name = ref(props.device.id);
const chosenType = ref<string | number>(typeKey(props.device));
const typeOptions = computed(() => Object.entries(types()).map(([key, t]) => ({ value: key, label: t.label })));
const isPositional = (d: Device): boolean => !d.central && !d.zone && !!types()[typeKey(d)]?.positional;
const positional = computed(() => isPositional(props.device));

/* Profils interchangeables : meme mecanisme et meme bande (un enrole Profalux 868 peut devenir Eveno ou NeoSol ;
   l'identite et le compteur restent). */
const savedProfile = computed(() => profileOf(props.device));
const brand = ref<string | number>(savedProfile.value ? savedProfile.value.marque : '');
const compatible = computed(() => profiles().filter(p => savedProfile.value && p.mecanisme === mechanism(props.device) && p.band === savedProfile.value.band));
const profile = computed(() => compatible.value.find(p => p.marque === brand.value) ?? savedProfile.value);
const values = computed<TxValues>(() => (profile.value && savedProfile.value && profile.value.marque !== savedProfile.value.marque ? {}
  : { freq_khz: props.device.freq_khz || 0, te_us: props.device.te_us || 0, tx_rpt: props.device.tx_rpt || 0, tx_rmt: props.device.tx_rmt || 0, frame_bits: props.device.frame_bits || 0 }));

const orientation = ref<number | ''>((props.device.orientation ?? -1) >= 0 ? (props.device.orientation ?? '') : '');
/* Une zone : un appareil dont la telecommande commande aussi d'autres volets (positionnels, pas une autre zone ni
   une centrale). Elle n'a plus de position propre, comme une centrale. */
const canBeZone = computed(() => !props.device.central && (!!props.device.zone || positional.value));
const zoneCandidates = computed(() => (state.status?.volets ?? [])
  .filter(x => x.id !== props.device.id && isPositional(x))
  .map(x => ({ value: x.id, label: x.id })));
const followers = ref<string | string[]>((props.device.members || '').split(',').filter(Boolean));
const confirming = ref(false);
const error = ref('');
watch(() => props.device.id, id => { name.value = id; });

async function rename(): Promise<void> {
  const next = name.value.trim();
  error.value = '';
  if (!next || next === props.device.id) return;
  if (hasComma(next)) { error.value = 'Pas de virgule dans le nom.'; return; }
  try { await api.rename(props.device.id, next); toast('Renommé'); emit('renamed', next); refresh(); }
  catch (e) { error.value = e === NAME_EXISTS ? 'Ce nom existe déjà.' : 'Échec du renommage.'; }
}
async function changeType(key: string | number): Promise<void> {
  try { await api.setType(props.device.id, types()[String(key)]?.code ?? 0); toast('Type changé'); }
  catch { toast('Refusé : type inchangé'); }
  refresh();
}
async function saveOrientation(): Promise<void> {
  const value = orientation.value === '' ? -1 : Math.round(Number(orientation.value)) % 360;
  try { await api.setOrientation(props.device.id, value); toast('Orientation enregistrée'); }
  catch { toast('Refusé : orientation non enregistrée'); }
  refresh();
}
async function saveZone(): Promise<void> {
  const list = Array.isArray(followers.value) ? followers.value : [];
  try {
    await api.setFollowers(props.device.id, list);
    toast(list.length ? 'Zone enregistrée' : 'Plus de volets suiveurs');
    refresh();
  } catch (e) { toast(`Échec de l'enregistrement${typeof e === 'string' ? ` : ${e}` : ''}`); }
}
/* Un volet qui suit deja une zone ne peut pas en etre une. */
const follows = computed(() => props.device.follows ?? []);
/* L'ecran de capture, sur CET appareil : recapturer ses commandes, ou apprendre la telecommande d'origine d'un
   volet virtuel (suivi de position). Rien pour une centrale ni un volet Somfy. */
const captureLabel = computed(() => {
  const d = props.device;
  if (d.central || (d.virt && isSomfy(d))) return null;
  return d.virt ? 'Apprendre sa télécommande d\'origine (suivi de position)' : 'Capturer ou recapturer ses commandes';
});
const openCapture = (): void => navigate(`${TAB.remotes}/${SECTION.learn}/${EXISTING_DEVICE}/${encodeURIComponent(props.device.id)}`);
const saveTx = (txValues: TxValues): Promise<void> => api.saveTxParams({ id: props.device.id, marque: brand.value, ...txValues }).then(refresh);
async function remove(): Promise<void> {
  try { await api.deleteDevice(props.device.id); }
  catch { toast('Refusé : appareil non supprimé'); confirming.value = false; return; }
  toast('Appareil supprimé');
  emit('deleted');
  refresh();
}
</script>

<template>
  <div class="editor">
    <OpField label="Nom de l'appareil" :control="`${uid}-name`"
             hint="Le nom sert d'identifiant (zones, centrales, entité Home Assistant) : le renommer met ces références à jour. Côté Home Assistant, une nouvelle entité apparaît.">
      <div class="row line">
        <input :id="`${uid}-name`" v-model="name" class="op-input" :maxlength="state.profiles?.name_max || 23">
        <OpButton :disabled="name.trim() === device.id" @click="rename">Renommer</OpButton>
      </div>
    </OpField>
    <OpStatline v-if="error" :tone="TONE.danger">{{ error }}</OpStatline>

    <div class="grid two">
      <OpField v-if="!device.central && !device.zone && typeOptions.length > 1" label="Type d'appareil" :control="`${uid}-type`"
               hint="Si l'appareil a été rangé au mauvais endroit. Identité, compteur et captures sont gardés.">
        <OpSelect :id="`${uid}-type`" v-model="chosenType" :options="typeOptions" @update:model-value="changeType" />
      </OpField>
      <OpField v-if="kind.hasTx && compatible.length > 1" label="Profil (marque)" :control="`${uid}-profile`"
               hint="La fréquence et les réglages par défaut suivent. L'identité et le compteur sont gardés.">
        <OpSelect :id="`${uid}-profile`" v-model="brand" :options="compatible.map(p => ({ value: p.marque, label: p.nom }))" />
      </OpField>
      <OpField v-else-if="kind.hasTx" label="Profil"><p class="value">{{ profile ? profile.nom : (device.marque || 'non défini') }}</p></OpField>
    </div>

    <OpField v-if="positional" label="Orientation de la façade (azimut)" :control="`${uid}-orientation`" hint="Pour les automatisations soleil. Nord 0 · Est 90 · Sud 180 · Ouest 270. Vide : non défini.">
      <div class="row line">
        <input :id="`${uid}-orientation`" v-model="orientation" class="op-input mono" type="number" min="0" max="359" placeholder="180 = sud">
        <OpButton @click="saveOrientation">Enregistrer</OpButton>
      </div>
    </OpField>
    <OpField v-if="canBeZone" label="Zone : volets qui suivent cette télécommande"
             hint="Pour une télécommande de pièce qui commande plusieurs volets. Les volets choisis suivent ses mouvements dans l'application, sans émission en plus.">
      <p v-if="follows.length" class="hint">Ce volet suit {{ follows.map(z => `« ${z} »`).join(', ') }} : il ne peut pas être une zone.
        Décoche-le d'abord dans {{ follows.length > 1 ? 'ces zones' : 'cette zone' }}.</p>
      <template v-else>
        <ChipPicker v-model="followers" multiple :options="zoneCandidates" label="Volets suiveurs" empty="Aucun volet simple à cocher." />
        <div class="row"><OpButton small @click="saveZone">Enregistrer la zone</OpButton></div>
      </template>
    </OpField>
    <div v-if="captureLabel" class="row"><OpButton small :icon="ICON.remote" @click="openCapture">{{ captureLabel }}</OpButton></div>

    <TxParamsPanel v-if="kind.hasTx" :profile="profile" :values="values" open :on-save="saveTx" />
    <p v-else-if="device.central" class="hint">Pas de réglages d'émission pour une centrale : elle émet avec ceux de ses volets.</p>

    <div class="row end">
      <template v-if="confirming">
        <span class="hint">Supprimer « {{ device.id }} » ?</span>
        <OpButton small @click="confirming = false">Annuler</OpButton>
        <OpButton small :variant="BUTTON.danger" @click="remove">Oui, supprimer</OpButton>
      </template>
      <OpButton v-else :variant="BUTTON.danger" small @click="confirming = true">Supprimer cet appareil</OpButton>
    </div>
  </div>
</template>

<style scoped>
.editor{display:grid;gap:16px;padding:16px 14px;border:1px solid var(--accent-line);border-top:0;border-radius:0 0 var(--radius-sm) var(--radius-sm);background:var(--surface)}
.line{flex-wrap:nowrap}
.line .op-input{flex:1;min-width:0}
.value{margin:0;font-size:.9rem;font-weight:600}
</style>
