<script setup>
// OTA : la version installee, le retour automatique, la mise a jour depuis GitHub (le boitier telecharge
// lui-meme) ou depuis un fichier, et le retour a la version precedente. Les confirmations sont dans la page,
// pas des fenetres du navigateur. Apres un redemarrage, la page attend le boitier puis se recharge.
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { etat, toast } from '../store.js';
import api from '../api/index.js';
import { sansV, plusRecente, majDisponible } from '../versions.js';
import OpCard from '../components/base/OpCard.vue';
import OpButton from '../components/base/OpButton.vue';
import OpStatline from '../components/base/OpStatline.vue';
import OpSwitch from '../components/base/OpSwitch.vue';
import OpProgress from '../components/base/OpProgress.vue';
import OpFileInput from '../components/base/OpFileInput.vue';

const st = ref(null);
const auto = ref(true);
const verif = ref(null);        // { ton, texte } : resultat de la verification
const verifEnCours = ref(false);
const confirmer = ref('');      // 'github' | 'fichier' | 'retour'
const avance = ref(null);       // { valeur, texte }
const fin = ref(null);          // { ton, texte }
const fichier = ref(null);
let relecture = null;

async function lireEtat() {
  st.value = await api.otaEtat().catch(() => st.value);
  /* Version en essai : relue apres la minute de validation. */
  clearTimeout(relecture);
  if (st.value && st.value.img === 'essai') relecture = setTimeout(lireEtat, 65000);
}
onMounted(async () => {
  await lireEtat();
  const c = await api.config().catch(() => ({}));
  auto.value = c.update_auto !== 0 && c.update_auto !== false;   // absent (ancien firmware) = active
  if (majDisponible(st.value)) verif.value = { ton: 'alerte', texte: `Installée ${sansV(st.value.version)}, disponible ${sansV(st.value.latest)}.` };
});
onUnmounted(() => clearTimeout(relecture));

const dispo = computed(() => majDisponible(st.value));
const retour = computed(() => {
  const s = st.value;
  if (!s || s.rb_auto === undefined) return null;   // firmware d'avant : rien a afficher
  if (s.img === 'essai') return { ton: 'alerte', texte: 'Version en essai : validée après 1 min sans plantage, sinon retour automatique à la précédente.' };
  if (s.rb_auto === 1) return { ton: 'ok', texte: 'Retour automatique : actif (une mise à jour qui plante au démarrage est annulée toute seule).' };
  if (s.rb_auto === 0) return { ton: 'neutre', texte: 'Retour automatique : inactif sur ce boîtier (bootloader d\'avant, voir le README du firmware pour l\'activer).' };
  return { ton: 'neutre', texte: 'Retour automatique : vérifié à la prochaine mise à jour.' };
});
const annulee = computed(() => (st.value && st.value.annulee && st.value.annulee_auto ? st.value.annulee : ''));
const retourPossible = computed(() => !st.value || st.value.rb_possible === undefined || !!st.value.rb_possible);

async function verifier() {
  verifEnCours.value = true;
  verif.value = { ton: 'neutre', texte: 'Le boîtier interroge GitHub…' };
  try {
    const s = await api.otaVerifier(phase => {
      verif.value = { ton: 'neutre', texte: phase === 'attente' ? 'Le boîtier attend la fin de la connexion du cloud…' : 'Le boîtier interroge GitHub…' };
    });
    st.value = s;
    const derniere = sansV(s.latest), installee = sansV(s.version);
    etat.majDispo = majDisponible(s);
    if (!derniere) verif.value = { ton: 'danger', texte: 'Le boîtier n\'a pas pu lire la dernière version (Internet ? limite de l\'API GitHub ?)' };
    else if (plusRecente(derniere, installee)) verif.value = { ton: 'alerte', texte: `Installée ${installee}, disponible ${derniere}.` };
    else verif.value = { ton: 'ok', texte: installee === derniere ? `${installee} = dernière version.` : `Installée ${installee}, dernière release ${derniere}.` };
  } catch (e) {
    verif.value = { ton: 'danger', texte: e === 'github' ? 'Pas de réponse de GitHub (Internet ?)' : 'Le boîtier ne répond pas.' };
  } finally { verifEnCours.value = false; }
}

/* Le boitier redemarre : on attend qu'il soit tombe puis revenu, puis on recharge la page (sa nouvelle
   interface). Une image refusee a la verification rejette : l'appelant affiche l'echec. */
async function attendreEtRecharger(texte) {
  fin.value = { ton: 'ok', texte };
  const s = await api.attendreRetour();
  fin.value = s ? { ton: 'ok', texte: `Redémarré sur la version ${s.version || '?'}, rechargement…` }
    : { ton: 'alerte', texte: 'Le boîtier ne répond pas encore : recharge la page dans un instant.' };
  if (s) setTimeout(() => location.reload(), 900);
}
async function installer(source) {
  confirmer.value = ''; fin.value = null;
  avance.value = { valeur: 0, texte: source === 'github' ? 'téléchargement…' : 'envoi…' };
  const suivi = (v, t) => { avance.value = { valeur: v == null ? (avance.value && avance.value.valeur) : v, texte: t }; };
  try {
    if (source === 'github') await api.otaInstaller(suivi, st.value.latest_url);
    else await api.otaEnvoyer(fichier.value, suivi);
    avance.value = { valeur: 1, texte: source === 'github' ? 'installée' : 'reçu' };
    await attendreEtRecharger(source === 'github' ? 'Installée : redémarrage, reconnexion…' : 'Reçu : vérification et redémarrage (environ 5 s), reconnexion…');
  } catch (e) {
    avance.value = null;
    fin.value = { ton: 'danger', texte: `Échec${typeof e === 'string' ? ` : ${e}` : ''}. Le boîtier garde sa version.` };
  }
}
async function revenir() {
  confirmer.value = '';
  try { await api.otaRevenir(); }
  catch (e) {
    if (e === 409) {   // refus : aucune version precedente valide (le boitier ne redemarre pas)
      st.value = { ...st.value, rb_possible: 0 };
      return;
    }
  }
  try { await attendreEtRecharger(`Retour à la version ${(st.value && st.value.precedente) || 'précédente'} : redémarrage, reconnexion…`); }
  catch (e) { fin.value = { ton: 'danger', texte: `Échec : ${e}.` }; }
}
async function basculerAuto(v) {
  const r = await api.enregistrerConfig({ update_auto: v }).catch(() => null);
  if (r && r.ok) toast(v ? 'Vérification automatique activée' : 'Vérification automatique désactivée');
  else { auto.value = !v; toast('Échec de l\'enregistrement'); }
}
const complet = computed(() => fichier.value && /full/i.test(fichier.value.name));
</script>

<template>
  <OpCard title="Mise à jour (OTA)" :sub="st ? `Version courante : ${st.version}${st.part ? ` · partition ${st.part}` : ''}` : ''">
    <div class="ota">
      <OpStatline v-if="retour" :tone="retour.ton">{{ retour.texte }}</OpStatline>
      <OpStatline v-if="annulee" tone="danger">La mise à jour {{ annulee }} n'a pas démarré correctement : le boîtier est revenu tout seul à la version précédente.</OpStatline>

      <h3 class="titre-section">Depuis GitHub</h3>
      <div class="rangee">
        <OpButton :disabled="verifEnCours" @click="verifier">Vérifier la dernière version</OpButton>
        <OpButton v-if="dispo" variant="primary" icon="ota" :disabled="!!avance" @click="confirmer = 'github'">Installer {{ sansV(st.latest) }}</OpButton>
      </div>
      <OpStatline v-if="verif" :tone="verif.ton" role="status">{{ verif.texte }}</OpStatline>
      <p v-else class="hint">Le boîtier interroge GitHub et récupère la dernière release publiée.</p>
      <OpStatline v-if="confirmer === 'github'" tone="alerte">Le boîtier va télécharger la mise à jour puis <b>redémarrer</b> (environ 30 s).
        <OpButton small variant="primary" @click="installer('github')">Confirmer</OpButton><OpButton small @click="confirmer = ''">Annuler</OpButton></OpStatline>
      <div class="option">
        <OpSwitch v-model="auto" @update:model-value="basculerAuto">Vérification automatique</OpSwitch>
        <p class="hint">Le boîtier interroge GitHub toutes les 6 h (entité de mise à jour Home Assistant). Désactivée, il ne le fait que sur le bouton ci-dessus.</p>
      </div>

      <h3 class="titre-section">Depuis un fichier</h3>
      <OpFileInput accept=".bin" label="Choisir le firmware (.bin)" @choisi="x => { fichier = x; confirmer = ''; }" />
      <OpStatline v-if="complet" tone="alerte">« {{ fichier.name }} » ressemble à un binaire <b>complet</b> (flash par câble), pas à une image OTA :
        l'OTA le refusera. Prends plutôt le fichier « -ota.bin ».</OpStatline>
      <div class="rangee">
        <OpButton variant="primary" :disabled="!fichier || !!avance" @click="confirmer = 'fichier'">Flasher</OpButton>
        <OpButton variant="danger" :disabled="!retourPossible || !!avance" @click="confirmer = 'retour'">↩ Version précédente</OpButton>
      </div>
      <OpStatline v-if="confirmer === 'fichier'" tone="alerte">Envoyer « {{ fichier.name }} » au boîtier, puis redémarrer dessus ?
        <OpButton small variant="primary" @click="installer('fichier')">Confirmer</OpButton><OpButton small @click="confirmer = ''">Annuler</OpButton></OpStatline>
      <OpStatline v-if="confirmer === 'retour'" tone="alerte">{{ st && st.precedente ? `Revenir à la version ${st.precedente} et redémarrer ?` : 'Revenir à la version précédente et redémarrer ?' }}
        <OpButton small variant="danger" @click="revenir">Revenir</OpButton><OpButton small @click="confirmer = ''">Annuler</OpButton></OpStatline>
      <p v-if="!retourPossible" class="hint">Pas de version précédente valide sur ce boîtier : rien vers quoi revenir (première installation, ou mise à jour annulée).</p>

      <div v-if="avance" class="avance"><OpProgress :valeur="avance.valeur" label="Mise à jour" /><span class="hint">{{ avance.texte }}</span></div>
      <OpStatline v-if="fin" :tone="fin.ton">{{ fin.texte }}</OpStatline>
      <p class="hint">Écrit sur la partition inactive, puis redémarre dessus. Si la nouvelle version plante dans sa première minute, le boîtier revient tout seul à
        l'ancienne (retour automatique actif). Ne pas le débrancher pendant cette minute : il reviendrait aussi à l'ancienne version.</p>
    </div>
  </OpCard>
</template>

<style scoped>
.ota{display:grid;gap:12px}
.option{display:grid;gap:4px}
.option .hint{padding-left:48px}
.avance{display:grid;gap:6px}
</style>
