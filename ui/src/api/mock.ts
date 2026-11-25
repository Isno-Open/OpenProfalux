// Fausses donnees (mode mock) : les memes champs que /api/status du firmware (shutters_status_json), avec des
// noms generiques. Les ordres sont simules : un volet met quelques secondes a monter ou a descendre, une
// centrale entraine ses volets, une zone ses suiveurs. Position en % de FERMETURE, comme le firmware (0 =
// ouvert, 100 = ferme). Les captures, l'enrolement et le calage reussissent apres un court delai, comme si on
// appuyait bien sur la telecommande.
import profilesTable from '../../../firmware/main/web/profiles.json';   // la vraie table, source unique
import { BAND, BRAND, COMMAND, FLAG, PROTOCOL, type Command } from '../domain';
import type realApi from './real';
import type { CaptureRequest, CaptureResult } from './real';
import type { Device, LearnedCommand, OkReply, PfxState, Profiles, RadioSummary, RfFrame, Session, ShutterOrder, Status } from './types';

const WRONG_PASSWORD = 'faux';   // le seul mot de passe refuse en mode mock (essai de l'ecran de connexion)
const wait = (ms: number): Promise<void> => new Promise(resolve => setTimeout(resolve, ms));
const startedAt = Date.now();

/* Reglages d'emission d'une fiche : 0 = heriter du profil (tri-etat 0 / 1 non / 2 oui pour tx_rpt, tx_rmt). */
const TX_INHERIT = { freq_khz: 0, te_us: 0, tx_rpt: 0, tx_rmt: 0, frame_bits: 0 };
const BUTTON_CODE: Partial<Record<Command, number>> = { [COMMAND.up]: 2, [COMMAND.stop]: 4, [COMMAND.down]: 8 };
const threeCommands = (s: string): Device['cmd'] => ({ up: { b: 2, s }, stop: { b: 4, s }, down: { b: 8, s } });

const devices: Device[] = [
  { id: 'Général', central: true, members: 'Salon,Cuisine,Chambre,Bureau', order: 0, serials: ['0x0A1B2E0'], cmd: {} },
  { id: 'Salon', type: 0, marque: 'Profalux', ...TX_INHERIT, position: 0, order: 1, orientation: 180, serials: ['0x0A1B2C3'], cmd: threeCommands('0x0A1B2C3'), travel_up_ms: 21000, travel_down_ms: 19500 },
  { id: 'Cuisine', type: 0, marque: 'Profalux', ...TX_INHERIT, position: 65, order: 2, orientation: 90, serials: ['0x0A1B2D7'], cmd: threeCommands('0x0A1B2D7'), travel_up_ms: 18000, travel_down_ms: 17000 },
  { id: 'Chambre', type: 0, marque: 'Profalux', ...TX_INHERIT, te_us: 400, tx_rpt: 2, position: 100, order: 3, orientation: -1, virt: true, proto: 'profalux', virt_serial: '0x0670012', serials: ['0x0B44A10'], cmd: {}, travel_up_ms: 20000, travel_down_ms: 19000 },
  { id: 'Bureau', type: 0, marque: 'Profalux', ...TX_INHERIT, freq_khz: 868330, position: 40, order: 4, orientation: -1, serials: ['0x0B44A21'], cmd: { up: { b: 2, s: '0x0B44A21' }, down: { b: 8, s: '0x0B44A21' } }, travel_up_ms: 16000, travel_down_ms: 15000 },
  { id: 'Étage', type: 0, zone: true, members: 'Chambre,Bureau', order: 5, serials: ['0x0B44A30'], cmd: {} },
  { id: 'Portail entrée', type: 1, marque: 'CAME', ...TX_INHERIT, freq_khz: 433920, rejeu: true, rejeu_cmds: { up: true }, order: 6, serials: [], cmd: {} },
];
const remotes: Record<string, string> = {
  '0x0A1B2E0': 'Télécommande générale', '0x0A1B2C3': 'Salon', '0x0A1B2D7': 'Cuisine', '0x0B44A10': 'Chambre', '0x0B44A21': 'Bureau',
  '0x0B44A30': 'Murale étage', '0x0D12E77': '',
};
let ignoredRemotes = ['0x0C99F01'];
const remoteBattery = { '0x0A1B2C3': { low: false }, '0x0A1B2D7': { low: true }, '0x0B44A21': { low: false }, '0x0A1B2E0': { low: false } };
const rfFrames: RfFrame[] = [
  { serial: '0x0A1B2C3', rssi: -58, button: '2' }, { serial: '0x0A1B2D7', rssi: -81, button: '8' },
  { serial: '0x0B44A21', rssi: -66, button: '4' }, { serial: '0x0A1B2E0', rssi: -63, button: '2' }, { serial: '0x0D12E77', rssi: -92, button: '2' },
];
const radios: RadioSummary[] = [
  { idx: 0, band: BAND.mhz868, chip: 'CC1101', freq_khz: 868425, ok: true },
  { idx: 1, band: BAND.mhz433, chip: 'CC1101', freq_khz: 433920, ok: true },
];
const PFX_MODELS = [
  { name: 'Profalux MAI-EMPX / MUR / NOE', routine: 'R6' },
  { name: 'FranciaFlex M4G', routine: 'R6' },
  { name: 'Profalux NeoSol', routine: 'R8' },
  { name: 'Profalux MAI-RDPX / RDTELPX', routine: 'R7' },
];

/* Mouvements en cours : position de depart et d'arrivee (% de fermeture, 100 = ferme), debut, duree. */
interface Movement { from: number; to: number; start: number; duration: number }
const movements: Record<string, Movement> = {};
/* Deja connecte ; avec ?connexion dans l'adresse, l'ecran de connexion s'affiche (essais). */
const session: Session = { protected: true, authed: !new URLSearchParams(location.search).has('connexion') };
interface PfxIdentity { serial: string; idx: number; counter: number; remote: string; model: number; marque?: string }
let pfx: PfxIdentity | null = null;                // identite virtuelle en cours d'enrolement
let nextIdentity = 0x0670013;

const find = (id: string): Device | undefined => devices.find(x => x.id === id);
const mustFind = (id: string): Device => { const d = find(id); if (!d) throw 404; return d; };
const hex7 = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(7, '0')}`;

function advance(): void {
  const now = Date.now();
  for (const [id, m] of Object.entries(movements)) {
    const d = find(id);
    if (!d) { delete movements[id]; continue; }
    const f = Math.min(1, (now - m.start) / m.duration);
    d.position = Math.round(m.from + (m.to - m.from) * f);
    if (f >= 1) delete movements[id];
  }
}

function start(d: Device, cmd: Command, value?: number): void {
  if (d.position == null) return;
  if (cmd === COMMAND.stop) { advance(); delete movements[d.id]; return; }
  let to: number;
  if (cmd === COMMAND.up) to = 0;
  else if (cmd === COMMAND.down) to = 100;
  else if (cmd === COMMAND.position) to = Math.max(0, Math.min(100, value ?? 0));
  else return;
  advance();
  const travel = to < d.position ? (d.travel_up_ms || 18000) : (d.travel_down_ms || 17000);
  const duration = Math.max(400, travel * Math.abs(to - d.position) / 100 / 3);   // accelere x3 pour le mode mock
  movements[d.id] = { from: d.position, to, start: Date.now(), duration };
}

/* Un nom d'appareil sert de cle (centrales, zones, Home Assistant) : on le remplace partout. */
function replaceMember(previous: string, next: string): void {
  for (const d of devices) {
    if (!d.members) continue;
    d.members = d.members.split(',').map(x => (x === previous ? next : x)).filter(Boolean).join(',');
  }
}

function newDevice(fields: Partial<Device> & { id: string }): Device {
  const d: Device = { type: 0, ...TX_INHERIT, position: 100, order: -1, orientation: -1, serials: [], cmd: {}, travel_up_ms: 0, travel_down_ms: 0, ...fields };
  devices.push(d);
  return d;
}

const copy = <T>(o: T): T => JSON.parse(JSON.stringify(o));
const ok = (): OkReply => ({ ok: 1 });
const okWith = <T extends object>(extra: T): OkReply & T => ({ ok: 1, ...extra });
/* La table est verifiee contre le contrat : un champ de profiles.json qui s'en ecarte casse la verification. */
const PROFILES: Profiles = profilesTable;

export default {
  async session() { return { ...session }; },
  async login(pass: string) {
    await wait(500);
    if (!pass || pass === WRONG_PASSWORD) return false;   // mode mock : tout mot de passe sauf « faux »
    session.authed = true;
    return true;
  },
  async logout(): Promise<unknown> { session.authed = false; return null; },
  async profiles(): Promise<Profiles> { return copy(PROFILES); },
  async status(): Promise<Status> {
    advance();
    return copy({
      uptime: Math.round((Date.now() - startedAt) / 1000) + 86400 * 3 + 7260,
      listening: true,
      volets: devices, remotes, remote_battery: remoteBattery, rf: rfFrames, remotes_ignored: ignoredRemotes,
      remotes_max: 16,
      wifi: { connected: true, ssid: 'MaBox', rssi: -54, ip: '192.168.1.50', fallback: false },
      mqtt: true,
    });
  },
  async sendShutter({ id, cmd, value }: ShutterOrder) {
    await wait(900);   // la radio emet ~1 s, comme le firmware
    const d = mustFind(id);
    const members = (d.members || '').split(',').map(find);
    const targets = d.central || d.zone ? [d.zone ? d : undefined, ...members] : [d];
    targets.forEach(t => { if (t) start(t, cmd, value); });
    return ok();
  },
  async saveDisplayOrder(ids: string[]) {
    devices.forEach(d => { d.order = ids.indexOf(d.id); });
    return ok();
  },

  /* ── Appareils ── */
  async radios() { return copy(radios); },
  /* Detection : la radio 868 entend la telecommande, la 433 rien. */
  async listen(radio: number) {
    await wait(2200);
    const r = radios.find(x => x.idx === radio);
    return r && r.band === BAND.mhz868 ? { n: 4, band: BAND.mhz868, rssi: -61 } : { n: 0 };
  },
  /* Capture d'une commande (clone KeeLoq ou rejeu brut). Cree l'appareil au premier appui. */
  async capture({ id, action, replay, type, brand, remoteName }: CaptureRequest): Promise<CaptureResult> {
    await wait(2300);
    const serial = '0x0E5A3B9';
    const d = find(id) ?? (replay
      ? newDevice({ id, type, marque: brand, rejeu: true, rejeu_cmds: {}, position: undefined })
      : newDevice({ id, type, marque: brand, serials: [serial] }));
    if (replay) { d.rejeu_cmds = { ...d.rejeu_cmds, [action]: true }; return { n: 412 }; }
    const learned: LearnedCommand = { b: BUTTON_CODE[action] ?? 0, s: serial };
    d.cmd[action] = learned;
    if (remoteName) remotes[serial] = remoteName;
    else if (!(serial in remotes)) remotes[serial] = '';
    return { serial, button: String(BUTTON_CODE[action] ?? 0), rssi: -57 };
  },
  async moveCapture({ id, from, to }: { id: string; from: Command; to: Command }) {
    const d = mustFind(id);
    const [a, b] = [d.cmd[from], d.cmd[to]];
    if (b) d.cmd[from] = b; else delete d.cmd[from];
    if (a) d.cmd[to] = a; else delete d.cmd[to];
    return ok();
  },
  /* « Caler sur ma telecommande » : balayage autour de la porteuse, renvoie le centre trouve. */
  async tuneToRemote(radio: number) {
    await wait(2600);
    const from_khz = radio === 1 ? 433920 : 868425;
    return { state: 2, status: 0, from_khz, center_khz: radio === 1 ? 433905 : 868338, peak_dbm: -47, floor_dbm: -103 };
  },
  async saveTxParams({ id, ...values }: Record<string, unknown>) {
    if (typeof id === 'string') Object.assign(mustFind(id), values);
    return ok();
  },
  async rename(id: string, name: string) {
    await wait(300);
    if (find(name)) throw 'exists';
    mustFind(id).id = name;
    replaceMember(id, name);
    return ok();
  },
  async setType(id: string, type: number) { mustFind(id).type = type; return ok(); },
  async deleteDevice(id: string) {
    await wait(300);
    devices.splice(devices.indexOf(mustFind(id)), 1);
    replaceMember(id, '');
    return ok();
  },
  async setOrientation(id: string, value: number) { mustFind(id).orientation = value; return ok(); },
  async setFollowers(id: string, members: string[]) {
    const d = mustFind(id);
    if (members.length) { d.zone = true; d.members = members.join(','); }
    else { delete d.zone; delete d.members; }
    return ok();
  },

  /* Telecommande virtuelle (enrolement) */
  async pfxState(): Promise<PfxState> { return { models: PFX_MODELS, active: pfx ? FLAG.on : FLAG.off, learn_ok: pfx ? FLAG.on : FLAG.off, ...(pfx || {}) }; },
  async pfxCapture({ model, marque }: Record<string, unknown>) {
    await wait(2500);
    pfx = { serial: hex7(nextIdentity++), idx: 12, counter: 1, remote: '0x0E5A3B9', model: Number(model) || 0, marque: typeof marque === 'string' ? marque : undefined };
    return { done: true, rc: 0, new: true, serial: pfx.serial, remote: pfx.remote };
  },
  async pfxRenew() {
    if (!pfx) return { ok: 0 };
    pfx.serial = hex7(nextIdentity++); pfx.idx += 1;
    return okWith({ serial: pfx.serial });
  },
  async pfxLearn() { await wait(4500); if (pfx) pfx.counter += 6; return ok(); },
  async pfxCommand() { await wait(900); if (pfx) pfx.counter += 1; return ok(); },
  async pfxSave(id: string) {
    await wait(300);
    if (find(id)) throw 'exists';
    newDevice({ id, virt: true, proto: PROTOCOL.profalux, virt_serial: pfx?.serial, marque: pfx?.marque || BRAND.profalux });
    pfx = null;
    return ok();
  },
  async pfxForget() { pfx = null; return ok(); },

  /* Somfy RTS */
  async somfyCreate({ id, address }: { id: string; address: string }) {
    await wait(400);
    if (find(id)) throw 'exists';
    newDevice({ id, virt: true, proto: PROTOCOL.somfy, marque: BRAND.somfy, virt_serial: address, freq_khz: 433420, frame_bits: 56 });
    return ok();
  },

  /* Centrale, calibration */
  async saveCentral({ id, members }: { id: string; members: string[] }) {
    await wait(300);
    const d = find(id);
    if (d && !d.central) throw 'exists';
    if (d) d.members = members.join(',');
    else newDevice({ id, central: true, members: members.join(','), position: undefined });
    return ok();
  },
  async calibrate({ id, travel_up_ms, travel_down_ms }: { id: string; travel_up_ms: number; travel_down_ms: number }) {
    Object.assign(mustFind(id), { travel_up_ms, travel_down_ms });
    return ok();
  },

  /* Telecommandes captees */
  async remoteRename(serial: string, name: string) { remotes[serial] = name; return ok(); },
  async remoteDelete(serial: string) { delete remotes[serial]; return ok(); },
  async remoteIgnore(serial: string) {
    delete remotes[serial];
    if (!ignoredRemotes.includes(serial)) ignoredRemotes.push(serial);
    return ok();
  },
  async remoteUnignore(serial: string) { ignoredRemotes = ignoredRemotes.filter(s => s !== serial); return ok(); },
  /* Creer un volet depuis les dernieres trames d'une telecommande dans le journal RF : ici, montee et
     descente sont dans le journal, pas stop. */
  async deviceFromRemote(serial: string, name: string) {
    await wait(400);
    if (find(name)) throw 'exists';
    newDevice({ id: name, marque: BRAND.profalux, serials: [serial], cmd: { up: { b: 2, s: serial }, down: { b: 8, s: serial } } });
    return { learned: 'up,down' };
  },
} satisfies typeof realApi;
