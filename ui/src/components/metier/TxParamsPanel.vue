<script setup>
// Les reglages d'emission PAR APPAREIL : porteuse, TE, bit de repetition, RMT, format de trame Somfy.
// Un seul composant a l'ajout (pre-rempli depuis le profil) et a l'edition (depuis la fiche). Les champs
// viennent du profil (champsEmission), jamais d'une liste en dur. « Caler sur ma telecommande » cherche la
// porteuse et la reporte dans le champ Frequence.
import { computed, reactive, ref, useId, watch } from 'vue';
import { champsEmission, resumeDefaut } from '../../profils.js';
import { avecRadio, radioPour } from '../../store.js';
import api from '../../api/index.js';
import OpDetails from '../base/OpDetails.vue';
import OpField from '../base/OpField.vue';
import OpSwitch from '../base/OpSwitch.vue';
import OpSelect from '../base/OpSelect.vue';
import OpButton from '../base/OpButton.vue';
import OpStatline from '../base/OpStatline.vue';
import OpProgress from '../base/OpProgress.vue';

const props = defineProps({
  profil: { type: Object, default: null },
  valeurs: { type: Object, default: () => ({}) },   // surcharges de la fiche (0 = heriter du profil)
  ouvert: Boolean,
  calage: Boolean,
  surEnregistrer: { type: Function, default: null },   // (valeurs) => Promise ; absent : l'ecran lit les valeurs
  libelle: { type: String, default: 'Enregistrer les réglages' },
});

const champs = computed(() => champsEmission(props.profil));
const v = reactive({});
const ouvre = ref(props.ouvert);
const statut = ref(null);     // { ton, texte }
const cal = ref(null);        // { ton, texte, cours }
const uid = useId();

function remplir(depuisProfil) {
  for (const f of champs.value) {
    const s = depuisProfil ? 0 : props.valeurs[f.nom];
    v[f.nom] = f.genre === 'oui-non' ? (s ? s === 2 : !!f.def) : (s || f.def);
  }
}
/* Le statut est relu chaque seconde : on ne reremplit que si le profil ou la fiche ont vraiment change,
   sinon la saisie en cours serait effacee. */
watch(() => JSON.stringify([props.profil && props.profil.marque, props.valeurs]), () => remplir(false), { immediate: true });

/* Valeurs a envoyer : tri-etat 1 (non) / 2 (oui) pour les interrupteurs, comme la fiche du firmware. */
function lire() {
  const out = {};
  for (const f of champs.value) {
    if (f.fixe) continue;
    out[f.nom] = f.genre === 'oui-non' ? (v[f.nom] ? 2 : 1) : Number(v[f.nom]) || 0;
  }
  return out;
}
defineExpose({ lire });

function remettre() {
  remplir(true);
  statut.value = { ton: 'neutre', texte: 'Valeurs du profil remises. Enregistre pour les appliquer.' };
}
async function enregistrer() {
  statut.value = null;
  try { await props.surEnregistrer(lire()); statut.value = { ton: 'ok', texte: 'Réglages enregistrés.' }; }
  catch { statut.value = { ton: 'danger', texte: 'Échec de l\'enregistrement.' }; }
}
async function caler() {
  cal.value = { ton: 'neutre', texte: 'Appuie et tiens un bouton de ta télécommande, à 1 ou 2 m du boîtier…', cours: true };
  try {
    const r = await avecRadio(async () => api.caler(await radioPour(props.profil.band)));
    if (!(r && r.status === 0 && r.center_khz)) throw 'rien';
    v.freq_khz = r.center_khz;
    ouvre.value = true;
    cal.value = { ton: 'ok', texte: `Porteuse trouvée : ${(r.center_khz / 1000).toFixed(3).replace('.', ',')} MHz, reportée dans les réglages avancés.` };
  } catch (e) {
    cal.value = e === 'occupee' ? null : { ton: 'danger', texte: 'Rien trouvé. Rapproche la télécommande, tiens le bouton appuyé, recommence.' };
  }
}
</script>

<template>
  <div v-if="profil && champs.length" class="tx-params">
    <div v-if="calage && champs.some(f => f.nom === 'freq_khz' && !f.fixe)" class="calage">
      <OpButton icon="telec" :disabled="cal && cal.cours" @click="caler">Caler sur ma télécommande</OpButton>
      <OpProgress v-if="cal && cal.cours" label="Recherche de la porteuse" />
      <OpStatline v-if="cal" :tone="cal.ton">{{ cal.texte }}</OpStatline>
    </div>
    <OpDetails v-model:open="ouvre" :titre="`Réglages avancés d'émission · ${profil.marque}`" icon="sys">
      <p class="hint">Pré-réglé depuis le profil <b>{{ profil.marque }}</b> : {{ champs.map(resumeDefaut).join(' · ') }}.
        Une valeur changée ne vaut que pour cet appareil, même sur la même radio.</p>
      <div class="champs">
        <OpField v-for="f in champs" :key="f.nom" :label="f.label" :hint="f.aide || ''" :pour="`${uid}-${f.nom}`">
          <p v-if="f.fixe" class="fixe"><b>{{ resumeDefaut(f) }}</b> · fixe pour cette marque</p>
          <input v-else-if="f.genre === 'nombre'" :id="`${uid}-${f.nom}`" v-model.number="v[f.nom]" class="op-input mono" type="number" step="1">
          <OpSwitch v-else-if="f.genre === 'oui-non'" v-model="v[f.nom]">{{ v[f.nom] ? 'Activé' : 'Désactivé' }}</OpSwitch>
          <OpSelect v-else-if="f.genre === 'choix'" :id="`${uid}-${f.nom}`" v-model="v[f.nom]" :options="f.options" />
        </OpField>
      </div>
      <div class="rangee">
        <OpButton v-if="surEnregistrer" variant="primary" @click="enregistrer">{{ libelle }}</OpButton>
        <OpButton variant="ghost" title="Remet les champs sur les valeurs du profil" @click="remettre">↺ Valeurs du profil</OpButton>
      </div>
      <OpStatline v-if="statut" :tone="statut.ton">{{ statut.texte }}</OpStatline>
    </OpDetails>
  </div>
</template>

<style scoped>
.tx-params{display:grid;gap:12px}
.calage{display:grid;gap:8px;justify-items:start}
.calage :deep(.op-progress),.calage :deep(.op-statline){justify-self:stretch}
.champs{display:grid;gap:14px}
@media (min-width:620px){.champs{grid-template-columns:1fr 1fr}}
.fixe{margin:0;font-size:.88rem}
</style>
