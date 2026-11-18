<script setup>
// L'ecran de capture UNIFIE. Avec un profil choisi dans la pyramide (#remotes/learn/<index>) : cloner une
// telecommande Profalux (KeeLoq) ou enregistrer celle d'un portail CAME (rejeu brut) ; le profil decide le
// moteur, la radio et la frequence, le type decide les commandes a capturer. Sans profil (#remotes/learn,
// ou #remotes/learn/-/<appareil> depuis Mes appareils ou les trames RF) : gerer un appareil existant,
// y compris apprendre la telecommande d'origine d'un volet virtuel pour suivre sa position.
import { computed, ref, watch } from 'vue';
import { route, aller } from '../../router.js';
import { etat, avecRadio, radioPour, rafraichir, toast, nomTelec as nomDe } from '../../store.js';
import { profils, types, cleType, mecanisme, iconeAppareil, profilDe, aVirgule } from '../../profils.js';
import api from '../../api/index.js';
import OpCard from '../../components/base/OpCard.vue';
import OpField from '../../components/base/OpField.vue';
import OpButton from '../../components/base/OpButton.vue';
import OpStatline from '../../components/base/OpStatline.vue';
import ChipPicker from '../../components/metier/ChipPicker.vue';
import CaptureSlots from '../../components/metier/CaptureSlots.vue';
import TxParamsPanel from '../../components/metier/TxParamsPanel.vue';

const ACTIONS_VOLET = [{ cmd: 'up', label: 'Montée' }, { cmd: 'stop', label: 'Stop' }, { cmd: 'down', label: 'Descente' }];
const mhz = k => `${(k / 1000).toFixed(3).replace('.', ',')} MHz`;

/* Profil porte par l'adresse ; null = gestion d'un appareil existant. */
const profil = computed(() => {
  const i = parseInt(route.suite[0], 10);
  return Number.isNaN(i) ? null : profils()[i] || null;
});
const volets = computed(() => (etat.statut && etat.statut.volets) || []);
/* Avec un profil : les appareils du meme type et du meme mecanisme (on ne clone pas un volet enrole).
   Sans profil : tous, sauf les centrales (pas de telecommande propre). */
const compatibles = computed(() => volets.value.filter(v => !v.central
  && (!profil.value || (!v.zone && cleType(v) === profil.value.type && mecanisme(v) === profil.value.mecanisme))));
const preselection = route.suite[1] ? decodeURIComponent(route.suite[1]) : '';
const choix = ref(preselection || (profil.value ? '__nouveau' : ''));
const nom = ref('');
const id = computed(() => (choix.value === '__nouveau' ? nom.value.trim() : choix.value));
const appareil = computed(() => volets.value.find(v => v.id === id.value) || null);
const options = computed(() => compatibles.value.map(v => ({ value: v.id, label: v.id, icon: iconeAppareil(v) })));
const typeCle = computed(() => (profil.value ? profil.value.type : appareil.value ? cleType(appareil.value) : 'volet'));
const libelleNouveau = computed(() => `+ Nouveau ${((types()[typeCle.value] || {}).label || 'volet').toLowerCase()}`);

const rejeu = computed(() => (profil.value ? profil.value.flow === 'rejeu' : !!(appareil.value && appareil.value.rejeu)));
const virtuel = computed(() => !!(appareil.value && appareil.value.virt));
const somfy = computed(() => virtuel.value && appareil.value.proto === 'somfy');
/* Le profil qui donne radio et frequence : celui de l'adresse, sinon celui de la fiche de l'appareil. */
const profilEffectif = computed(() => profil.value || (appareil.value ? profilDe(appareil.value) : null));
const actions = computed(() => (somfy.value ? [] : ((types()[typeCle.value] || {}).capture) || ACTIONS_VOLET));

const appris = computed(() => {
  const v = appareil.value, out = {};
  if (!v) return out;
  for (const a of actions.value) {
    if (rejeu.value) { if (v.rejeu_cmds && v.rejeu_cmds[a.cmd]) out[a.cmd] = true; }
    else if (v.cmd && v.cmd[a.cmd]) out[a.cmd] = `0x${Number(v.cmd[a.cmd].b).toString(16).toUpperCase()} · ${v.cmd[a.cmd].s}`;
  }
  return out;
});
const complet = computed(() => actions.value.length > 0 && actions.value.every(a => appris.value[a.cmd]));

/* Nom de la telecommande : prerempli avec son nom actuel quand l'appareil en a deja une (Renommer),
   sinon vide (le nom saisi est donne a la telecommande captee). Relu quand on change d'appareil. */
const nomTelec = ref('');
const serialTelec = computed(() => (appareil.value && (appareil.value.serials || [])[0]) || '');
watch(() => id.value + '|' + serialTelec.value, () => {
  nomTelec.value = serialTelec.value ? ((etat.statut && etat.statut.remotes || {})[serialTelec.value] || id.value) : '';
}, { immediate: true });
async function renommerTelec() {
  await api.telecNommer(serialTelec.value, nomTelec.value.trim());
  toast('Télécommande renommée');
  rafraichir();
}

const enCours = ref('');
const echec = ref('');
const erreurNom = ref('');
const panneau = ref(null);
/* Frequence de CETTE capture : les reglages avances d'abord. Puis, depuis la pyramide, le profil choisi ; pour un
   appareil existant, sa frequence enregistree (porteuse calee, 0 = celle de sa marque), puis sa marque. */
const frequence = computed(() => {
  const r = panneau.value ? panneau.value.lire() : {};
  const fiche = appareil.value && appareil.value.freq_khz;
  const marque = profilEffectif.value && profilEffectif.value.freq_khz;
  return r.freq_khz || (profil.value ? marque || fiche : fiche || marque) || 0;
});
/* L'enrolement n'est propose comme alternative que si la marque en declare un (pas CAME). */
const enrolement = computed(() => {
  const p = profilEffectif.value;
  if (!p || rejeu.value || virtuel.value) return null;
  return profils().find(x => x.marque === p.marque && x.flow === 'enrol-pfx') || null;
});

async function capturer(cmd) {
  erreurNom.value = ''; echec.value = '';
  if (!id.value) { erreurNom.value = profil.value ? 'Donne d\'abord un nom à l\'appareil.' : 'Choisis d\'abord un appareil.'; return; }
  if (choix.value === '__nouveau') {
    if (aVirgule(id.value)) { erreurNom.value = 'La virgule n\'est pas permise dans un nom de volet.'; return; }
    if (appareil.value && !compatibles.value.includes(appareil.value)) { erreurNom.value = 'Ce nom est déjà pris par un autre appareil.'; return; }
  }
  const label = actions.value.find(a => a.cmd === cmd).label;
  const p = profilEffectif.value || {};
  const reglages = profil.value && panneau.value ? panneau.value.lire() : {};
  /* L'appareil vise est fige pour toute la capture : choisir un autre appareil pendant l'attente ne doit pas lui
     faire recevoir les reglages de celle-ci. */
  const cible = id.value, nouveau = choix.value === '__nouveau';
  const serialCible = (appareil.value && (appareil.value.serials || [])[0]) || '';
  enCours.value = cmd;
  try {
    const r = await avecRadio(async () => api.capturer({
      id: cible, action: cmd, rejeu: rejeu.value, type: (types()[typeCle.value] || {}).code || 0, marque: p.marque,
      radio: await radioPour(p.band), freq_khz: frequence.value, rx: p.rx, existe: !!appareil.value,
      nomTelec: serialTelec.value ? '' : nomTelec.value.trim(),
      serialsConnus: (appareil.value && appareil.value.serials) || [], virt: virtuel.value,
    }));
    /* Les reglages avances (ajout depuis la pyramide) passent a l'appareil, comme la capture les a utilises. */
    if (profil.value) {
      const corps = { id: cible };
      for (const [k, v] of Object.entries(reglages)) if (v) corps[k] = v;
      if (p.marque) corps.marque = p.marque;
      if (Object.keys(corps).length > 1) await api.emission(corps).catch(() => {});
    }
    toast(rejeu.value ? `✓ ${label} captée (${r.n} impulsions)` : `✓ ${label} apprise (0x${r.button}, ${r.rssi} dBm)`);
    if (nouveau && choix.value === '__nouveau') choix.value = cible;
    await rafraichir();
  } catch (e) {
    if (e && e.autre) toast(`Pas la bonne télécommande : trame de ${e.autre}, le volet utilise ${serialCible}`);
    else if (e !== 'occupee') echec.value = label;
  } finally { enCours.value = ''; }
}
async function deplacer(de, vers) {
  await api.deplacerCapture({ id: id.value, de, vers });
  toast(`Commande déplacée vers ${actions.value.find(a => a.cmd === vers).label}`);
  rafraichir();
}

const titre = computed(() => (profil.value ? profil.value.nom : 'Cloner une télécommande'));
const sous = computed(() => (!profil.value
  ? 'Tu as ta télécommande d\'origine : choisis un volet (ou crée-le), puis capture chaque commande en appuyant dessus.'
  : rejeu.value
    ? 'Donne un nom, puis capture en appuyant sur ta télécommande. Pour un portail, un seul bouton suffit (le moteur enchaîne la séquence).'
    : 'Donne un nom, puis capture chaque commande en appuyant sur ta télécommande. Une capture par commande suffit.'));
const consigne = computed(() => {
  if (!id.value) return 'Choisis un volet ci-dessus (ou crée-en un) pour activer la capture.';
  if (somfy.value) return `Volet Somfy « ${id.value} » : rien à capturer, il se pilote depuis l'onglet Volets.`;
  if (virtuel.value) return `Volet enrôlé « ${id.value} » : pour suivre la position, clique Capturer puis appuie sur sa télécommande d'origine en tenant le bouton une seconde (moins d'un mètre du boîtier). Facultatif.`;
  return `Clique Capturer, puis appuie sur le bouton de ta télécommande en le tenant une seconde (moins d'un mètre du boîtier).`;
});
const ligneSerials = computed(() => {
  const v = appareil.value;
  if (!v) return null;
  const s = v.serials || [];
  if (v.virt) return { ton: 'neutre', texte: `Identité virtuelle : ${nomDe(v.virt_serial)}${s.length ? ` · Télécommandes : ${s.map(nomDe).join(', ')}` : ''}` };
  if (s.length > 1) return { ton: 'alerte', texte: `Commandes de plusieurs télécommandes : ${s.join(', ')}` };
  return s.length ? { ton: 'neutre', texte: `Télécommande : ${s[0]}` } : null;
});
const retour = computed(() => (profil.value ? { lien: `remotes/add/${profil.value.type}`, texte: '‹ Ajouter un appareil' } : { lien: 'remotes/devices', texte: '‹ Mes appareils' }));
</script>

<template>
  <OpCard :title="titre" :sub="sous">
    <template #avant><button type="button" class="lien-retour" @click="aller(retour.lien)">{{ retour.texte }}</button></template>
    <div class="flux">
      <OpStatline v-if="!rejeu && frequence" tone="neutre">Capture sur la fréquence de <b>{{ profil ? (profil.marque || profil.nom) : (id || 'cet appareil') }}</b> :
        <b>{{ mhz(frequence) }}</b>.<template v-if="profil"> Ajuste-la en bas (Réglages avancés, ou « Caler sur ma télécommande »).</template></OpStatline>
      <OpField :label="profil ? `${(types()[typeCle] || {}).label || 'Appareil'} à compléter` : 'Volet'" :hint="profil ? 'Un appareil déjà commencé, ou un nouveau.' : ''">
        <ChipPicker v-model="choix" :options="options" :ajout="profil ? libelleNouveau : ''" :disabled="!!enCours" label="Appareil" vide="Aucun appareil pour l'instant." />
      </OpField>
      <OpField v-if="choix === '__nouveau'" label="Nom de l'appareil" pour="cap-nom" hint="Il sert d'identifiant (centrales, zones, Home Assistant). Pas de virgule.">
        <input id="cap-nom" v-model="nom" class="op-input" :disabled="!!enCours" :placeholder="rejeu ? 'ex : Portail entrée' : 'ex : Salon'"
               :maxlength="(etat.profils && etat.profils.name_max) || 23" autocomplete="off">
      </OpField>
      <OpField v-if="!rejeu && !somfy" label="Nom de la télécommande" pour="cap-telec" :hint="serialTelec ? '' : 'Facultatif : pour la reconnaître dans Captées.'">
        <div class="rangee ligne">
          <input id="cap-telec" v-model="nomTelec" class="op-input" :placeholder="serialTelec ? 'Nom de cette télécommande' : 'ex : Murale chambre parents'" autocomplete="off">
          <OpButton v-if="serialTelec" @click="renommerTelec">Renommer</OpButton>
        </div>
      </OpField>
      <OpStatline v-if="erreurNom" tone="danger">{{ erreurNom }}</OpStatline>
      <OpStatline v-if="ligneSerials" :tone="ligneSerials.ton">{{ ligneSerials.texte }}</OpStatline>
      <OpStatline v-if="somfy" tone="ok"><b>Volet Somfy RTS (télécommande virtuelle).</b> Il est piloté par son adresse et son code roulant : pas de télécommande à capturer.</OpStatline>
      <OpStatline v-else-if="virtuel" tone="ok"><b>Volet enrôlé (télécommande virtuelle).</b> Il est piloté par génération, pas besoin de cloner. En option,
        apprends ci-dessous sa <b>télécommande d'origine</b> : le boîtier suivra alors la position quand tu l'utilises.</OpStatline>

      <TxParamsPanel v-if="profil" ref="panneau" :profil="profil" calage />

      <template v-if="actions.length">
        <h3 class="titre-section">Capturer les commandes</h3>
        <p class="hint">{{ consigne }}</p>
        <CaptureSlots :actions="actions" :appris="appris" :en-cours="enCours" :rejeu="rejeu" :disabled="etat.radioOccupee || !id"
                      @capturer="capturer" @deplacer="deplacer" />
      </template>
      <p v-else class="hint">{{ consigne }}</p>
      <OpStatline v-if="echec" tone="danger"><b>Rien capté pour « {{ echec }} ».</b> Rapproche la télécommande à moins d'un mètre, tiens le bouton une seconde,
        et réessaie. Toujours rien ? La fréquence n'est peut-être pas la bonne : essaie <b>Caler sur ma télécommande</b>, ou vérifie le profil radio dans
        <button type="button" class="lien" @click="aller('sys/device')">Système › Appareil</button>.</OpStatline>
      <OpStatline v-if="complet && !virtuel" tone="ok"><b>Tout est capturé.</b> Teste depuis l'onglet Volets : le pavé
        {{ rejeu ? 'rejoue tes trames' : 'pilote le volet' }}.
        <OpButton small variant="ghost" @click="aller('control')">Aller aux Volets</OpButton></OpStatline>

      <OpStatline v-if="enrolement" tone="neutre"><b>Le clonage ne pilote pas ton moteur ?</b> Certains moteurs refusent le rejeu (code roulant strict).
        Dans ce cas, <button type="button" class="lien" @click="aller(`remotes/enrol/${profils().indexOf(enrolement)}`)">crée une télécommande virtuelle</button> :
        ça nécessite aussi ta télécommande d'origine.</OpStatline>
    </div>
  </OpCard>
</template>

<style scoped>
.flux{display:grid;gap:14px}
.ligne{flex-wrap:nowrap}
.ligne .op-input{flex:1;min-width:0}
.lien{background:none;border:0;padding:0;color:var(--accent);font:inherit;font-weight:600;cursor:pointer;text-decoration:underline}
</style>
