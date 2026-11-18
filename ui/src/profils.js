// Lecture de profiles.json, la source unique des types, marques et methodes. Les ecrans ne codent en dur
// aucun type ni aucune marque : ils demandent ici. Meme logique que l'interface d'aujourd'hui
// (profileFields, profileForDevice, deviceKind), rassemblee en un seul module.
import { etat } from './store.js';

export const profils = () => (etat.profils && etat.profils.profils) || [];
export const types = () => (etat.profils && etat.profils.types) || {};

/* Icone du sprite pour un type declare ; un type inconnu prend celle du volet. */
const ICONE_TYPE = { volet: 'volet', portail: 'portail' };
export const iconeType = cle => ICONE_TYPE[cle] || 'volet';

export function cleType(v) {
  const t = types();
  const code = typeof (v && v.type) === 'number' ? v.type : 0;
  return Object.keys(t).find(k => t[k].code === code) || 'volet';
}

export function iconeAppareil(v) {
  if (v.central) return 'centrale';
  if (v.zone) return 'zone';
  return iconeType(cleType(v));
}

/* Mecanisme d'un appareil : enrole (virtuel), rejeu brut, ou clone (KeeLoq capture). */
export const mecanisme = v => (v.virt ? 'enrolement' : v.rejeu ? 'rejeu' : 'cloner');

/* Ce qu'est l'appareil, en mots, et s'il porte des reglages d'emission. */
export function genre(v) {
  if (v.central) return { lbl: 'centrale', tx: false };
  if (v.zone) return { lbl: 'zone', tx: false };
  if (v.rejeu) return { lbl: 'rejeu (code fixe)', tx: true };
  if (v.virt) return { lbl: v.proto === 'somfy' ? 'Somfy RTS' : 'télécommande virtuelle', tx: true };
  return { lbl: 'télécommande clonée', tx: true };
}

/* Profil d'un appareil : la marque gravee dans sa fiche, sinon deduite du protocole. */
export function profilDe(v) {
  let marque = v.marque;
  if (!marque) marque = v.proto === 'somfy' ? 'Somfy RTS' : v.rejeu ? 'CAME' : 'Profalux';
  const mec = mecanisme(v);
  return profils().find(p => p.marque === marque && p.mecanisme === mec) || profils().find(p => p.marque === marque) || null;
}

/* Les champs reglables d'un profil, derives des cles de son bloc tx (jamais une liste en dur). */
export function champsEmission(p) {
  if (!p) return [];
  const tx = p.tx || {}, F = [];
  if (p.freq_khz) F.push({ nom: 'freq_khz', label: 'Fréquence porteuse (kHz)', genre: 'nombre', def: p.freq_khz, fixe: p.marque === 'Somfy RTS' });
  if ('te_us' in tx) F.push({ nom: 'te_us', label: "TE d'émission (µs)", genre: 'nombre', def: tx.te_us });
  if ('repeat_bit' in tx) F.push({ nom: 'tx_rpt', label: 'Bit de répétition', aide: 'Émettre comme la télécommande d\'origine.', genre: 'oui-non', def: tx.repeat_bit });
  if ('rmt' in tx) F.push({ nom: 'tx_rmt', label: 'Émission matérielle (RMT)', genre: 'oui-non', def: tx.rmt });
  if ('frame_bits' in tx) F.push({ nom: 'frame_bits', label: 'Format de trame', genre: 'choix', def: tx.frame_bits, options: [{ value: 56, label: '56 bits (standard)' }, { value: 80, label: '80 bits (étendu)' }] });
  return F;
}

export function resumeDefaut(f) {
  switch (f.nom) {
    case 'freq_khz': return `${(f.def / 1000).toFixed(3).replace('.', ',')} MHz`;
    case 'te_us': return `TE ${f.def} µs`;
    case 'tx_rpt': return `répétition ${f.def ? 'oui' : 'non'}`;
    case 'tx_rmt': return `RMT ${f.def ? 'oui' : 'non'}`;
    case 'frame_bits': return `${f.def} bits`;
    default: return String(f.def);
  }
}

/* Ou mene une methode de la pyramide : les adresses d'aujourd'hui, avec l'index du profil pour qu'un F5
   garde la marque choisie (l'enrolement la perdait). */
export function adresseMethode(p) {
  const i = profils().indexOf(p);
  if (p.flow === 'learn' || p.flow === 'rejeu') return `remotes/learn/${i}`;
  if (p.flow === 'enrol-pfx') return `remotes/enrol/${i}`;
  if (p.flow === 'create-somfy') return `remotes/somfy/${i}`;
  return 'remotes/add';
}

/* Phrase de repli par methode, pour un profil sans champ description (les « note » de profiles.json sont
   des notes de developpeur, jamais montrees). */
export const PHRASE_METHODE = {
  learn: 'Tu as la télécommande d\'origine : le boîtier copie ses commandes, une capture par bouton.',
  rejeu: 'Le boîtier enregistre le signal de ta télécommande et le rejoue à l\'identique.',
  'enrol-pfx': 'Le boîtier devient une nouvelle télécommande, que le moteur apprend. La télécommande d\'origine sert au geste de validation.',
  'create-somfy': 'Pas besoin de télécommande d\'origine : le boîtier choisit une adresse et le moteur l\'apprend en mode appairage.',
};

export const BANDE = b => (b === '868' ? '868 MHz' : b === '433' ? '433 MHz' : `${b} MHz`);
export const aVirgule = s => String(s).includes(',');   // un nom sert d'identifiant dans des listes a virgules
