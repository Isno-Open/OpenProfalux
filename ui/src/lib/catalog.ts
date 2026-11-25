// Lecture verifiee de profiles.json (le catalogue des types, boutons et profils, issue #134). Le fichier arrive du
// boitier (ou de la construction en mode demo) : ses valeurs sont des chaines quelconques tant qu'on ne les a pas
// comparees aux enumerations du domaine. Une assertion de type ferait taire le compilateur sans rien verifier ; ici
// chaque valeur est controlee, et ce qui n'est pas reconnu est ecarte avec sa raison.
//
// Un TYPE invalide casse le catalogue : sans ses boutons, aucun appareil de ce type ne se pilote. Un PROFIL invalide
// est seulement ecarte (il ne sera pas propose) : un profil qu'une version plus recente du firmware connait (moteur
// io-homecontrol par exemple) ne doit pas priver l'interface de tous les autres.
import type { Profile, ProfileTx, Profiles, RoleEntry, TypeButton, TypeEntry } from '../api/types';
import { ADD_METHOD, BUTTON_BEHAVIOR, COMMAND, ENGINE, TEST_STATUS } from '../domain';

type Obj = Record<string, unknown>;
const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);
const isStr = (v: unknown): v is string => typeof v === 'string' && v.length > 0;
const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
/* Vrai si v est l'une des valeurs de l'enumeration : le garde de type suit l'enumeration, pas une liste recopiee. */
const oneOf = <T extends Record<string, string>>(e: T, v: unknown): v is T[keyof T] => typeof v === 'string' && Object.values(e).includes(v);

export class CatalogError extends Error {}

function parseButton(typeKey: string, raw: unknown, i: number): TypeButton {
  const where = `type ${typeKey}, bouton ${i + 1}`;
  if (!isObj(raw)) throw new CatalogError(`${where} : objet attendu`);
  if (!isStr(raw.id)) throw new CatalogError(`${where} : id requis`);
  if (!oneOf(COMMAND, raw.cmd)) throw new CatalogError(`${where} (${raw.id}) : cmd « ${String(raw.cmd)} » inconnue`);
  if (!oneOf(BUTTON_BEHAVIOR, raw.behavior)) throw new CatalogError(`${where} (${raw.id}) : comportement « ${String(raw.behavior)} » inconnu`);
  if (!isStr(raw.label)) throw new CatalogError(`${where} (${raw.id}) : label requis`);
  const pos = raw.position;
  if (!isObj(pos) || !isNum(pos.row) || !isNum(pos.col)) throw new CatalogError(`${where} (${raw.id}) : position { row, col } requise`);
  return { id: raw.id, cmd: raw.cmd, behavior: raw.behavior, label: raw.label, ...(isStr(raw.en) ? { en: raw.en } : {}), position: { row: pos.row, col: pos.col } };
}

function parseType(key: string, raw: unknown): TypeEntry {
  if (!isObj(raw)) throw new CatalogError(`type ${key} : objet attendu`);
  if (!isStr(raw.label) || !isNum(raw.code)) throw new CatalogError(`type ${key} : label et code requis`);
  if (!Array.isArray(raw.buttons) || !raw.buttons.length) throw new CatalogError(`type ${key} : au moins un bouton requis`);
  const buttons = raw.buttons.map((b, i) => parseButton(key, b, i));
  const ids = new Set<string>();
  for (const b of buttons) {
    if (ids.has(b.id)) throw new CatalogError(`type ${key} : bouton ${b.id} en double`);
    ids.add(b.id);
  }
  const travel = raw.travel_s;
  return {
    label: raw.label, code: raw.code, buttons,
    ...(isStr(raw.icon) ? { icon: raw.icon } : {}),
    ...(raw.positional === true ? { positional: true } : {}),
    ...(isObj(raw.en) && isStr(raw.en.label) ? { en: { label: raw.en.label } } : {}),
    ...(isObj(travel) && isNum(travel.min) && isNum(travel.max) ? { travel_s: { min: travel.min, max: travel.max } } : {}),
  };
}

/* Le bloc d'emission : seuls les champs connus, chacun de son type. */
function parseTx(raw: Obj): ProfileTx {
  const tx: ProfileTx = {};
  if (isNum(raw.te_us)) tx.te_us = raw.te_us;
  if (typeof raw.repeat_bit === 'boolean') tx.repeat_bit = raw.repeat_bit;
  if (typeof raw.rmt === 'boolean') tx.rmt = raw.rmt;
  if (isNum(raw.frame_bits)) tx.frame_bits = raw.frame_bits;
  return tx;
}

function parseRole(raw: unknown): RoleEntry | undefined {
  if (!isObj(raw)) return undefined;
  const role: RoleEntry = {};
  if (isStr(raw.label)) role.label = raw.label;
  if (isStr(raw.members_label)) role.members_label = raw.members_label;
  if (Array.isArray(raw.members)) role.members = raw.members.filter(isStr);
  if (raw.positional_only === true) role.positional_only = true;
  if (raw.list_first === true) role.list_first = true;
  return role;
}

/* Un profil, ou la raison de son refus. */
function parseProfile(raw: unknown, types: Record<string, TypeEntry>): Profile | string {
  if (!isObj(raw)) return 'objet attendu';
  const id = isStr(raw.id) ? raw.id : '?';
  if (!isStr(raw.id)) return 'id requis';
  if (!isStr(raw.name) || !isStr(raw.brand)) return `${id} : name et brand requis`;
  if (!isStr(raw.type) || !types[raw.type]) return `${id} : type « ${String(raw.type)} » inconnu`;
  if (!oneOf(ADD_METHOD, raw.add)) return `${id} : methode d'ajout « ${String(raw.add)} » inconnue`;
  if (!oneOf(ENGINE, raw.engine)) return `${id} : moteur « ${String(raw.engine)} » inconnu`;
  const radio = raw.radio;
  if (!isObj(radio) || !isStr(radio.band)) return `${id} : radio.band requis`;
  const test = raw.test;
  if (test !== undefined && (!isObj(test) || !oneOf(TEST_STATUS, test.status))) return `${id} : test.status inconnu`;
  const rx = radio.rx;
  return {
    id: raw.id, name: raw.name, type: raw.type, brand: raw.brand, add: raw.add, engine: raw.engine,
    radio: {
      band: radio.band,
      ...(isNum(radio.freq_khz) ? { freq_khz: radio.freq_khz } : {}),
      ...(isStr(radio.modulation) ? { modulation: radio.modulation } : {}),
      ...(isObj(rx) ? { rx: { ...(isNum(rx.datarate_baud) ? { datarate_baud: rx.datarate_baud } : {}), ...(isNum(rx.bandwidth_khz) ? { bandwidth_khz: rx.bandwidth_khz } : {}) } } : {}),
    },
    ...(isStr(raw.description) ? { description: raw.description } : {}),
    ...(isStr(raw.code) ? { code: raw.code } : {}),
    ...(isObj(raw.tx) ? { tx: parseTx(raw.tx) } : {}),
    ...(isObj(test) && oneOf(TEST_STATUS, test.status) ? { test: { status: test.status } } : {}),
  };
}

/* Le catalogue verifie. Rend aussi les profils ecartes, avec leur raison, pour que l'appelant les signale. */
export function parseProfiles(raw: unknown): { profiles: Profiles; rejected: string[] } {
  if (!isObj(raw) || !isObj(raw.types) || !Array.isArray(raw.profiles)) throw new CatalogError('profiles.json : types et profiles requis');
  const types: Record<string, TypeEntry> = {};
  for (const [key, t] of Object.entries(raw.types)) types[key] = parseType(key, t);
  const profiles: Profile[] = [], rejected: string[] = [];
  const ids = new Set<string>();
  for (const p of raw.profiles) {
    const parsed = parseProfile(p, types);
    if (typeof parsed === 'string') { rejected.push(parsed); continue; }
    if (ids.has(parsed.id)) { rejected.push(`${parsed.id} : id en double`); continue; }
    ids.add(parsed.id);
    profiles.push(parsed);
  }
  return {
    profiles: {
      types, profiles,
      ...(isObj(raw.roles) ? { roles: { central: parseRole(raw.roles.central), zone: parseRole(raw.roles.zone) } } : {}),
      ...(isNum(raw.name_max) ? { name_max: raw.name_max } : {}),
      ...(isNum(raw.devices_max) ? { devices_max: raw.devices_max } : {}),
      ...(isObj(raw.traits) ? { traits: raw.traits } : {}),
    },
    rejected,
  };
}
