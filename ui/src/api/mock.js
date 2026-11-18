// Fausses donnees de la maquette : les memes champs que /api/status du firmware (shutters_status_json),
// avec des noms generiques. Les ordres sont simules : un volet met quelques secondes a monter ou a
// descendre, une centrale entraine ses volets, une zone ses suiveurs. Position en % de FERMETURE, comme le
// firmware (0 = ouvert, 100 = ferme). Les captures, l'enrolement et le
// calage reussissent apres un court delai, comme si on appuyait bien sur la telecommande.
import profils from '../../../firmware/main/web/profiles.json';   // la vraie table, source unique

const attendre = ms => new Promise(r => setTimeout(r, ms));
const t0 = Date.now();

/* Reglages d'emission d'une fiche : 0 = heriter du profil (tri-etat 0 / 1 non / 2 oui pour tx_rpt, tx_rmt). */
const TX0 = { freq_khz: 0, te_us: 0, tx_rpt: 0, tx_rmt: 0, frame_bits: 0 };
const cmd3 = s => ({ up: { b: 2, s }, stop: { b: 4, s }, down: { b: 8, s } });

const volets = [
  { id: 'Général', central: true, members: 'Salon,Cuisine,Chambre,Bureau', order: 0, serials: ['0x0A1B2E0'], cmd: {} },
  { id: 'Salon', type: 0, marque: 'Profalux', ...TX0, position: 0, order: 1, orientation: 180, serials: ['0x0A1B2C3'], cmd: cmd3('0x0A1B2C3'), travel_up_ms: 21000, travel_down_ms: 19500 },
  { id: 'Cuisine', type: 0, marque: 'Profalux', ...TX0, position: 65, order: 2, orientation: 90, serials: ['0x0A1B2D7'], cmd: cmd3('0x0A1B2D7'), travel_up_ms: 18000, travel_down_ms: 17000 },
  { id: 'Chambre', type: 0, marque: 'Profalux', ...TX0, te_us: 400, tx_rpt: 2, position: 100, order: 3, orientation: -1, virt: true, proto: 'profalux', virt_serial: '0x0670012', serials: ['0x0B44A10'], cmd: {}, travel_up_ms: 20000, travel_down_ms: 19000 },
  { id: 'Bureau', type: 0, marque: 'Profalux', ...TX0, position: 40, order: 4, orientation: -1, serials: ['0x0B44A21'], cmd: { up: { b: 2, s: '0x0B44A21' }, down: { b: 8, s: '0x0B44A21' } }, travel_up_ms: 16000, travel_down_ms: 15000 },
  { id: 'Étage', type: 0, zone: true, members: 'Chambre,Bureau', order: 5, serials: ['0x0B44A30'], cmd: {} },
  { id: 'Portail entrée', type: 1, marque: 'CAME', ...TX0, freq_khz: 433920, rejeu: true, rejeu_cmds: { up: true }, order: 6, serials: [], cmd: {} },
];
const remotes = {
  '0x0A1B2E0': 'Télécommande générale', '0x0A1B2C3': 'Salon', '0x0A1B2D7': 'Cuisine', '0x0B44A10': 'Chambre', '0x0B44A21': 'Bureau',
  '0x0B44A30': 'Murale étage', '0x0D12E77': '',
};
let remotes_ignored = ['0x0C99F01'];
const remote_battery = { '0x0A1B2C3': { low: false }, '0x0A1B2D7': { low: true }, '0x0B44A21': { low: false }, '0x0A1B2E0': { low: false } };
const rf = [
  { serial: '0x0A1B2C3', rssi: -58, button: '2' }, { serial: '0x0A1B2D7', rssi: -81, button: '8' },
  { serial: '0x0B44A21', rssi: -66, button: '4' }, { serial: '0x0A1B2E0', rssi: -63, button: '2' }, { serial: '0x0D12E77', rssi: -92, button: '2' },
];
const radios = [
  { idx: 0, band: '868', chip: 'CC1101', freq_khz: 868425, ok: true },
  { idx: 1, band: '433', chip: 'CC1101', freq_khz: 433920, ok: true },
];
const MODELES = [
  { name: 'Profalux MAI-EMPX / MUR / NOE', routine: 'R6' },
  { name: 'FranciaFlex M4G', routine: 'R6' },
  { name: 'Profalux NeoSol', routine: 'R8' },
  { name: 'Profalux MAI-RDPX / RDTELPX', routine: 'R7' },
];

/* Mouvements en cours : id -> { de, vers, debut, duree } (position en % de fermeture, 100 = ferme). */
const mouvements = {};
/* Deja connecte ; avec ?connexion dans l'adresse, l'ecran de connexion s'affiche (essais). */
let session = { protected: true, authed: !new URLSearchParams(location.search).has('connexion') };
let pfx = null;                                    // identite virtuelle en cours d'enrolement
let prochaineIdentite = 0x0670013;

const trouver = id => volets.find(x => x.id === id);
const hex7 = n => `0x${n.toString(16).toUpperCase().padStart(7, '0')}`;

function avancer() {
  const now = Date.now();
  for (const [id, m] of Object.entries(mouvements)) {
    const v = trouver(id);
    if (!v) { delete mouvements[id]; continue; }
    const f = Math.min(1, (now - m.debut) / m.duree);
    v.position = Math.round(m.de + (m.vers - m.de) * f);
    if (f >= 1) delete mouvements[id];
  }
}

function lancer(v, cmd, valeur) {
  if (v.position == null || !['up', 'down', 'stop', 'pos'].includes(cmd)) return;
  if (cmd === 'stop') { avancer(); delete mouvements[v.id]; return; }
  const vers = cmd === 'up' ? 0 : cmd === 'down' ? 100 : Math.max(0, Math.min(100, valeur));
  avancer();
  const course = vers < v.position ? (v.travel_up_ms || 18000) : (v.travel_down_ms || 17000);
  const duree = Math.max(400, course * Math.abs(vers - v.position) / 100 / 3);   // accelere x3 pour la maquette
  mouvements[v.id] = { de: v.position, vers, debut: Date.now(), duree };
}

/* Un nom d'appareil sert de cle (centrales, zones, Home Assistant) : on le remplace partout. */
function remplacerMembre(ancien, nouveau) {
  for (const v of volets) {
    if (!v.members) continue;
    v.members = v.members.split(',').map(x => (x === ancien ? nouveau : x)).filter(Boolean).join(',');
  }
}

function nouvelAppareil(champs) {
  const v = { type: 0, ...TX0, position: 100, order: -1, orientation: -1, serials: [], cmd: {}, travel_up_ms: 0, travel_down_ms: 0, ...champs };
  volets.push(v);
  return v;
}

const copie = o => JSON.parse(JSON.stringify(o));
const ok = (extra = {}) => ({ ok: 1, ...extra });

export default {
  async session() { return { ...session }; },
  async connexion(pass) {
    await attendre(500);
    if (!pass || pass === 'faux') return false;   // maquette : tout mot de passe sauf « faux »
    session.authed = true;
    return true;
  },
  async deconnexion() { session.authed = false; },
  async profils() { return copie(profils); },
  async statut() {
    avancer();
    return copie({
      uptime: Math.round((Date.now() - t0) / 1000) + 86400 * 3 + 7260,
      listening: true,
      volets, remotes, remote_battery, rf, remotes_ignored,
      remotes_max: 16,
      wifi: { connected: true, ssid: 'MaBox', rssi: -54, ip: '192.168.1.50', fallback: false },
      mqtt: true,
    });
  },
  async ordre({ id, cmd, value }) {
    await attendre(900);   // la radio emet ~1 s, comme le firmware
    const v = trouver(id);
    if (!v) throw 404;
    const cibles = v.central || v.zone ? [v.zone ? v : null, ...v.members.split(',').map(trouver)] : [v];
    cibles.filter(Boolean).forEach(c => lancer(c, cmd, value));
    return ok();
  },
  async ordreAffichage(ids) {
    volets.forEach(v => { v.order = ids.indexOf(v.id); });
    return ok();
  },

  /* ── Appareils ── */
  async radios() { return copie(radios); },
  /* Detection : la radio 868 entend la telecommande, la 433 rien. */
  async ecouter(radio) {
    await attendre(2200);
    const r = radios.find(x => x.idx === radio);
    return r && r.band === '868' ? { n: 4, band: '868', rssi: -61 } : { n: 0 };
  },
  /* Capture d'une commande (clone KeeLoq ou rejeu brut). Cree l'appareil au premier appui. */
  async capturer({ id, action, rejeu, type, marque, nomTelec }) {
    await attendre(2300);
    let v = trouver(id);
    const serial = '0x0E5A3B9';
    if (!v) v = rejeu ? nouvelAppareil({ id, type, marque, rejeu: true, rejeu_cmds: {}, position: undefined }) : nouvelAppareil({ id, type, marque, serials: [serial] });
    if (rejeu) { v.rejeu_cmds[action] = true; return { n: 412 }; }
    v.cmd[action] = { b: { up: 2, stop: 4, down: 8 }[action], s: serial };
    if (nomTelec) remotes[serial] = nomTelec;
    else if (!(serial in remotes)) remotes[serial] = '';
    return { serial, button: { up: '2', stop: '4', down: '8' }[action], rssi: -57 };
  },
  async deplacerCapture({ id, de, vers }) {
    const v = trouver(id);
    [v.cmd[de], v.cmd[vers]] = [v.cmd[vers], v.cmd[de]];
    if (!v.cmd[de]) delete v.cmd[de];
    return ok();
  },
  /* « Caler sur ma telecommande » : balayage autour de la porteuse, renvoie le centre trouve. */
  async caler(radio) {
    await attendre(2600);
    const from_khz = radio === 1 ? 433920 : 868425;
    return { state: 2, status: 0, from_khz, center_khz: radio === 1 ? 433905 : 868338, peak_dbm: -47, floor_dbm: -103 };
  },
  async emission({ id, ...vals }) {
    Object.assign(trouver(id), vals);
    return ok();
  },
  async renommer(id, nom) {
    await attendre(300);
    if (trouver(nom)) throw 'exists';
    trouver(id).id = nom;
    remplacerMembre(id, nom);
    return ok();
  },
  async changerType(id, type) { trouver(id).type = type; return ok(); },
  async supprimer(id) {
    await attendre(300);
    volets.splice(volets.indexOf(trouver(id)), 1);
    remplacerMembre(id, '');
    return ok();
  },
  async orientation(id, valeur) { trouver(id).orientation = valeur; return ok(); },
  async suiveurs(id, membres) {
    const v = trouver(id);
    if (membres.length) { v.zone = true; v.members = membres.join(','); }
    else { delete v.zone; delete v.members; }
    return ok();
  },

  /* Telecommande virtuelle (enrolement) */
  async pfxEtat() { return { models: MODELES, active: !!pfx, learn_ok: !!pfx, ...(pfx || {}) }; },
  async pfxCapturer({ model, marque }) {
    await attendre(2500);
    pfx = { serial: hex7(prochaineIdentite++), idx: 12, counter: 1, remote: '0x0E5A3B9', model, marque };
    return { done: true, rc: 0, new: true, serial: pfx.serial, remote: pfx.remote };
  },
  async pfxRenouveler() { pfx.serial = hex7(prochaineIdentite++); pfx.idx += 1; return ok({ serial: pfx.serial }); },
  async pfxApprendre() { await attendre(4500); pfx.counter += 6; return ok(); },
  async pfxCmd() { await attendre(900); pfx.counter += 1; return ok(); },
  async pfxEnregistrer(id) {
    await attendre(300);
    if (trouver(id)) throw 'exists';
    nouvelAppareil({ id, virt: true, proto: 'profalux', virt_serial: pfx.serial, marque: pfx.marque || 'Profalux' });
    pfx = null;
    return ok();
  },
  async pfxOublier() { pfx = null; return ok(); },

  /* Somfy RTS */
  async somfyCreer({ id, address }) {
    await attendre(400);
    if (trouver(id)) throw 'exists';
    nouvelAppareil({ id, virt: true, proto: 'somfy', marque: 'Somfy RTS', virt_serial: address, freq_khz: 433420, frame_bits: 56 });
    return ok();
  },

  /* Centrale, calibration */
  async centrale({ id, members }) {
    await attendre(300);
    const v = trouver(id);
    if (v && !v.central) throw 'exists';
    if (v) v.members = members.join(',');
    else nouvelAppareil({ id, central: true, members: members.join(','), position: undefined });
    return ok();
  },
  async calibrer({ id, travel_up_ms, travel_down_ms }) {
    Object.assign(trouver(id), { travel_up_ms, travel_down_ms });
    return ok();
  },

  /* Telecommandes captees */
  async telecNommer(serial, name) { remotes[serial] = name; return ok(); },
  async telecSupprimer(serial) { delete remotes[serial]; return ok(); },
  async telecIgnorer(serial) {
    delete remotes[serial];
    if (!remotes_ignored.includes(serial)) remotes_ignored.push(serial);
    return ok();
  },
  async telecRetablir(serial) { remotes_ignored = remotes_ignored.filter(s => s !== serial); return ok(); },
  /* Creer un volet depuis les dernieres trames d'une telecommande dans le journal RF : ici, montee et
     descente sont dans le journal, pas stop. */
  async voletDepuisTelec(serial, name) {
    await attendre(400);
    if (trouver(name)) throw 'exists';
    nouvelAppareil({ id: name, marque: 'Profalux', serials: [serial], cmd: { up: { b: 2, s: serial }, down: { b: 8, s: serial } } });
    return { learned: 'up,down' };
  },
};
