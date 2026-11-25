// Plugin de la famille OpenProfalux pour OpenCloudApp (contrat : opencloudapp/docs/FAMILLES.md).
// Il apprend au portail a lire un boitier OpenProfalux et a le commander. Les types d'appareils sont
// ceux du firmware (profiles.json, engendres dans kinds.generated.ts).
//
// Ce que publie le boitier sous `openprofalux/<boitier>/` (relai cloud du firmware, cloud_topic.c) :
//   status                    online, testament offline, disabled (cloud coupe sur le boitier), forgotten (oublie sur le boitier) (retenu) -> presence
//   cover/<id>/attrs          {"type":"volet","positional":true,"name":"Séjour",
//                              "travel_up_s":19,"travel_down_s":18} (retenu)        -> appareil, reglages
//   cover/<id>/position       0..100, % d'OUVERTURE pour HA (retenu)            -> position, en % de FERMETURE
//   remote/<tel>/battery      ON si la pile est faible (retenu)               -> alerte
//   system/status             {"fw":"0.2.16","target":"m5-atom-lite","ssid":"...","ip":"...",
//                              "mqtt":"hote:1883","reset":"software",...}      -> version, carte declaree, reseau, redemarrage
//   system/ota                {"trial":false,"previous":"0.2.16","rollback":true} (retenu) -> essai, retour arriere
//   <nom>/state               battement ~60 s {"rssi":-67,"free_heap":...,"min_heap":...,"up_s":...} -> sante
//   update/state              {"installed_version","latest_version",...}       -> mise a jour
//   listen/state              ON|OFF, ecoute radio permanente (retenu)          -> action possible (couper ou activer)
//   backup/data               sauvegarde (JSON de /api/backup, NON retenue), en reponse a backup/get -> fichier
// Le reste (etat texte, trames) n'a pas de fait correspondant : ignore.
// Commandes, sous le meme prefixe : cover/<id>/set (OPEN|STOP|CLOSE), cover/<id>/set_position (0..100, % d'ouverture),
// update/install (INSTALL), ota/rollback (ROLLBACK) et listen/set (ON|OFF) pour le boitier entier. Reglages : cover/<id>/config {"travel_up_ms":N,
// "travel_down_ms":N,"rank":N,"orientation":N,"members":["slug",...]}, valides par le firmware contre les
// memes bornes (profiles.json). Une centrale ou une zone publie aussi "role" et "members" dans attrs.
import type { Plugin } from "../opencloudapp/server/src/plugin-types.js";
import { PLUGIN_CATEGORY } from "../opencloudapp/server/src/plugin-contract.js";
import { FACT, EVENT_LEVEL, FAMILY_CONTRACT, BOX_FILE_TYPE, type Fact, type FamilyDecl, type FamilyMessage } from "../opencloudapp/server/src/family-contract.js";
import { isNewerVersion } from "../opencloudapp/server/src/version.js";
import { KINDS } from "./kinds.generated.js";
import { CONTROL_ID, SETTING_ID, roleKindId } from "./ids.js";
import { REMOTE_KINDS, REMOTE_NAME, readRemote, remoteCommand, remoteConfigure } from "./remotes.js";

/** Actions de la carte -> charge que le firmware attend sur cover/<id>/set. */
export const ACTION_PAYLOAD: Readonly<Record<string, string>> = { [CONTROL_ID.up]: "OPEN", [CONTROL_ID.stop]: "STOP", [CONTROL_ID.down]: "CLOSE", [CONTROL_ID.step]: "STEP" };
/** Presence telle que le firmware l'annonce (mqtt_bridge.c, PRESENCE_ONLINE / PRESENCE_OFFLINE /
 *  PRESENCE_DISABLED : cloud coupe sur le boitier, annonce avant de partir). */
export const PRESENCE = { online: "online", offline: "offline", disabled: "disabled", forgotten: "forgotten" } as const;
/** Actions sur le boitier entier : installer la derniere version (entite update de HA, payload_install),
 *  revenir a la version precedente (firmware, ota/rollback). */
export const UPDATE_INSTALL = { control: "update", path: "update/install", payload: "INSTALL" } as const;
export const ROLLBACK = { control: "rollback", path: "ota/rollback", payload: "ROLLBACK" } as const;
/** Ecoute radio permanente (interrupteur « Ecoute RF permanente » du firmware, shutters.c) : le boitier
 *  publie son etat sur listen/state et obeit a listen/set, avec les memes mots. Deux actions du boitier,
 *  dont une seule est possible a la fois : celle qui change l'etat publie. */
export const LISTEN_WORD = { on: "ON", off: "OFF" } as const;
export const LISTEN_STATE_PATH = "listen/state";
export const LISTEN_ON = { control: "listen-on", path: "listen/set", payload: LISTEN_WORD.on } as const;
export const LISTEN_OFF = { control: "listen-off", path: "listen/set", payload: LISTEN_WORD.off } as const;
const BOX_ACTIONS = [UPDATE_INSTALL, ROLLBACK, LISTEN_ON, LISTEN_OFF] as const;
/** Sauvegarde du boitier, demandee par le portail (firmware mqtt_bridge.c, BACKUP_GET_PATH et
 *  BACKUP_DATA_PATH) : le boitier repond sur le cloud seulement, sans retenue. */
export const BACKUP = { file: "backup", requestPath: "backup/get", requestPayload: "GET", dataPath: "backup/data" } as const;
// Boitier retire de son compte sur le portail : ordre de s'oublier, retenu (firmware : CLOUD_FORGET_PATH).
export const UNPAIR = { path: "cloud/forget", payload: "FORGET" } as const;
// Nom donne au boitier sur le portail, retenu : affiche dans son onglet Cloud (firmware : CLOUD_NAME_PATH).
export const BOX_NAME_PATH = "cloud/name";
/** Etat de pile d'une telecommande (binary_sensor HA : ON = pile faible). */
const BATTERY_LOW = "ON";

/** Cause du dernier redemarrage, telle que le firmware la nomme (mqtt_bridge.c reset_word). */
const RESET_TEXT: Readonly<Record<string, { fr: string; en: string }>> = {
  poweron: { fr: "Mise sous tension", en: "Power on" },
  external: { fr: "Bouton de remise à zéro", en: "Reset button" },
  software: { fr: "Redémarrage logiciel (mise à jour, réglage)", en: "Software restart (update, setting)" },
  panic: { fr: "Plantage du firmware", en: "Firmware crash" },
  watchdog: { fr: "Chien de garde (blocage)", en: "Watchdog (hang)" },
  deepsleep: { fr: "Réveil de veille", en: "Wake from sleep" },
  brownout: { fr: "Baisse de tension", en: "Brownout" },
  unknown: { fr: "Inconnue", en: "Unknown" },
};
const num = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const str = (v: unknown): v is string => typeof v === "string" && v !== "";

const json = (s: string): Record<string, unknown> | null => {
  try { const v: unknown = JSON.parse(s); return v && typeof v === "object" && !Array.isArray(v) ? (v as Record<string, unknown>) : null; }
  catch { return null; }
};
/** Nom de l'appareil : celui que le boitier publie (le vrai, accents compris) ; un firmware plus
 *  ancien ne publie que le slug des topics, dont on rend au moins les espaces (« _ »). */
const nameOf = (published: unknown, slug: string): string =>
  typeof published === "string" && published.trim() !== "" ? published : slug.replace(/_/g, " ");

/** Le portail montre la position comme l'interface du boitier : en % de FERMETURE (0 = ouvert en haut,
 *  100 = ferme en bas), la convention interne du firmware (shutters.c). Le boitier, lui, PUBLIE et RECOIT
 *  le % d'ouverture que veut Home Assistant (100 = ouvert). La traduction est la meme dans les deux sens. */
const PERCENT_FULL = 100;
export const closureFromOpening = (pct: number): number => PERCENT_FULL - pct;
export const openingFromClosure = closureFromOpening;

export function read({ path, payload, retained }: FamilyMessage): Fact[] {
  if (path === BACKUP.dataPath) return [{ fact: FACT.file, file: BACKUP.file, content: payload }];
  if (path === "status") {
    const p = payload.trim();
    if (p === PRESENCE.disabled) return [{ fact: FACT.presence, online: false, disabled: true }];
    if (p === PRESENCE.forgotten) return [{ fact: FACT.presence, online: false, forgotten: true }];
    return p === PRESENCE.online || p === PRESENCE.offline ? [{ fact: FACT.presence, online: p === PRESENCE.online }] : [];
  }
  const remote = readRemote(path, payload);
  if (remote) return remote;
  let m = /^cover\/([^/]+)\/(attrs|position)$/.exec(path);
  if (m) {
    const device = m[1]!;
    if (m[2] === "position") {
      const n = Number(payload);
      return payload.trim() !== "" && Number.isFinite(n) ? [{ fact: FACT.state, device, control: CONTROL_ID.position, value: closureFromOpening(n) }] : [];
    }
    // description retiree (volet supprime sur le boitier) : sa carte s'en va
    if (payload.trim() === "") return [{ fact: FACT.gone, device }];
    const a = json(payload);
    if (!a) return [];
    const up = a[SETTING_ID.travelUp], down = a[SETTING_ID.travelDown], rank = a[SETTING_ID.rank], ori = a[SETTING_ID.orientation];
    // valeurs publiees seulement si definies (temps de course : volet etalonne, les deux)
    // centrale ou zone : son type a elle (« volet-central »), et ses membres (slugs des appareils)
    const role = str(a.role) ? a.role : null, members = a[SETTING_ID.members];
    const settings: Record<string, number | readonly string[]> = {
      ...(num(up) && num(down) ? { [SETTING_ID.travelUp]: up, [SETTING_ID.travelDown]: down } : {}),
      ...(num(rank) ? { [SETTING_ID.rank]: rank } : {}),
      ...(num(ori) ? { [SETTING_ID.orientation]: ori } : {}),
      ...(role && Array.isArray(members) && members.every((x) => typeof x === "string") ? { [SETTING_ID.members]: members as string[] } : {}),
    };
    const type = typeof a.type === "string" ? a.type : "";
    return [{ fact: FACT.device, device, kind: role ? roleKindId(type, role) : type, name: nameOf(a.name, device),
      hidden: a.positional === true ? [] : [CONTROL_ID.position], settings, ...(num(rank) ? { rank } : {}) }];
  }
  m = /^remote\/([^/]+)\/battery$/.exec(path);
  // une alerte par passage a « pile faible » vu en direct ; le retenu resservi au branchement n'en est pas un
  if (m) return payload.trim() === BATTERY_LOW && !retained
    ? [{ fact: FACT.event, level: EVENT_LEVEL.alert, code: "pile-faible", text: { fr: `Pile faible : télécommande ${m[1]}`, en: `Low battery: remote ${m[1]}` } }]
    : [];
  if (path === "system/status") {
    const s = json(payload);
    if (!s || typeof s.fw !== "string") return [];
    const reset = str(s.reset) ? RESET_TEXT[s.reset] ?? RESET_TEXT.unknown : undefined;
    // target : la carte pour laquelle le firmware a ete construit (boards/<id>.json), declaree par lui
    return [{ fact: FACT.box, firmware: s.fw, ...(str(s.target) ? { board: s.target } : {}),
      ...(reset ? { health: { lastReset: reset } } : {}),
      network: { ...(str(s.ssid) ? { ssid: s.ssid } : {}), ...(str(s.ip) ? { ip: s.ip } : {}), ...(str(s.mqtt) ? { broker: s.mqtt } : {}) } }];
  }
  if (path === LISTEN_STATE_PATH) {
    const w = payload.trim();
    if (w !== LISTEN_WORD.on && w !== LISTEN_WORD.off) return [];
    return [{ fact: FACT.box, actions: { [LISTEN_ON.control]: w === LISTEN_WORD.off, [LISTEN_OFF.control]: w === LISTEN_WORD.on } }];
  }
  m = /^([^/]+)\/state$/.exec(path);
  if (m && m[1] !== "update") {   // listen/state est lu plus haut
    // battement : seulement s'il en a la forme (signal), sinon ce n'est pas lui
    const h = json(payload);
    if (!h || !num(h.rssi)) return [];
    return [{ fact: FACT.box, health: {
      ...(h.rssi < 0 ? { rssiDbm: h.rssi } : {}),
      ...(num(h.up_s) ? { uptimeS: h.up_s } : {}),
      ...(num(h.free_heap) ? { freeHeap: h.free_heap } : {}),
      ...(num(h.min_heap) ? { minFreeHeap: h.min_heap } : {}) } }];
  }
  if (path === "update/state") {
    const u = json(payload);
    if (!u) return [];
    const inst = u.installed_version, latest = u.latest_version;
    return [{ fact: FACT.box,
      ...(typeof inst === "string" ? { firmware: inst } : {}),
      ...(typeof latest === "string" ? { latest } : {}),
      // la meme regle que le portail : seulement vers plus recent
      actions: { [UPDATE_INSTALL.control]: typeof inst === "string" && typeof latest === "string" && isNewerVersion(latest, inst) } }];
  }
  if (path === "system/ota") {
    const o = json(payload);
    if (!o) return [];
    return [{ fact: FACT.box,
      ...(typeof o.trial === "boolean" ? { trial: o.trial } : {}),
      ...(typeof o.previous === "string" ? { previous: o.previous } : {}),
      actions: { [ROLLBACK.control]: o.rollback === true } }];
  }
  return [];
}

export const command: FamilyDecl["command"] = ({ device }, control, value) => {
  if (!device) {
    const a = BOX_ACTIONS.find((x) => x.control === control);
    return a ? { path: a.path, payload: a.payload } : null;
  }
  const r = remoteCommand(device, control);
  if (r) return r;
  if (control === CONTROL_ID.position) return typeof value === "number" ? { path: `cover/${device}/set_position`, payload: String(Math.round(openingFromClosure(value))) } : null;
  const p = ACTION_PAYLOAD[control];
  return p ? { path: `cover/${device}/set`, payload: p } : null;
};

/** Reglages depuis le portail -> cover/<id>/config. Le firmware attend les temps de course PAR DEUX,
 *  en millisecondes : un seul des deux n'est pas envoye (le volet garderait un etalonnage boiteux).
 *  Rang et orientation passent tels quels. Rien a envoyer : null. */
export const configure: NonNullable<FamilyDecl["configure"]> = ({ device }, values) => {
  const remoteName = values[REMOTE_NAME];
  if (typeof remoteName === "string") return remoteConfigure(device, remoteName.trim());
  const up = values[SETTING_ID.travelUp], down = values[SETTING_ID.travelDown];
  const rank = values[SETTING_ID.rank], ori = values[SETTING_ID.orientation], members = values[SETTING_ID.members];
  const out: Record<string, number | readonly string[]> = {
    ...(num(up) && num(down) ? { travel_up_ms: Math.round(up * 1000), travel_down_ms: Math.round(down * 1000) } : {}),
    ...(num(rank) ? { rank: Math.round(rank) } : {}),
    ...(num(ori) ? { orientation: Math.round(ori) } : {}),
    // membres d'une centrale ou d'une zone : les slugs tels quels (le portail a verifie qu'ils existent)
    ...(Array.isArray(members) ? { [SETTING_ID.members]: members } : {}),
  };
  return Object.keys(out).length ? { path: `cover/${device}/config`, payload: JSON.stringify(out) } : null;
};

/** Deplacer un appareil (glisser sa carte) : le meme message que le reglage du rang, le firmware decale
 *  les autres (shutters_set_rank). */
export const reorder: NonNullable<FamilyDecl["reorder"]> = ({ device }, rank) =>
  ({ path: `cover/${device}/config`, payload: JSON.stringify({ [SETTING_ID.rank]: Math.round(rank) }) });

export const family: FamilyDecl = {
  id: "openprofalux",
  label: { fr: "OpenProfalux", en: "OpenProfalux" },
  icon: { path: "M4 4h16v16H4zM4 8h16M4 12h16M4 16h16" },
  contract: FAMILY_CONTRACT,
  topicRoot: "openprofalux",
  kinds: [...KINDS, ...REMOTE_KINDS],
  boxControls: [
    { id: UPDATE_INSTALL.control, label: { fr: "Installer la dernière version", en: "Install the latest version" },
      confirm: { fr: "Le boîtier va télécharger la nouvelle version et redémarrer (environ une minute). En cas de problème, il revient seul à la version actuelle.",
        en: "The box will download the new version and restart (about a minute). If anything goes wrong, it returns to the current version on its own." } },
    { id: ROLLBACK.control, label: { fr: "Revenir à la version précédente", en: "Go back to the previous version" },
      confirm: { fr: "Le boîtier va redémarrer sur la version précédente.", en: "The box will restart on the previous version." } },
    { id: LISTEN_ON.control, label: { fr: "Activer l'écoute permanente", en: "Turn on permanent listening" } },
    { id: LISTEN_OFF.control, label: { fr: "Couper l'écoute permanente", en: "Turn off permanent listening" },
      confirm: { fr: "Sans écoute, le boîtier ne suit plus les volets commandés par leur télécommande : leur position affichée peut devenir fausse.",
        en: "Without listening, the box no longer follows shutters driven by their remote: their displayed position may become wrong." } },
  ],
  boxFiles: [{ id: BACKUP.file, label: { fr: "Télécharger la sauvegarde", en: "Download the backup" }, type: BOX_FILE_TYPE.json }],
  requestFile: (_target, file) => (file === BACKUP.file ? { path: BACKUP.requestPath, payload: BACKUP.requestPayload } : null),
  unpair: () => ({ path: UNPAIR.path, payload: UNPAIR.payload }),
  boxName: (_target, name) => ({ path: BOX_NAME_PATH, payload: name }),
  read,
  reorder,
  command,
  configure,
};

const plugin: Plugin = {
  id: "openprofalux",
  title: { fr: "Boîtiers OpenProfalux", en: "OpenProfalux boxes" },
  description: {
    fr: "Volets et portails pilotés par un boîtier OpenProfalux : commandes, position, pile des télécommandes, mises à jour.",
    en: "Shutters and gates driven by an OpenProfalux box: commands, position, remote battery, updates.",
  },
  icon: { path: "M4 4h16v16H4zM4 8h16M4 12h16M4 16h16" },
  category: PLUGIN_CATEGORY.devices,
  register(api) { api.registerDeviceFamily(family); },
};
export default plugin;
