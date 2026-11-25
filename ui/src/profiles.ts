// Lecture de profiles.json, la source unique des types, marques et methodes. Les ecrans ne codent en dur aucun
// type ni aucune marque : ils demandent ici. Meme logique que l'interface d'avant (profileFields,
// profileForDevice, deviceKind), rassemblee en un seul module.
import type { Device, Profile, TypeEntry } from './api/types';
import {
  BAND, BRAND, DEVICE_TYPE, FIELD_KIND, FLOW, MECHANISM, PROTOCOL, SECTION, TAB, TX_FIELD,
  type FieldKind, type Mechanism, type TxFieldName,
} from './domain';
import { ICON, type IconName } from './icons';
import { state } from './store';

export const profiles = (): Profile[] => state.profiles?.profils ?? [];
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

/* Mecanisme d'un appareil : enrole (virtuel), rejeu brut, ou clone (KeeLoq capture). */
export const mechanism = (device: Device): Mechanism => (device.virt ? MECHANISM.enrolment : device.rejeu ? MECHANISM.replay : MECHANISM.clone);
export const isSomfy = (device: Pick<Device, 'proto'> | null | undefined): boolean => device?.proto === PROTOCOL.somfy;

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
/* Profil d'un appareil : celui de sa marque et de son mecanisme. */
export function profileOf(device: Device): Profile | null {
  const brand = brandOf(device);
  const mech = mechanism(device);
  return profiles().find(p => p.marque === brand && p.mecanisme === mech) ?? profiles().find(p => p.marque === brand) ?? null;
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
  if (profile.freq_khz) fields.push({ name: TX_FIELD.frequency, label: 'Fréquence porteuse (kHz)', kind: FIELD_KIND.number, defaultValue: profile.freq_khz, fixed: profile.marque === BRAND.somfy });
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

/* Ou mene une methode de la pyramide : les adresses d'aujourd'hui, avec l'index du profil pour qu'un F5 garde
   la marque choisie (l'enrolement la perdait). */
export function methodRoute(profile: Profile): string {
  const i = profiles().indexOf(profile);
  if (profile.flow === FLOW.learn || profile.flow === FLOW.replay) return `${TAB.remotes}/${SECTION.learn}/${i}`;
  if (profile.flow === FLOW.enrolPfx) return `${TAB.remotes}/${SECTION.enrol}/${i}`;
  if (profile.flow === FLOW.createSomfy) return `${TAB.remotes}/${SECTION.somfy}/${i}`;
  return `${TAB.remotes}/${SECTION.add}`;
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
