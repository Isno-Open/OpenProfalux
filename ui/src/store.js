// L'etat partage de l'interface : la session, le statut du boitier (relu toutes les 3 s, comme
// aujourd'hui), la table des profils et les messages. Les ecrans lisent ici et agissent par la couche api.
import { reactive } from 'vue';
import api from './api/index.js';
import { quandNonAutorise } from './api/http.js';
import { majDisponible } from './versions.js';

const PERIODE_STATUT = 3000;        // comme app.js
const DELAI_STATUT = 4000;          // au-dela, la requete est abandonnee
const ECHECS_HORS_LIGNE = 2;        // bandeau « injoignable » apres deux echecs de suite
const PERIODE_MAJ = 30 * 60 * 1000; // relecture de la verification GitHub du boitier

export const etat = reactive({
  pret: false,          // la session est connue (sinon on n'affiche rien)
  statut: null,         // /api/status
  statutOkA: null,      // heure du dernier statut recu
  profils: null,        // profiles.json (route protegee : lu apres la connexion)
  protege: false,       // /api/session : interface protegee par mot de passe
  connecte: false,
  horsLigne: false,     // le boitier ne repond plus
  radioOccupee: false,  // une emission ou une capture est en cours : la radio est une ressource unique
  envoi: null,          // { id, cmd } de la commande en cours, pour l'animer
  majDispo: false,      // une version plus recente attend sur GitHub : pastille sur l'onglet OTA
  toast: null,
});

let minuteur = null, minuteurMaj = null, toastMinuteur = null, echecs = 0, enCours = false, lance = false;

export function toast(texte) {
  etat.toast = { texte };
  clearTimeout(toastMinuteur);
  toastMinuteur = setTimeout(() => { etat.toast = null; }, 2600);
}

/* Un 401 : la session a expire ou le mot de passe a change. La connexion ne s'affiche que si le boitier
   est vraiment protege (un 401 residuel apres un retrait de protection ne doit pas bloquer l'interface). */
quandNonAutorise(async () => {
  const s = await api.session().catch(() => null);
  if (s && !s.protected) { etat.protege = false; return; }
  etat.protege = true;
  etat.connecte = false;
});

export async function rafraichir() {
  if (enCours || !etat.connecte) return;
  enCours = true;
  const ctl = new AbortController(), t = setTimeout(() => ctl.abort(), DELAI_STATUT);
  try {
    const s = await api.statut(ctl.signal);
    if (etat.statut && s.uptime < etat.statut.uptime) toast('Le boîtier a redémarré');
    etat.statut = s;
    etat.statutOkA = Date.now();
    echecs = 0;
    etat.horsLigne = false;
  } catch (e) {
    if (e !== 401 && ++echecs >= ECHECS_HORS_LIGNE) etat.horsLigne = true;
  } finally {
    clearTimeout(t);
    enCours = false;
  }
}

export async function verifierMaj() {
  const st = await api.otaEtat().catch(() => null);
  if (st) etat.majDispo = majDisponible(st);
  return st;
}

/* L'application une fois la session ouverte : profils, statut, rafraichissements. Une seule fois. */
async function lancer() {
  etat.connecte = true;
  if (!etat.profils) etat.profils = await api.profils().catch(() => null);
  await rafraichir();
  if (lance) return;
  lance = true;
  minuteur = setInterval(() => { if (!document.hidden) rafraichir(); }, PERIODE_STATUT);
  verifierMaj();
  minuteurMaj = setInterval(() => { if (!document.hidden) verifierMaj(); }, PERIODE_MAJ);
  document.addEventListener('visibilitychange', () => { if (!document.hidden) rafraichir(); });
}

/* Au chargement : l'etat de la session AVANT tout, pour ne pas declencher une volee de 401. */
export async function demarrer() {
  const s = await api.session().catch(() => null);   // firmware plus ancien : route absente, interface ouverte
  etat.protege = !!(s && s.protected);
  etat.pret = true;
  if (!s || s.authed || !s.protected) await lancer();
}

export async function connexion(pass) {
  const ok = await api.connexion(pass);
  if (ok) await lancer();
  return ok;
}

export async function deconnexion() {
  await api.deconnexion().catch(() => {});
  location.reload();
}

/* Le mot de passe vient de changer : le firmware a ferme toutes les sessions, la notre comprise. */
export function sessionFermee() {
  if (history.replaceState) history.replaceState(null, '', location.pathname);
  etat.protege = true;
  etat.connecte = false;
}
export function protectionRetiree() {
  etat.protege = false;
  lancer();
}

/* Une commande a la fois : pendant qu'elle part, les autres boutons sont grises (pas de matraquage). */
export async function envoyer(ordre) {
  if (etat.radioOccupee) return;
  etat.radioOccupee = true;
  etat.envoi = { id: ordre.id, cmd: ordre.cmd };
  try {
    const r = await api.ordre(ordre);
    if (r && r.busy) toast('Émission déjà en cours sur une autre carte, réessaie');
  } catch { toast('Radio occupée, réessaie dans un instant'); }
  finally { etat.radioOccupee = false; etat.envoi = null; rafraichir(); }
}

/* Une operation radio longue (capture, enrolement, calage) : la radio est prise, le pilotage attend. */
export async function avecRadio(fn) {
  if (etat.radioOccupee) { toast('Radio occupée, réessaie dans un instant'); throw 'occupee'; }
  etat.radioOccupee = true;
  try { return await fn(); }
  finally { etat.radioOccupee = false; rafraichir(); }
}

/* Les radios de la carte (bandes 868 / 433), lues une fois. Echec ou liste vide : null, et la pyramide
   montre tout (comme aujourd'hui, le filet de securite). */
let radiosPromesse = null;
export function radios() {
  if (!radiosPromesse) radiosPromesse = api.radios().then(r => (Array.isArray(r) && r.length ? r : null)).catch(() => { radiosPromesse = null; return null; });
  return radiosPromesse;
}
export async function radioPour(bande) {
  const r = (await radios()) || [];
  const m = r.find(x => String(x.band) === String(bande));
  return m ? m.idx : 0;
}

export async function ranger(ids) {
  etat.statut.volets.forEach(v => { v.order = ids.indexOf(v.id); });
  await api.ordreAffichage(ids).catch(() => toast('Ordre non enregistré'));
}

/* Nom a afficher pour un serial : nom de la telecommande, sinon le serial. */
export const nomTelec = s => ((etat.statut && etat.statut.remotes) || {})[s] || s;

/* Le type declare d'un appareil, lu dans profiles.json (source unique) : volet, portail... */
export function typeDe(v) {
  const types = (etat.profils && etat.profils.types) || {};
  const code = (v && v.type) || 0;
  const cle = Object.keys(types).find(k => types[k].code === code) || 'volet';
  return { cle, ...(types[cle] || { label: 'Volet', dpad: { up: 'Montée', stop: 'Stop', down: 'Descente' } }) };
}
