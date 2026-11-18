<script setup>
// L'edition d'un appareil, sous sa ligne dans « Mes appareils » : nom, type, profil (marque), orientation
// et zone pour un volet, reglages d'emission, suppression. Les choix possibles viennent de profiles.json.
import { computed, ref, watch } from 'vue';
import { etat, rafraichir, toast } from '../../store.js';
import { aller } from '../../router.js';
import { profils, types, cleType, genre, profilDe, mecanisme, aVirgule } from '../../profils.js';
import api from '../../api/index.js';
import OpField from '../base/OpField.vue';
import OpSelect from '../base/OpSelect.vue';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import ChipPicker from './ChipPicker.vue';
import TxParamsPanel from './TxParamsPanel.vue';

const props = defineProps({ v: { type: Object, required: true } });
const emit = defineEmits(['renomme', 'supprime']);

const g = computed(() => genre(props.v));
const uid = props.v.id.replace(/\W/g, '') || 'app';
const nom = ref(props.v.id);
const typeChoisi = ref(cleType(props.v));
const optionsTypes = computed(() => Object.entries(types()).map(([k, t]) => ({ value: k, label: t.label })));
const positionnel = computed(() => !props.v.central && !props.v.zone && !!(types()[cleType(props.v)] || {}).positional);

/* Profils interchangeables : meme mecanisme et meme bande (un enrole Profalux 868 peut devenir Eveno ou
   NeoSol ; l'identite et le compteur restent). */
const profilFiche = computed(() => profilDe(props.v));
const marque = ref(profilFiche.value ? profilFiche.value.marque : '');
const compatibles = computed(() => profils().filter(p => profilFiche.value && p.mecanisme === mecanisme(props.v) && p.band === profilFiche.value.band));
const profil = computed(() => compatibles.value.find(p => p.marque === marque.value) || profilFiche.value);
const valeurs = computed(() => (profil.value && profilFiche.value && profil.value.marque !== profilFiche.value.marque ? {}
  : { freq_khz: props.v.freq_khz || 0, te_us: props.v.te_us || 0, tx_rpt: props.v.tx_rpt || 0, tx_rmt: props.v.tx_rmt || 0, frame_bits: props.v.frame_bits || 0 }));

const orientation = ref(props.v.orientation >= 0 ? props.v.orientation : '');
/* Une zone : un appareil dont la telecommande commande aussi d'autres volets (positionnels, pas une autre
   zone ni une centrale). Elle n'a plus de position propre, comme une centrale. */
const zonable = computed(() => !props.v.central && (!!props.v.zone || positionnel.value));
const candidatsZone = computed(() => ((etat.statut && etat.statut.volets) || [])
  .filter(x => x.id !== props.v.id && !x.central && !x.zone && !!(types()[cleType(x)] || {}).positional)
  .map(x => ({ value: x.id, label: x.id })));
const suiveurs = ref((props.v.members || '').split(',').filter(Boolean));
const confirmer = ref(false);
const erreur = ref('');
watch(() => props.v.id, id => { nom.value = id; });

async function renommer() {
  const n = nom.value.trim();
  erreur.value = '';
  if (!n || n === props.v.id) return;
  if (aVirgule(n)) { erreur.value = 'Pas de virgule dans le nom.'; return; }
  try { await api.renommer(props.v.id, n); toast('Renommé'); emit('renomme', n); rafraichir(); }
  catch (e) { erreur.value = e === 'exists' ? 'Ce nom existe déjà.' : 'Échec du renommage.'; }
}
async function changerType(cle) {
  await api.changerType(props.v.id, (types()[cle] || {}).code || 0);
  toast('Type changé');
  rafraichir();
}
async function enregistrerOrientation() {
  const o = orientation.value === '' ? -1 : Math.round(Number(orientation.value)) % 360;
  await api.orientation(props.v.id, o);
  toast('Orientation enregistrée');
  rafraichir();
}
async function enregistrerZone() {
  try {
    await api.suiveurs(props.v.id, suiveurs.value);
    toast(suiveurs.value.length ? 'Zone enregistrée' : 'Plus de volets suiveurs');
    rafraichir();
  } catch (e) { toast(`Échec de l'enregistrement${typeof e === 'string' ? ` : ${e}` : ''}`); }
}
/* Un volet qui suit deja une zone ne peut pas en etre une. */
const suit = computed(() => props.v.follows || []);
/* L'ecran de capture, sur CET appareil : recapturer ses commandes, ou apprendre la telecommande d'origine
   d'un volet virtuel (suivi de position). Rien pour une centrale ni un volet Somfy. */
const capture = computed(() => {
  const v = props.v;
  if (v.central || (v.virt && v.proto === 'somfy')) return null;
  return v.virt ? 'Apprendre sa télécommande d\'origine (suivi de position)' : 'Capturer ou recapturer ses commandes';
});
const enregistrerEmission = vals => api.emission({ id: props.v.id, marque: marque.value, ...vals }).then(rafraichir);
async function supprimer() {
  await api.supprimer(props.v.id);
  toast('Appareil supprimé');
  emit('supprime');
  rafraichir();
}
</script>

<template>
  <div class="edition">
    <OpField label="Nom de l'appareil" :pour="`${uid}-nom`"
             hint="Le nom sert d'identifiant (zones, centrales, entité Home Assistant) : le renommer met ces références à jour. Côté Home Assistant, une nouvelle entité apparaît.">
      <div class="rangee ligne">
        <input :id="`${uid}-nom`" v-model="nom" class="op-input" :maxlength="(etat.profils && etat.profils.name_max) || 23">
        <OpButton :disabled="nom.trim() === v.id" @click="renommer">Renommer</OpButton>
      </div>
    </OpField>
    <OpStatline v-if="erreur" tone="danger">{{ erreur }}</OpStatline>

    <div class="grille deux">
      <OpField v-if="!v.central && !v.zone && optionsTypes.length > 1" label="Type d'appareil" :pour="`${uid}-type`"
               hint="Si l'appareil a été rangé au mauvais endroit. Identité, compteur et captures sont gardés.">
        <OpSelect :id="`${uid}-type`" v-model="typeChoisi" :options="optionsTypes" @update:model-value="changerType" />
      </OpField>
      <OpField v-if="g.tx && compatibles.length > 1" label="Profil (marque)" :pour="`${uid}-profil`"
               hint="La fréquence et les réglages par défaut suivent. L'identité et le compteur sont gardés.">
        <OpSelect :id="`${uid}-profil`" v-model="marque" :options="compatibles.map(p => ({ value: p.marque, label: p.nom }))" />
      </OpField>
      <OpField v-else-if="g.tx" label="Profil"><p class="valeur">{{ profil ? profil.nom : (v.marque || 'non défini') }}</p></OpField>
    </div>

    <OpField v-if="positionnel" label="Orientation de la façade (azimut)" :pour="`${uid}-ori`" hint="Pour les automatisations soleil. Nord 0 · Est 90 · Sud 180 · Ouest 270. Vide : non défini.">
      <div class="rangee ligne">
        <input :id="`${uid}-ori`" v-model="orientation" class="op-input mono" type="number" min="0" max="359" placeholder="180 = sud">
        <OpButton @click="enregistrerOrientation">Enregistrer</OpButton>
      </div>
    </OpField>
    <OpField v-if="zonable" label="Zone : volets qui suivent cette télécommande"
             hint="Pour une télécommande de pièce qui commande plusieurs volets. Les volets choisis suivent ses mouvements dans l'application, sans émission en plus.">
      <p v-if="suit.length" class="hint">Ce volet suit {{ suit.map(z => `« ${z} »`).join(', ') }} : il ne peut pas être une zone.
        Décoche-le d'abord dans {{ suit.length > 1 ? 'ces zones' : 'cette zone' }}.</p>
      <template v-else>
        <ChipPicker v-model="suiveurs" multiple :options="candidatsZone" label="Volets suiveurs" vide="Aucun volet simple à cocher." />
        <div class="rangee"><OpButton small @click="enregistrerZone">Enregistrer la zone</OpButton></div>
      </template>
    </OpField>
    <div v-if="capture" class="rangee"><OpButton small icon="telec" @click="aller(`remotes/learn/-/${encodeURIComponent(v.id)}`)">{{ capture }}</OpButton></div>

    <TxParamsPanel v-if="g.tx" :profil="profil" :valeurs="valeurs" ouvert :sur-enregistrer="enregistrerEmission" />
    <p v-else-if="v.central" class="hint">Pas de réglages d'émission pour une centrale : elle émet avec ceux de ses volets.</p>

    <div class="rangee fin">
      <template v-if="confirmer">
        <span class="hint">Supprimer « {{ v.id }} » ?</span>
        <OpButton small @click="confirmer = false">Annuler</OpButton>
        <OpButton small variant="danger" @click="supprimer">Oui, supprimer</OpButton>
      </template>
      <OpButton v-else variant="danger" small @click="confirmer = true">Supprimer cet appareil</OpButton>
    </div>
  </div>
</template>

<style scoped>
.edition{display:grid;gap:16px;padding:16px 14px;border:1px solid var(--accent-line);border-top:0;border-radius:0 0 var(--radius-sm) var(--radius-sm);background:var(--surface)}
.ligne{flex-wrap:nowrap}
.ligne .op-input{flex:1;min-width:0}
.valeur{margin:0;font-size:.9rem;font-weight:600}
</style>
