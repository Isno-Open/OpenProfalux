// Lecture de profiles.json, la source unique des types, marques et methodes. Les ecrans ne codent en dur aucun
// type ni aucune marque : ils demandent ici. Meme logique que l'interface d'avant (profileFields,
// profileForDevice, deviceKind), rassemblee en un seul module.
import type { Device, Profile, TypeButton, TypeEntry } from './api/types';
import {
  ADD_METHOD, BAND, BRAND, DEVICE_TYPE, ENGINE, FIELD_KIND, FLOW, PROTOCOL, SECTION, TAB, TX_FIELD,
  type AddMethod, type Command, type Engine, type FieldKind, type Flow, type TxFieldName,
} from './domain';
import { ICON, type IconName } from './icons';
import { state } from './store';

export const profiles = (): Profile[] => state.profiles?.profiles ?? [];
export const profileById = (id: string | undefined): Profile | null => (id ? profiles().find(p => p.id === id) ?? null : null);
export const types = (): Record<string, TypeEntry> => state.profiles?.types ?? {};

/* Icone du sprite pour un type declare ; un type inconnu prend celle du volet. */
const TYPE_ICON: Record<string, IconName> = { [DEVICE_TYPE.shutter]: ICON.shutter, [DEVICE_TYPE.gate]: ICON.gate };
export const typeIcon = (key: string): IconName => TYPE_ICON[key] ?? ICON.shutter;

export function typeKey(device: Pick<Device, 'type'> | null | undefined): string {
  const t = types();
  const code = typeof device?.type === 'number' ? device.type : 0;
  return Object.keys(t).find(k => t[k]?.code === code) ?? DEVICE_TYPE.shutter;
}

export function deviceIcon(device: Device): IconName {
  if (device.central) return ICON.central;
  if (device.zone) return ICON.zone;
  return typeIcon(typeKey(device));
}

export const isSomfy = (device: Pick<Device, 'proto'> | null | undefined): boolean => device?.proto === PROTOCOL.somfy;

/* Les deux axes d'un appareil, deduits de sa fiche : enrole (virtuel, Somfy ou KeeLoq), rejeu brut, ou clone
   (KeeLoq capture). La fiche ne porte pas encore l'id de son profil (etape 2 de #134). */
export interface Method { add: AddMethod; engine: Engine }
export function methodOf(device: Device): Method {
  if (device.virt) return { add: ADD_METHOD.enrol, engine: isSomfy(device) ? ENGINE.somfy : ENGINE.keeloq };
  return { add: ADD_METHOD.copy, engine: device.rejeu ? ENGINE.replay : ENGINE.keeloq };
}
export const sameMethod = (p: Profile, m: Method): boolean => p.add === m.add && p.engine === m.engine;

/* Ce qu'est l'appareil, en mots, et s'il porte des reglages d'emission. */
export function kindOf(device: Device): { label: string; hasTx: boolean } {
  if (device.central) return { label: 'centrale', hasTx: false };
  if (device.zone) return { label: 'zone', hasTx: false };
  if (device.rejeu) return { label: 'rejeu (code fixe)', hasTx: true };
  if (device.virt) return { label: isSomfy(device) ? 'Somfy RTS' : 'télécommande virtuelle', hasTx: true };
  return { label: 'télécommande clonée', hasTx: true };
}

/* Marque d'un appareil : celle gravee dans sa fiche, sinon deduite du protocole. Ne depend pas de profiles.json. */
export const brandOf = (device: Device): string => device.marque || (isSomfy(device) ? BRAND.somfy : device.rejeu ? BRAND.came : BRAND.profalux);
/* Profil d'un appareil : celui que sa fiche porte (grave par le firmware, #134), sinon celui de sa marque et de sa
   methode (ajout + moteur), pour un firmware anterieur qui ne le grave pas encore. */
export function profileOf(device: Device): Profile | null {
  const own = profileById(device.profile);
  if (own) return own;
  const brand = brandOf(device);
  const method = methodOf(device);
  return profiles().find(p => p.brand === brand && sameMethod(p, method)) ?? profiles().find(p => p.brand === brand) ?? null;
}

/* Les boutons d'un type, dans l'ordre de leur position (ligne, puis colonne). */
export function buttonsOf(entry: TypeEntry | null | undefined): TypeButton[] {
  return [...(entry?.buttons ?? [])].sort((a, b) => a.position.row - b.position.row || a.position.col - b.position.col);
}
/* Libelle de chaque commande d'un type, tire de ses boutons (ce qui etait le « dpad »). */
export function commandLabels(entry: TypeEntry | null | undefined): Partial<Record<Command, string>> {
  return Object.fromEntries(buttonsOf(entry).map(b => [b.cmd, b.label]));
}

/* Un champ d'emission reglable d'un profil. */
export interface TxField {
  name: TxFieldName;
  label: string;
  kind: FieldKind;
  defaultValue: number | boolean;
  fixed?: boolean;
  help?: string;
  options?: { value: number; label: string }[];
}

/* Les champs reglables d'un profil, derives des cles de son bloc tx (jamais une liste en dur). */
export function txFields(profile: Profile | null | undefined): TxField[] {
  if (!profile) return [];
  const tx = profile.tx ?? {}, fields: TxField[] = [];
  const freq = profile.radio.freq_khz;
  if (freq) fields.push({ name: TX_FIELD.frequency, label: 'Fréquence porteuse (kHz)', kind: FIELD_KIND.number, defaultValue: freq, fixed: profile.engine === ENGINE.somfy });
  if (tx.te_us !== undefined) fields.push({ name: TX_FIELD.te, label: "TE d'émission (µs)", kind: FIELD_KIND.number, defaultValue: tx.te_us });
  if (tx.repeat_bit !== undefined) fields.push({ name: TX_FIELD.repeat, label: 'Bit de répétition', help: 'Émettre comme la télécommande d\'origine.', kind: FIELD_KIND.toggle, defaultValue: tx.repeat_bit });
  if (tx.rmt !== undefined) fields.push({ name: TX_FIELD.rmt, label: 'Émission matérielle (RMT)', kind: FIELD_KIND.toggle, defaultValue: tx.rmt });
  if (tx.frame_bits !== undefined) fields.push({ name: TX_FIELD.frameBits, label: 'Format de trame', kind: FIELD_KIND.choice, defaultValue: tx.frame_bits, options: [{ value: 56, label: '56 bits (standard)' }, { value: 80, label: '80 bits (étendu)' }] });
  return fields;
}

export const mhz = (khz: number): string => `${(khz / 1000).toFixed(3).replace('.', ',')} MHz`;

export function defaultSummary(field: TxField): string {
  const yesNo = (v: number | boolean): string => (v ? 'oui' : 'non');
  switch (field.name) {
    case TX_FIELD.frequency: return mhz(Number(field.defaultValue));
    case TX_FIELD.te: return `TE ${field.defaultValue} µs`;
    case TX_FIELD.repeat: return `répétition ${yesNo(field.defaultValue)}`;
    case TX_FIELD.rmt: return `RMT ${yesNo(field.defaultValue)}`;
    case TX_FIELD.frameBits: return `${field.defaultValue} bits`;
    default: return String(field.defaultValue);
  }
}

/* Parcours d'ajout d'un profil : il se deduit de ses deux axes. Un moteur sans parcours dans cette interface
   (ook-fixe, io-homecontrol a venir) rend null : son profil n'est pas propose. */
export function flowOf(profile: Pick<Profile, 'add' | 'engine'>): Flow | null {
  if (profile.add === ADD_METHOD.copy && profile.engine === ENGINE.replay) return FLOW.replay;
  if (profile.add === ADD_METHOD.copy && profile.engine === ENGINE.keeloq) return FLOW.learn;
  if (profile.add === ADD_METHOD.enrol && profile.engine === ENGINE.keeloq) return FLOW.enrolPfx;
  if (profile.add === ADD_METHOD.enrol && profile.engine === ENGINE.somfy) return FLOW.createSomfy;
  return null;
}

/* Ou mene une methode de la pyramide, avec l'id du profil pour qu'un F5 garde la marque choisie (l'enrolement la
   perdait). L'id ne depend pas de l'ordre des profils : ajouter un profil ne casse aucune adresse. */
export function methodRoute(profile: Profile): string {
  const id = encodeURIComponent(profile.id);
  switch (flowOf(profile)) {
    case FLOW.learn: case FLOW.replay: return `${TAB.remotes}/${SECTION.learn}/${id}`;
    case FLOW.enrolPfx: return `${TAB.remotes}/${SECTION.enrol}/${id}`;
    case FLOW.createSomfy: return `${TAB.remotes}/${SECTION.somfy}/${id}`;
    default: return `${TAB.remotes}/${SECTION.add}`;
  }
}

/* Phrase de repli par methode, pour un profil sans champ description (les « note » de profiles.json sont des
   notes de developpeur, jamais montrees). */
export const METHOD_SENTENCE: Record<string, string> = {
  [FLOW.learn]: 'Tu as la télécommande d\'origine : le boîtier copie ses commandes, une capture par bouton.',
  [FLOW.replay]: 'Le boîtier enregistre le signal de ta télécommande et le rejoue à l\'identique.',
  [FLOW.enrolPfx]: 'Le boîtier devient une nouvelle télécommande, que le moteur apprend. La télécommande d\'origine sert au geste de validation.',
  [FLOW.createSomfy]: 'Pas besoin de télécommande d\'origine : le boîtier choisit une adresse et le moteur l\'apprend en mode appairage.',
};

/* Espace insecable entre le nombre et l'unite : « MHz » ne passe jamais seul a la ligne. */
const BAND_LABEL: Record<string, string> = { [BAND.mhz868]: '868\u00a0MHz', [BAND.mhz433]: '433\u00a0MHz' };
export const bandLabel = (band: string): string => BAND_LABEL[band] ?? `${band}\u00a0MHz`;
/* Couleur d'une bande (tuiles de marque) : une variable par bande, declaree dans styles/tokens.css. */
const BAND_COLOR: Record<string, string> = { [BAND.mhz868]: 'var(--band-868)', [BAND.mhz433]: 'var(--band-433)' };
export const bandColor = (band: string): string => BAND_COLOR[band] ?? 'var(--muted)';
export const hasComma = (s: string): boolean => String(s).includes(',');   // un nom sert d'identifiant dans des listes a virgules
