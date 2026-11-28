// Lecture du catalogue (profiles.json), la source unique des protocoles, types et profils. Les ecrans ne codent en
// dur aucun type ni aucune marque : ils demandent ici. Les profils sont resolus (heritage) par la meme fonction que la
// detection et le generateur (pyramid-detect.js), puis mis a plat dans une vue que les ecrans lisent.
import type { Device } from './api/types';
import type { AddMethod, Band, Catalog, Command as ButtonCommand, DeviceType, Encoder, Profile, Protocol, Radio } from './api/catalog.generated';
import {
  BAND, BRAND, COMMAND, DEVICE_TYPE, ENCODER, FIELD_KIND, FLOW, MECHANISM, MOVE_COMMANDS, PROTOCOL, SECTION, TAB, TX_FIELD, isCommand,
  type Command, type FieldKind, type Flow, type Mechanism, type MoveCommand, type TxFieldName,
} from './domain';
import { resolveProfiles } from './lib/detection';
import { ICON, type IconName } from './icons';
import { state } from './store';

/* Methodes d'ajout du catalogue (profils « add »). */
export const ADD = { copy: 'copy', enrol: 'enrol' } as const satisfies Record<string, AddMethod>;
/* Codage d'un protocole (« code ») : un code fixe se rejoue tel quel. */
const CODE_FIXED = 'fixed';
/* Commande d'un bouton pas a pas (types[].buttons[].command). */
export const STEP = COMMAND.step satisfies ButtonCommand;

/** Un profil resolu, a plat : ce que les ecrans lisent. */
export interface ProfileView {
  id: string;
  name: string;
  description?: string;
  brand: string;              // la marque ; un profil sans marque se range sous son nom (jamais une chaine vide)
  type: string;               // le premier type du profil (cle de types)
  protocol: string;           // cle de protocols
  encoder: Encoder;
  add: AddMethod;
  mechanism: Mechanism;
  flow: Flow;
  band: Band;
  freq_khz: number;
  rx?: Radio['rx'];
  tx: NonNullable<Profile['tx']>;
  models: string[];
  instructions: NonNullable<Profile['instructions']>;
}

export const catalog = (): Catalog | null => state.profiles;
export const types = (): Record<string, DeviceType> => state.profiles?.types ?? {};
const protocols = (): Record<string, Protocol> => state.profiles?.protocols ?? {};

/* Mecanisme d'un profil, d'apres sa methode et le codage de son protocole : un enrolement, un rejeu (code fixe
   copie), ou un clone (code tournant copie : le boitier apprend l'identite). */
const mechanismOf = (add: AddMethod, protocol: Protocol | undefined): Mechanism =>
  add === ADD.enrol ? MECHANISM.enrolment : protocol?.code === CODE_FIXED ? MECHANISM.replay : MECHANISM.clone;
/* Parcours d'aujourd'hui d'un enrolement, par encodeur. Un seul ecran deroulera la procedure du protocole (#134,
   etape 8) ; d'ici la, chaque encodeur garde le sien. */
const ENROL_FLOW: Partial<Record<Encoder, Flow>> = { [ENCODER.keeloq]: FLOW.enrolPfx, [ENCODER.somfyRts]: FLOW.createSomfy };
const COPY_FLOW: Record<Mechanism, Flow> = { [MECHANISM.replay]: FLOW.replay, [MECHANISM.clone]: FLOW.learn, [MECHANISM.enrolment]: FLOW.learn };

function view(p: Profile): ProfileView | null {
  const protocol = protocols()[p.protocol ?? ''];
  const radio = p.radio ?? {};
  if (!protocol || !p.add || !p.types?.[0] || !radio.band || !radio.freq_khz) return null;   // le generateur l'a refuse
  const mechanism = mechanismOf(p.add, protocol);
  const flow = p.add === ADD.enrol ? ENROL_FLOW[protocol.encoder] : COPY_FLOW[mechanism];
  if (!flow) return null;
  return {
    id: p.id, name: p.name, description: p.description, brand: p.brand ?? p.name, type: p.types[0], protocol: p.protocol ?? '',
    encoder: protocol.encoder, add: p.add, mechanism, flow, band: radio.band, freq_khz: radio.freq_khz, rx: radio.rx,
    tx: p.tx ?? {}, models: p.models ?? [], instructions: p.instructions ?? {},
  };
}

/* Les profils resolus, recalcules seulement quand le catalogue change. */
let cache: { source: Catalog; list: ProfileView[] } | null = null;
export function profiles(): ProfileView[] {
  const doc = state.profiles;
  if (!doc) return [];
  if (cache?.source !== doc) cache = { source: doc, list: resolveProfiles(doc).flatMap(p => view(p) ?? []) };
  return cache.list;
}
export const profileById = (id: string): ProfileView | null => profiles().find(p => p.id === id) ?? null;

/* Icone d'un type ou d'un role : le dessin de la planche que profiles.json NOMME (le portail cloud lit la meme
   declaration : meme dessin des deux cotes). Un nom absent ou hors de la planche prend le dessin de repli. */
const ICON_NAMES: readonly string[] = Object.values(ICON);
const isIconName = (name: string | undefined): name is IconName => name !== undefined && ICON_NAMES.includes(name);
const declaredIcon = (name: string | undefined, fallback: IconName): IconName => (isIconName(name) ? name : fallback);
export const typeIcon = (key: string): IconName => declaredIcon(types()[key]?.icon, ICON.shutter);

/* Ou le firmware range la trame d'un bouton capture. Le pas a pas n'existe pas encore pour lui (#134, etape 5) :
   il range la trame unique du portail sous « up » et la rejoue pour les trois commandes. */
const captureSlot = (command: ButtonCommand): Command => (command !== STEP && isCommand(command) ? command : COMMAND.up);

/** Un type d'appareil, tel que les ecrans le lisent. */
export interface TypeView {
  key: string;
  label: string;
  positional: boolean;
  travel_s?: DeviceType['travel_s'];
  dpad: Record<MoveCommand, string>;          // libelles des commandes (pave, Home Assistant, cloud)
  capture: { cmd: Command; label: string }[]; // un creneau de capture par bouton
}
const DEFAULT_DPAD: Record<MoveCommand, string> = { [COMMAND.up]: 'Montée', [COMMAND.stop]: 'Stop', [COMMAND.down]: 'Descente' };
export function typeView(key: string): TypeView {
  const t = types()[key];
  if (!t) return { key, label: 'Volet', positional: true, dpad: DEFAULT_DPAD, capture: MOVE_COMMANDS.map(cmd => ({ cmd, label: DEFAULT_DPAD[cmd] })) };
  const dpad = { [COMMAND.up]: t.commands.up.label, [COMMAND.stop]: t.commands.stop.label, [COMMAND.down]: t.commands.down.label };
  const capture = t.buttons.map(b => ({
    cmd: captureSlot(b.command),
    label: b.label ?? (b.command === COMMAND.up || b.command === COMMAND.stop || b.command === COMMAND.down ? dpad[b.command] : b.command),
  }));
  return { key, label: t.label, positional: t.positional, travel_s: t.travel_s, dpad, capture };
}

/* Le type d'un appareil : la cle que porte sa fiche (le firmware traduit l'ancien numero, mode legacy). Une cle que
   ce catalogue ne connait pas s'affiche comme un volet. */
export const typeKey = (device: Pick<Device, 'type'> | null | undefined): string =>
  (device?.type && types()[device.type] ? device.type : DEVICE_TYPE.shutter);
export const typeOf = (device: Pick<Device, 'type'> | null | undefined): TypeView => typeView(typeKey(device));

export function deviceIcon(device: Device): IconName {
  if (device.central) return declaredIcon(state.profiles?.roles?.central?.icon, ICON.central);
  if (device.zone) return declaredIcon(state.profiles?.roles?.zone?.icon, ICON.zone);
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

/* Marque d'un appareil : celle gravee dans sa fiche, sinon deduite du protocole. Ne depend pas du catalogue. */
export const brandOf = (device: Device): string => device.marque || (isSomfy(device) ? BRAND.somfy : device.rejeu ? BRAND.came : BRAND.profalux);
/* Profil d'un appareil : celui que porte sa fiche ; pour un appareil inclassable, celui de sa marque et de son
   mecanisme. */
export function profileOf(device: Device): ProfileView | null {
  const own = device.profile ? profileById(device.profile) : null;
  if (own) return own;
  const brand = brandOf(device);
  const mech = mechanism(device);
  return profiles().find(p => p.brand === brand && p.mechanism === mech) ?? profiles().find(p => p.brand === brand) ?? null;
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

/* Les champs reglables d'un profil, derives des cles de son bloc tx (jamais une liste en dur). La porteuse de Somfy
   RTS est celle de la norme : on la montre, on ne la regle pas. */
export function txFields(profile: ProfileView | null | undefined): TxField[] {
  if (!profile) return [];
  const tx = profile.tx, fields: TxField[] = [];
  fields.push({ name: TX_FIELD.frequency, label: 'Fréquence porteuse (kHz)', kind: FIELD_KIND.number, defaultValue: profile.freq_khz, fixed: profile.encoder === ENCODER.somfyRts });
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

/* Ou mene une methode de la pyramide, avec l'identifiant du profil pour qu'un F5 garde la marque choisie. */
const FLOW_SECTION: Record<Flow, string> = { [FLOW.learn]: SECTION.learn, [FLOW.replay]: SECTION.learn, [FLOW.enrolPfx]: SECTION.enrol, [FLOW.createSomfy]: SECTION.somfy };
export const methodRoute = (profile: ProfileView): string => `${TAB.remotes}/${FLOW_SECTION[profile.flow]}/${encodeURIComponent(profile.id)}`;

/* Phrase de repli par methode, pour un profil sans champ description (les « note » du catalogue sont des notes de
   developpeur, jamais montrees). */
export const METHOD_SENTENCE: Record<Flow, string> = {
  [FLOW.learn]: 'Tu as la télécommande d\'origine : le boîtier copie ses commandes, une capture par bouton.',
  [FLOW.replay]: 'Le boîtier enregistre le signal de ta télécommande et le rejoue à l\'identique.',
  [FLOW.enrolPfx]: 'Le boîtier devient une nouvelle télécommande, que le moteur apprend. La télécommande d\'origine sert au geste de validation.',
  [FLOW.createSomfy]: 'Pas besoin de télécommande d\'origine : le boîtier choisit une adresse et le moteur l\'apprend en mode appairage.',
};

/* Espace insecable entre le nombre et l'unite : « MHz » ne passe jamais seul a la ligne. */
const BAND_LABEL: Record<string, string> = { [BAND.mhz868]: '868 MHz', [BAND.mhz433]: '433 MHz' };
export const bandLabel = (band: string): string => BAND_LABEL[band] ?? `${band} MHz`;
/* Couleur d'une bande (tuiles de marque) : une variable par bande, declaree dans styles/tokens.css. */
const BAND_COLOR: Record<string, string> = { [BAND.mhz868]: 'var(--band-868)', [BAND.mhz433]: 'var(--band-433)' };
export const bandColor = (band: string): string => BAND_COLOR[band] ?? 'var(--muted)';
export const hasComma = (s: string): boolean => String(s).includes(',');   // un nom sert d'identifiant dans des listes a virgules
