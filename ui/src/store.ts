// L'etat partage de l'interface : la session, le statut du boitier (relu toutes les 3 s, comme aujourd'hui),
// la table des profils et les messages. Les ecrans lisent ici et agissent par la couche api.
import { reactive } from 'vue';
import api from './api/index';
import { whenUnauthorized } from './api/http';
import type { Device, OtaStatus, Profiles, RadioSummary, ShutterOrder, Status, TypeEntry } from './api/types';
import { COMMAND, DEVICE_TYPE, FAILURE, HTTP_UNAUTHORIZED, type Command } from './domain';
import { updateAvailable } from './versions';

const STATUS_PERIOD_MS = 3000;          // comme app.js
const STATUS_TIMEOUT_MS = 4000;         // au-dela, la requete est abandonnee
const OFFLINE_AFTER_FAILURES = 2;       // bandeau « injoignable » apres deux echecs de suite
const UPDATE_CHECK_PERIOD_MS = 30 * 60 * 1000; // relecture de la verification GitHub du boitier
const TOAST_MS = 2600;

interface AppState {
  ready: boolean;                       // la session est connue (sinon on n'affiche rien)
  status: Status | null;                // /api/status
  statusOkAt: number | null;            // heure du dernier statut recu
  profiles: Profiles | null;            // profiles.json (route protegee : lu apres la connexion)
  isProtected: boolean;                 // /api/session : interface protegee par mot de passe
  loggedIn: boolean;
  offline: boolean;                     // le boitier ne repond plus
  radioBusy: boolean;                   // une emission ou une capture est en cours : la radio est une ressource unique
  sending: { id: string; cmd: Command } | null;   // commande en cours, pour l'animer
  updateAvailable: boolean;             // une version plus recente attend sur GitHub : pastille sur l'onglet OTA
  toast: { text: string } | null;
}

export const state = reactive<AppState>({
  ready: false, status: null, statusOkAt: null, profiles: null, isProtected: false, loggedIn: false, offline: false,
  radioBusy: false, sending: null, updateAvailable: false, toast: null,
});

let toastTimer: ReturnType<typeof setTimeout> | undefined;
let failures = 0, refreshing = false, launched = false;
let loadingProfiles: Promise<void> | null = null;

export function toast(text: string): void {
  state.toast = { text };
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { state.toast = null; }, TOAST_MS);
}

/* Un 401 : la session a expire ou le mot de passe a change. La connexion ne s'affiche que si le boitier est
   vraiment protege (un 401 residuel apres un retrait de protection ne doit pas bloquer l'interface). */
whenUnauthorized(async () => {
  const s = await api.session().catch(() => null);
  if (s && !s.protected) { state.isProtected = false; return; }
  state.isProtected = true;
  state.loggedIn = false;
});

/* profiles.json : lu au lancement, puis relu a chaque relecture du statut tant qu'il manque (coupure au
   demarrage) : sans lui, Ajouter un appareil et les reglages d'emission n'ont rien a montrer. L'ancienne
   interface le redemandait de meme a chaque ecran qui en avait besoin. */
function loadProfiles(): Promise<void> {
  if (state.profiles) return Promise.resolve();
  loadingProfiles ??= api.profiles().then(p => { state.profiles = p; }, () => {}).finally(() => { loadingProfiles = null; });
  return loadingProfiles;
}

export async function refresh(): Promise<void> {
  if (refreshing || !state.loggedIn) return;
  refreshing = true;
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), STATUS_TIMEOUT_MS);
  try {
    const s = await api.status(controller.signal);
    if (state.status && s.uptime < state.status.uptime) toast('Le boîtier a redémarré');
    state.status = s;
    state.statusOkAt = Date.now();
    failures = 0;
    state.offline = false;
    if (!state.profiles) loadProfiles();
  } catch (e) {
    if (e !== HTTP_UNAUTHORIZED && ++failures >= OFFLINE_AFTER_FAILURES) state.offline = true;
  } finally {
    clearTimeout(timer);
    refreshing = false;
  }
}

export async function checkUpdate(): Promise<OtaStatus | null> {
  const st = await api.otaStatus().catch(() => null);
  if (st) state.updateAvailable = updateAvailable(st);
  return st;
}

/* L'application une fois la session ouverte : profils, statut, rafraichissements. Une seule fois. */
async function launch(): Promise<void> {
  state.loggedIn = true;
  await loadProfiles();
  await refresh();
  if (launched) return;
  launched = true;
  setInterval(() => { if (!document.hidden) refresh(); }, STATUS_PERIOD_MS);
  checkUpdate();
  setInterval(() => { if (!document.hidden) checkUpdate(); }, UPDATE_CHECK_PERIOD_MS);
  document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
}

/* Au chargement : l'etat de la session AVANT tout, pour ne pas declencher une volee de 401. */
export async function start(): Promise<void> {
  const s = await api.session().catch(() => null);   // firmware plus ancien : route absente, interface ouverte
  state.isProtected = !!(s && s.protected);
  state.ready = true;
  if (!s || s.authed || !s.protected) await launch();
}

export async function login(pass: string): Promise<boolean> {
  const ok = await api.login(pass);
  if (ok) await launch();
  return ok;
}

export async function logout(): Promise<void> {
  await api.logout().catch(() => {});
  location.reload();
}

/* Le mot de passe vient de changer : le firmware a ferme toutes les sessions, la notre comprise. */
export function sessionClosed(): void {
  if (history.replaceState) history.replaceState(null, '', location.pathname);
  state.isProtected = true;
  state.loggedIn = false;
}
export function protectionRemoved(): void {
  state.isProtected = false;
  launch();
}

/* Une commande a la fois : pendant qu'elle part, les autres boutons sont grises (pas de matraquage). */
export async function sendCommand(order: ShutterOrder): Promise<void> {
  if (state.radioBusy) return;
  state.radioBusy = true;
  state.sending = { id: order.id, cmd: order.cmd };
  try {
    const r = await api.sendShutter(order);
    if (r && r.busy) toast('Émission déjà en cours sur une autre carte, réessaie');
  } catch { toast('Radio occupée, réessaie dans un instant'); }
  finally { state.radioBusy = false; state.sending = null; refresh(); }
}

/* Une operation radio longue (capture, enrolement, calage) : la radio est prise, le pilotage attend. */
export async function withRadio<T>(operation: () => Promise<T>): Promise<T> {
  if (state.radioBusy) { toast('Radio occupée, réessaie dans un instant'); throw FAILURE.busy; }
  state.radioBusy = true;
  try { return await operation(); }
  finally { state.radioBusy = false; refresh(); }
}

/* Les radios de la carte (bandes 868 / 433), lues une fois. Echec ou liste vide : null, et la pyramide montre
   tout (comme aujourd'hui, le filet de securite). */
let radiosPromise: Promise<RadioSummary[] | null> | null = null;
export function radios(): Promise<RadioSummary[] | null> {
  if (!radiosPromise) radiosPromise = api.radios().then(r => (Array.isArray(r) && r.length ? r : null)).catch(() => { radiosPromise = null; return null; });
  return radiosPromise;
}
export async function radioFor(band: string | undefined): Promise<number> {
  const list = (await radios()) || [];
  const match = list.find(x => String(x.band) === String(band));
  return match ? match.idx : 0;
}

export async function saveOrder(ids: string[]): Promise<void> {
  state.status?.volets.forEach(d => { d.order = ids.indexOf(d.id); });
  await api.saveDisplayOrder(ids).catch(() => toast('Ordre non enregistré'));
}

/* Nom a afficher pour un serial : nom de la telecommande, sinon le serial. */
export const remoteName = (serial: string): string => state.status?.remotes[serial] || serial;

/* Le type declare d'un appareil, lu dans profiles.json (source unique) : volet, portail... */
const DEFAULT_TYPE: TypeEntry = { label: 'Volet', code: 0, dpad: { [COMMAND.up]: 'Montée', [COMMAND.stop]: 'Stop', [COMMAND.down]: 'Descente' } };
export function typeOf(device: Pick<Device, 'type'> | null | undefined): TypeEntry & { key: string } {
  const types = state.profiles?.types ?? {};
  const code = device?.type ?? 0;
  const key = Object.keys(types).find(k => types[k]?.code === code) ?? DEVICE_TYPE.shutter;
  return { key, ...(types[key] ?? DEFAULT_TYPE) };
}
