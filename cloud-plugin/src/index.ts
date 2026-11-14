// Plugin de la famille OpenProfalux pour OpenCloudApp (contrat : opencloudapp/docs/FAMILLES.md).
// Il apprend au portail a lire un boitier OpenProfalux et a le commander. Les types d'appareils sont
// ceux du firmware (profiles.json, engendres dans kinds.generated.ts).
//
// Ce que publie le boitier sous `openprofalux/<boitier>/` (relai cloud du firmware, cloud_topic.c) :
//   status                    online, testament offline (retenu)              -> presence
//   cover/<id>/attrs          {"type":"volet","positional":true,"name":"Séjour",
//                              "travel_up_s":19,"travel_down_s":18} (retenu)        -> appareil, reglages
//   cover/<id>/position       0..100 (retenu)                                  -> position
//   remote/<tel>/battery      ON si la pile est faible (retenu)               -> alerte
//   system/status             {"fw":"0.2.16","target":"m5-atom-lite",...}      -> version
//   update/state              {"installed_version","latest_version",...}       -> mise a jour
// Le reste (battement, ecoute radio, etat texte) n'a pas de fait correspondant : ignore.
// Commandes, sous le meme prefixe : cover/<id>/set (OPEN|STOP|CLOSE), cover/<id>/set_position (0..100),
// update/install (INSTALL) pour le boitier entier. Reglages (etalonnage) : cover/<id>/config
// {"travel_up_ms":N,"travel_down_ms":N}, valides par le firmware contre les memes bornes (profiles.json).
import type { Plugin } from "../opencloudapp/server/src/plugin-types.js";
import { PLUGIN_CATEGORY } from "../opencloudapp/server/src/plugin-contract.js";
import { FACT, EVENT_LEVEL, FAMILY_CONTRACT, type Fact, type FamilyDecl, type FamilyMessage } from "../opencloudapp/server/src/family-contract.js";
import { KINDS } from "./kinds.generated.js";
import { CONTROL_ID, SETTING_ID } from "./ids.js";

/** Actions de la carte -> charge que le firmware attend sur cover/<id>/set. */
export const ACTION_PAYLOAD: Readonly<Record<string, string>> = { [CONTROL_ID.up]: "OPEN", [CONTROL_ID.stop]: "STOP", [CONTROL_ID.down]: "CLOSE" };
/** Presence telle que le firmware l'annonce (mqtt_bridge.c, PRESENCE_ONLINE / PRESENCE_OFFLINE). */
export const PRESENCE = { online: "online", offline: "offline" } as const;
/** Commande de mise a jour du boitier entier (entite update de HA, payload_install). */
export const UPDATE_INSTALL = { control: "update", path: "update/install", payload: "INSTALL" } as const;
/** Etat de pile d'une telecommande (binary_sensor HA : ON = pile faible). */
const BATTERY_LOW = "ON";

const json = (s: string): Record<string, unknown> | null => {
  try { const v: unknown = JSON.parse(s); return v && typeof v === "object" && !Array.isArray(v) ? (v as Record<string, unknown>) : null; }
  catch { return null; }
};
/** Nom de l'appareil : celui que le boitier publie (le vrai, accents compris) ; un firmware plus
 *  ancien ne publie que le slug des topics, dont on rend au moins les espaces (« _ »). */
const nameOf = (published: unknown, slug: string): string =>
  typeof published === "string" && published.trim() !== "" ? published : slug.replace(/_/g, " ");

export function read({ path, payload, retained }: FamilyMessage): Fact[] {
  if (path === "status") {
    const p = payload.trim();
    return p === PRESENCE.online || p === PRESENCE.offline ? [{ fact: FACT.presence, online: p === PRESENCE.online }] : [];
  }
  let m = /^cover\/([^/]+)\/(attrs|position)$/.exec(path);
  if (m) {
    const device = m[1]!;
    if (m[2] === "position") {
      const n = Number(payload);
      return payload.trim() !== "" && Number.isFinite(n) ? [{ fact: FACT.state, device, control: CONTROL_ID.position, value: n }] : [];
    }
    const a = json(payload);
    if (!a) return [];
    const up = a[SETTING_ID.travelUp], down = a[SETTING_ID.travelDown];
    // temps de course publies seulement si le volet est etalonne (les deux) : sinon, pas de valeur
    const settings = typeof up === "number" && typeof down === "number" ? { settings: { [SETTING_ID.travelUp]: up, [SETTING_ID.travelDown]: down } } : {};
    return [{ fact: FACT.device, device, kind: typeof a.type === "string" ? a.type : "", name: nameOf(a.name, device),
      hidden: a.positional === true ? [] : [CONTROL_ID.position], ...settings }];
  }
  m = /^remote\/([^/]+)\/battery$/.exec(path);
  // une alerte par passage a « pile faible » vu en direct ; le retenu resservi au branchement n'en est pas un
  if (m) return payload.trim() === BATTERY_LOW && !retained
    ? [{ fact: FACT.event, level: EVENT_LEVEL.alert, code: "pile-faible", text: { fr: `Pile faible : télécommande ${m[1]}`, en: `Low battery: remote ${m[1]}` } }]
    : [];
  if (path === "system/status") {
    const s = json(payload);
    return s && typeof s.fw === "string" ? [{ fact: FACT.box, firmware: s.fw }] : [];
  }
  if (path === "update/state") {
    const u = json(payload);
    if (!u) return [];
    return [{ fact: FACT.box,
      ...(typeof u.installed_version === "string" ? { firmware: u.installed_version } : {}),
      ...(typeof u.latest_version === "string" ? { latest: u.latest_version } : {}) }];
  }
  return [];
}

export const command: FamilyDecl["command"] = ({ device }, control, value) => {
  if (!device) return control === UPDATE_INSTALL.control ? { path: UPDATE_INSTALL.path, payload: UPDATE_INSTALL.payload } : null;
  if (control === CONTROL_ID.position) return typeof value === "number" ? { path: `cover/${device}/set_position`, payload: String(Math.round(value)) } : null;
  const p = ACTION_PAYLOAD[control];
  return p ? { path: `cover/${device}/set`, payload: p } : null;
};

/** Etalonnage depuis le portail : le firmware attend les DEUX temps de course, en millisecondes (le
 *  formulaire envoie ses deux champs). S'il en manque un, rien n'est envoye. */
export const configure: NonNullable<FamilyDecl["configure"]> = ({ device }, values) => {
  const up = values[SETTING_ID.travelUp], down = values[SETTING_ID.travelDown];
  if (typeof up !== "number" || typeof down !== "number") return null;
  return { path: `cover/${device}/config`, payload: JSON.stringify({ travel_up_ms: Math.round(up * 1000), travel_down_ms: Math.round(down * 1000) }) };
};

export const family: FamilyDecl = {
  id: "openprofalux",
  label: { fr: "OpenProfalux", en: "OpenProfalux" },
  icon: { path: "M4 4h16v16H4zM4 8h16M4 12h16M4 16h16" },
  contract: FAMILY_CONTRACT,
  topicRoot: "openprofalux",
  kinds: KINDS,
  read,
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
