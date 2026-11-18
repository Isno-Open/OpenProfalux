<script setup>
// Creer un volet Somfy RTS (433,42 MHz) : pas besoin de telecommande d'origine. On choisit une adresse
// (24 bits), le boitier la fait apprendre au moteur en mode appairage, puis on teste.
import { computed, ref } from 'vue';
import { aller } from '../../router.js';
import { etat, envoyer, rafraichir, typeDe } from '../../store.js';
import { aVirgule } from '../../profils.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpSteps from '../../components/base/OpSteps.vue';
import ShutterControl from '../../components/metier/ShutterControl.vue';

const nom = ref('');
const adresse = ref('');
const cree = ref('');          // id du volet cree
const statut = ref(null);
const appaire = ref(false);
const etape = computed(() => (!cree.value ? 0 : !appaire.value ? 1 : 2));
const dpad = computed(() => typeDe({ type: 0 }).dpad);
const envoi = computed(() => (etat.envoi && etat.envoi.id === cree.value ? etat.envoi.cmd : ''));

function generer() {
  adresse.value = `0x${Math.floor(Math.random() * 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0')}`;
}
async function creer() {
  const n = nom.value.trim();
  if (!n || aVirgule(n)) { statut.value = { ton: 'danger', texte: 'Donne un nom au volet, sans virgule.' }; return; }
  if (!/^0x[0-9a-f]{6}$/i.test(adresse.value.trim())) { statut.value = { ton: 'danger', texte: 'L\'adresse fait 6 chiffres hexadécimaux, par exemple 0x1A2B3C. Clique Générer.' }; return; }
  try {
    await api.somfyCreer({ id: n, address: adresse.value.trim() });
    cree.value = n;
    statut.value = { ton: 'ok', texte: `Volet « ${n} » créé. Appaire maintenant le moteur.` };
    rafraichir();
  } catch (e) {
    statut.value = { ton: 'danger', texte: e === 'exists' ? 'Ce nom existe déjà.' : 'Échec de la création.' };
  }
}
async function appairer() {
  await envoyer({ id: cree.value, cmd: 'prog' });
  appaire.value = true;
}
</script>

<template>
  <OpCard title="Créer un volet Somfy" sub="Moteur Somfy RTS, 433,42 MHz. Pas besoin de télécommande d'origine : on choisit une adresse et le moteur l'apprend.">
    <template #avant><button type="button" class="lien-retour" @click="aller('remotes/add/volet')">‹ Ajouter un appareil</button></template>
    <div class="flux">
      <OpSteps :etapes="['Créer', 'Appairer', 'Tester']" :actuelle="etape" />
      <OpField label="Nom du volet" pour="somfy-nom">
        <input id="somfy-nom" v-model="nom" class="op-input" placeholder="ex : Volet salon Somfy" maxlength="23" :disabled="!!cree">
      </OpField>
      <OpField label="Adresse (24 bits)" pour="somfy-adr" hint="L'identité de la télécommande virtuelle. Garde-la : c'est elle que le moteur retient.">
        <div class="rangee ligne">
          <input id="somfy-adr" v-model="adresse" class="op-input mono" placeholder="0x123456" spellcheck="false" autocomplete="off" :disabled="!!cree">
          <OpButton :disabled="!!cree" @click="generer">Générer</OpButton>
        </div>
      </OpField>
      <div class="rangee"><OpButton variant="primary" :disabled="!!cree" @click="creer">Créer le volet</OpButton></div>
      <OpStatline v-if="statut" :tone="statut.ton">{{ statut.texte }}</OpStatline>

      <section v-if="cree" class="etape">
        <h3 class="titre-section">Appairer le moteur</h3>
        <p class="hint">Mets le moteur en attente d'appairage : appui long sur le bouton PROG de sa télécommande d'origine, le moteur fait
          un va-et-vient. Puis clique : le boîtier émet la commande d'appairage. Un nouveau va-et-vient : c'est appairé.</p>
        <div class="rangee"><OpButton :variant="appaire ? 'normal' : 'primary'" icon="telec" :disabled="etat.radioOccupee" @click="appairer">Émettre l'appairage</OpButton></div>
        <OpStatline v-if="appaire" tone="ok">Appairage émis. Le moteur a fait un va-et-vient ? Teste ci-dessous.</OpStatline>
      </section>
      <section v-if="appaire" class="etape">
        <h3 class="titre-section">Tester</h3>
        <ShutterControl class="pave-test" :libelles="dpad" :envoi="envoi" :disabled="etat.radioOccupee" @ordre="cmd => envoyer({ id: cree, cmd })" />
        <p class="hint">Le volet est aussi dans l'onglet <b>Volets</b> et pilotable depuis Home Assistant.</p>
      </section>
    </div>
  </OpCard>
</template>

<style scoped>
.flux{display:grid;gap:14px}
.etape{display:grid;gap:12px;padding-left:14px;border-left:2px solid var(--accent-line)}
.ligne{flex-wrap:nowrap}
.ligne .op-input{flex:1;min-width:0}
.pave-test{max-width:360px}
</style>
