<script setup>
// Creer une telecommande virtuelle (enrolement) : le boitier devient une nouvelle telecommande, que le
// moteur apprend. Cinq etapes : capturer la telecommande d'origine (le boitier attribue une identite,
// nouvelle si le moteur est inconnu, la meme s'il est deja connu), emettre la trame d'apprentissage (~5 s),
// faire les gestes, tester apres le delai de confirmation du moteur (~10 s), enregistrer comme volet.
// L'etat vit dans le boitier (/api/pfx) : une identite en cours se reprend apres un F5. La marque choisie
// dans la pyramide est dans l'adresse (#remotes/enrol/<index>).
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { route, aller } from '../../router.js';
import { etat, rafraichir, toast } from '../../store.js';
import { profils, types, aVirgule } from '../../profils.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpSelect from '../../components/base/OpSelect.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import OpSteps from '../../components/base/OpSteps.vue';
import OpProgress from '../../components/base/OpProgress.vue';
import TxParamsPanel from '../../components/metier/TxParamsPanel.vue';
import ShutterControl from '../../components/metier/ShutterControl.vue';

/* Gestes PHYSIQUES a faire APRES l'emission, par routine du moteur (pfx_enrol.c). */
const GESTES = {
  R6: ['Monte le volet jusqu\'en butée haute.', 'Descends de ~4 lames.', 'Remonte jusqu\'en butée haute.',
    'Le volet fait un va-et-vient = enrôlé ✅. S\'il ne bouge pas, attends ~10 s puis teste ▲ ■ ▼ quand même : beaucoup de moteurs confirment sans bouger.'],
  R7: ['Monte → descends ~4 lames → remonte (procédure proche, à confirmer).',
    'Va-et-vient = enrôlé. S\'il ne bouge pas, attends ~10 s puis teste ▲ ■ ▼ quand même. Rapporte le résultat sur GitHub.'],
  R8: ['Montée → laisse le volet s\'arrêter seul en butée haute.', 'Descente → laisse ~4 lames apparentes.', 'Montée → laisse s\'arrêter seul.',
    'Le volet fait un va-et-vient = enrôlé ✅. S\'il ne bouge pas, attends ~10 s puis teste ▲ ■ ▼ quand même. Rapporte le résultat sur GitHub.'],
};
/* Geste a faire AVANT l'emission (certains modeles partent d'une position de reference). */
const AVANT = { R8: 'NeoSol : AVANT d\'émettre, descends le volet et laisse ~4 lames apparentes.' };
const DELAI_MOTEUR = 10;   // secondes de confirmation du moteur avant de tester

const profil = computed(() => {
  const p = profils()[parseInt(route.suite[0], 10)];
  return p && p.flow === 'enrol-pfx' ? p : null;
});
const pfx = ref(null);         // /api/pfx
const modele = ref(0);
const panneau = ref(null);
const capture = ref(null);     // { ton, texte, cours, plus }
const renouvelable = ref(false);
const emission = ref(null);    // { ton, texte, cours }
const gestes = ref(false);     // emission finie : les gestes sont a faire
const attente = ref(-1);       // secondes restantes ; 0 = test permis ; -1 = gestes pas encore faits
const nom = ref('');
const fin = ref(null);
const envoi = ref('');
let minuteur = null;

const modeles = computed(() => (pfx.value && pfx.value.models) || []);
const optionsModeles = computed(() => modeles.value.map((m, i) => ({ value: i, label: `${m.name} · ${m.routine}` })));
const routine = computed(() => (modeles.value[modele.value] || {}).routine || 'R6');
const actif = computed(() => !!(pfx.value && pfx.value.active));
const dpad = computed(() => ((types().volet || {}).dpad) || { up: 'Montée', stop: 'Stop', down: 'Descente' });
const etape = computed(() => (fin.value && fin.value.ton === 'ok' ? 5 : !actif.value ? 0 : !gestes.value ? 1 : attente.value !== 0 ? 2 : 3));
const ligneIdentite = computed(() => {
  const d = pfx.value;
  if (!d || !d.active) return '';
  return `Identité ${d.serial} · slot ${d.idx + 1}/63 · compteur ${d.counter}${d.remote && d.remote !== '0x0000000' ? ` · télécommande ${d.remote}` : ''}`;
});

async function lire() {
  const d = await api.pfxEtat().catch(() => null);
  if (!d) return;
  pfx.value = d;
  if (d.active) modele.value = d.model;
}
onMounted(async () => {
  await lire();
  if (!actif.value && profil.value) {
    const i = modeles.value.findIndex(m => m.name.includes(profil.value.marque));
    if (i >= 0) modele.value = i;
  }
});
onUnmounted(() => clearInterval(minuteur));

/* Reglages du panneau -> corps de /api/pfx/capture : le firmware lit des booleens, une option absente
   reste « heriter », une frequence ou un TE a 0 n'est pas envoye. */
function corpsCapture() {
  const v = panneau.value ? panneau.value.lire() : {}, b = { model: Number(modele.value) };
  if (v.freq_khz) b.freq_khz = v.freq_khz;
  if (v.te_us) b.te_us = v.te_us;
  if (v.tx_rpt) b.tx_repeat_bit = v.tx_rpt === 2;
  if (v.tx_rmt) b.tx_rmt = v.tx_rmt === 2;
  if (profil.value && profil.value.marque) b.marque = profil.value.marque;
  return b;
}
function reprendre() { gestes.value = false; attente.value = -1; emission.value = null; clearInterval(minuteur); }

async function capturer() {
  if (etat.radioOccupee) { toast('Radio occupée, réessaie dans un instant'); return; }
  etat.radioOccupee = true;
  renouvelable.value = false;
  reprendre();
  let n = 15;
  capture.value = { ton: 'neutre', texte: `Appuie MAINTENANT sur ta télécommande d'origine… ${n} s`, cours: true };
  const compte = setInterval(() => { n = Math.max(0, n - 1); capture.value = { ...capture.value, texte: `Appuie MAINTENANT sur ta télécommande d'origine… ${n} s` }; }, 1000);
  let d = null;
  try { d = await api.pfxCapturer(corpsCapture()); }
  catch (e) { d = e === 'demarrage' ? { echec: 'Échec du démarrage de la capture' } : null; }
  finally { clearInterval(compte); etat.radioOccupee = false; }
  if (!d) capture.value = { ton: 'danger', texte: 'Aucune réponse du boîtier' };
  else if (d.echec) capture.value = { ton: 'danger', texte: d.echec };
  else if (d.rc === 0 && d.new) capture.value = { ton: 'ok', texte: `Nouveau moteur : identité ${d.serial} attribuée (télécommande ${d.remote})` };
  else if (d.rc === 0) {
    /* Identite reprise : proposer une identite neuve, au cas ou d'autres moteurs la connaissent (issue #53). */
    capture.value = { ton: 'ok', texte: `Moteur déjà connu : même identité ${d.serial} (télécommande ${d.remote})`,
      plus: d.used_by ? `Cette identité pilote déjà le volet virtuel « ${d.used_by} » : tout moteur qui la connaît bouge avec lui.` : '' };
    renouvelable.value = true;
  }
  else if (d.rc === -1) capture.value = { ton: 'danger', texte: '63 identités déjà attribuées : oublie-en une pour libérer un slot.' };
  /* Capture ratee : l'identite d'avant est deselectionnee, rien ne partira avec elle (issue #53). */
  else if (d.rc === -3) capture.value = { ton: 'danger', texte: 'La capture a échoué : le boîtier n\'a rien reçu. Rapproche la télécommande du boîtier (moins d\'un mètre), tiens le bouton une seconde, et recommence.', aide: true };
  else capture.value = { ton: 'danger', texte: 'La capture a échoué : télécommande non reconnue. Recommence avec la télécommande de ce volet.' };
  await lire();
}
async function renouveler() {
  const r = await api.pfxRenouveler().catch(() => null);
  if (r && r.ok) {
    renouvelable.value = false;
    capture.value = { ton: 'ok', texte: `Identité neuve ${r.serial} attribuée : l'ancienne reste réservée, aucun autre moteur ne la recevra.` };
    reprendre();
  } else if (r && r.rc === -1) capture.value = { ton: 'danger', texte: '63 identités déjà attribuées : plus de slot libre pour une identité neuve.' };
  else capture.value = { ton: 'danger', texte: 'Refusé : refais la capture de la télécommande, puis recommence.' };
  await lire();
}
async function emettre() {
  if (etat.radioOccupee) { toast('Radio occupée, réessaie dans un instant'); return; }
  etat.radioOccupee = true;
  gestes.value = false; attente.value = -1;
  let n = 5;
  emission.value = { ton: 'neutre', texte: `Émission ~5 s : ne touche à rien… ${n} s`, cours: true };
  const compte = setInterval(() => { n = Math.max(0, n - 1); emission.value = { ...emission.value, texte: `Émission ~5 s : ne touche à rien… ${n} s` }; }, 1000);
  try {
    const r = await api.pfxApprendre();
    if (r && r.ok) {
      emission.value = { ton: 'ok', texte: 'Émission finie. Fais les gestes ci-dessous MAINTENANT, puis clique « J\'ai fait les gestes ».' };
      gestes.value = true;   // les gestes ne se montrent qu'a la fin de l'emission
    } else emission.value = { ton: 'danger', texte: r && r.err ? r.err : 'Échec (radio occupée ?)' };
  } catch { emission.value = { ton: 'danger', texte: 'Radio occupée, réessaie dans un instant.' }; }
  finally { clearInterval(compte); etat.radioOccupee = false; await lire(); }
}
/* Gestes faits : le delai de confirmation du moteur (~10 s) passe avant d'autoriser le test. */
function gestesFaits() {
  attente.value = DELAI_MOTEUR;
  clearInterval(minuteur);
  minuteur = setInterval(() => { attente.value = Math.max(0, attente.value - 1); if (attente.value === 0) clearInterval(minuteur); }, 1000);
}
async function tester(cmd) {
  if (attente.value !== 0 || etat.radioOccupee) return;
  etat.radioOccupee = true; envoi.value = cmd;
  try { await api.pfxCmd(cmd); } catch { toast('Radio occupée, réessaie dans un instant'); }
  finally { envoi.value = ''; etat.radioOccupee = false; lire(); }
}
async function enregistrer() {
  const n = nom.value.trim();
  if (!n) { toast('Donne un nom au volet'); return; }
  if (aVirgule(n)) { toast('La virgule n\'est pas permise dans un nom de volet'); return; }
  try {
    await api.pfxEnregistrer(n);
    toast('Volet créé : voir l\'onglet Volets');
    fin.value = { ton: 'ok', texte: `Volet « ${n} » enregistré. Il est dans l'onglet Volets et dans Home Assistant.` };
    nom.value = '';
    await lire();   // l'identite a migre vers le volet
    rafraichir();
  } catch { toast('Échec de l\'enregistrement'); }
}
async function oublier() {
  await api.pfxOublier().catch(() => {});
  toast('Identité désélectionnée · compteur conservé');
  reprendre(); capture.value = null; fin.value = null;
  await lire();
}
</script>

<template>
  <OpCard title="Créer une télécommande virtuelle" :sub="profil ? `${profil.nom} · ${(profil.freq_khz / 1000).toFixed(3).replace('.', ',')} MHz` : ''">
    <template #avant><button type="button" class="lien-retour" @click="aller('remotes/add/volet')">‹ Ajouter un appareil</button></template>
    <div class="flux">
      <p class="hint">À utiliser si le clonage ne pilote pas ton moteur. On crée une nouvelle télécommande virtuelle et on l'apprend au moteur.
        <b>Nécessite ta télécommande d'origine</b> (pour le geste de validation). Appuie dessus : le boîtier l'identifie et attribue une identité,
        <b>nouvelle</b> si le moteur est inconnu, <b>la même</b> s'il est déjà connu.</p>
      <OpSteps :etapes="['Capturer', 'Émettre', 'Gestes', 'Tester', 'Enregistrer']" :actuelle="etape" />

      <!-- 1. Capturer la telecommande d'origine : le boitier attribue l'identite -->
      <section class="etape" :class="{ passee: etape > 0 }">
        <h3 class="titre-section">Identifier le moteur</h3>
        <OpField label="Modèle de moteur" pour="pfx-modele">
          <OpSelect id="pfx-modele" v-model="modele" :options="optionsModeles" />
        </OpField>
        <TxParamsPanel ref="panneau" :profil="profil" calage />
        <div class="rangee">
          <OpButton :variant="actif ? 'normal' : 'primary'" icon="telec" :disabled="etat.radioOccupee" @click="capturer">{{ actif ? 'Recapturer' : 'Capturer' }}</OpButton>
        </div>
        <OpProgress v-if="capture && capture.cours" label="Capture en cours" />
        <OpStatline v-if="capture" :tone="capture.ton">{{ capture.texte }}<template v-if="capture.plus"><br>⚠️ {{ capture.plus }}</template>
          <template v-if="capture.aide"><br>Toujours rien ? Vérifie le profil radio dans
            <button type="button" class="lien" @click="aller('sys/device')">Système › Appareil</button>.</template></OpStatline>
        <template v-if="renouvelable">
          <p class="hint">Tu réenrôles ce moteur : continue avec cette identité. Si elle a été apprise par <b>d'autres moteurs</b> (ils bougent ensemble),
            crée plutôt une identité neuve : elle ne servira qu'au moteur que tu enrôles maintenant.</p>
          <div class="rangee"><OpButton @click="renouveler">🆕 Créer une identité neuve à la place</OpButton></div>
        </template>
        <OpStatline v-if="ligneIdentite" tone="ok"><b>{{ ligneIdentite }}</b></OpStatline>
      </section>

      <template v-if="actif">
        <!-- 2. Emettre la trame d'apprentissage -->
        <section class="etape" :class="{ passee: gestes }">
          <h3 class="titre-section">1. Enrôler l'identité sur le moteur</h3>
          <OpStatline v-if="AVANT[routine]" tone="alerte"><b>{{ AVANT[routine] }}</b></OpStatline>
          <p class="hint">Clique ci-dessous : l'émission dure ~5 s. <b>Ne touche à rien</b> pendant. Les gestes à faire sur ta télécommande s'afficheront
            <b>à la fin</b> de l'émission.</p>
          <div class="rangee"><OpButton :variant="gestes ? 'normal' : 'primary'" icon="telec" :disabled="etat.radioOccupee || !pfx.learn_ok"
                                        :title="pfx.learn_ok ? '' : 'Capte d\'abord la télécommande de ce volet'" @click="emettre">📡 Émettre la trame d'apprentissage (~5 s)</OpButton></div>
          <OpProgress v-if="emission && emission.cours" label="Émission en cours" />
          <OpStatline v-if="emission" :tone="emission.ton">{{ emission.texte }}</OpStatline>
          <template v-if="gestes">
            <p class="hint"><b>✅ Émission finie. Fais MAINTENANT sur ta télécommande d'origine :</b></p>
            <ol class="gestes"><li v-for="g in GESTES[routine]" :key="g">{{ g }}</li></ol>
            <p class="hint">Quand les gestes sont faits, clique ci-dessous. Beaucoup de moteurs font un petit va-et-vient pour confirmer : bon signe.
              Certains ne bougent pas du tout : c'est normal aussi.</p>
            <div v-if="attente < 0" class="rangee"><OpButton variant="primary" @click="gestesFaits">✅ J'ai fait les gestes</OpButton></div>
            <OpStatline v-else-if="attente > 0" tone="neutre">Le moteur confirme, patiente… {{ attente }} s</OpStatline>
            <OpStatline v-else tone="ok">Tu peux tester maintenant avec ▲ ■ ▼.</OpStatline>
          </template>
        </section>

        <!-- 3. Tester, puis enregistrer -->
        <template v-if="attente === 0">
          <section class="etape" :class="{ passee: etape > 3 }">
            <h3 class="titre-section">2. Tester la nouvelle identité</h3>
            <p class="hint">Si le moteur a bien appris, ces boutons doivent le piloter. S'il ne répond pas tout de suite, réessaie : ne recommence pas l'enrôlement.</p>
            <ShutterControl class="pave-test" :libelles="dpad" :envoi="envoi" :disabled="etat.radioOccupee" @ordre="tester" />
          </section>
          <section class="etape">
            <h3 class="titre-section">3. Enregistrer comme volet</h3>
            <p class="hint">Pour le retrouver dans l'onglet <b>Volets</b> et le piloter depuis Home Assistant.</p>
            <div class="rangee ligne">
              <input v-model="nom" class="op-input" placeholder="Nom du volet (ex : Salon)" :maxlength="(etat.profils && etat.profils.name_max) || 23" aria-label="Nom du volet">
              <OpButton variant="primary" @click="enregistrer">💾 Enregistrer</OpButton>
            </div>
          </section>
        </template>
        <div class="rangee fin"><OpButton variant="ghost" small @click="oublier">↩ Désélectionner (garde l'identité et son compteur)</OpButton></div>
      </template>
      <OpStatline v-if="fin" :tone="fin.ton">{{ fin.texte }}
        <OpButton small variant="ghost" @click="aller('control')">Aller aux Volets</OpButton></OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.flux{display:grid;gap:16px}
.etape{display:grid;gap:12px;padding-left:14px;border-left:2px solid var(--accent-line)}
.etape.passee{border-left-color:color-mix(in srgb,var(--ok) 45%,var(--border))}
.gestes{margin:0;padding-left:1.3em;display:grid;gap:6px;font-size:.9rem}
.pave-test{max-width:360px}
.ligne{flex-wrap:nowrap}
.ligne .op-input{flex:1;min-width:0}
.lien{background:none;border:0;padding:0;color:var(--accent);font:inherit;font-weight:600;cursor:pointer;text-decoration:underline}
</style>
