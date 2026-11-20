// Fausses donnees des onglets Reseau, OTA, Systeme et Support : les memes champs que /api/config, /api/radios,
// /api/health, /api/diag, /api/ota/status, /api/rf et /api/log du firmware. Les actions longues (verification
// GitHub, mise a jour, calibration du gain, captures) reussissent apres un delai.
import { BAND, CHECK_PHASE, FAILURE, FLAG, GRANT_STATE, NO_SERIAL, RADIO_PROFILE, SCAN_STATE, type CheckPhase, type DownloadKey } from '../domain';
import type realSystemApi from './real-system';
import type { Progress } from './real-system';
import type {
  CaptureLast, CloudStatus, Config, ConfigReply, ConfigUpdate, Dataset, DatasetFrame, Health, OkReply, OtaStatus, RadioDetail,
  ReceptionTest, RfFrame, UndoInfo,
} from './types';
import { WRITABLE_CONFIG_FLAGS } from './types';

const wait = (ms: number): Promise<void> => new Promise(resolve => setTimeout(resolve, ms));
const startedAt = Math.floor(Date.now() / 1000);

const config: Config = {
  wifi_ssid: 'MaBox', device: '', mdns_default: 'openprofalux', mdns_host: 'openprofalux',
  mqtt_uri: 'mqtt://192.168.1.10', mqtt_user: 'ha', mqtt_pass_len: 12, mqtt_ca_len: 0, mqtt_cert_len: 0, mqtt_key_len: 0,
  mqtt_obey: FLAG.on, log_frames: FLAG.on, debug: FLAG.off, ui_auth: FLAG.on, update_auto: FLAG.on,
};
const radios: RadioDetail[] = [
  { idx: 0, band: BAND.mhz868, band_low_khz: 863000, band_high_khz: 870000, partnum: 0, version: 20, present: true, primary: true,
    radio_profile: RADIO_PROFILE.profalux, freq_khz: 868425, freq_custom_khz: 0, rx_gain: 39, profiles: [RADIO_PROFILE.profalux, RADIO_PROFILE.eveno, RADIO_PROFILE.custom] },
  { idx: 1, band: BAND.mhz433, band_low_khz: 433050, band_high_khz: 434790, partnum: 0, version: 20, present: true, primary: false,
    radio_profile: RADIO_PROFILE.custom, freq_khz: 433920, freq_custom_khz: 433920, rx_gain: 39, profiles: [RADIO_PROFILE.custom] },
];
/* Frequence de chaque profil de reception (celle du firmware), et marqueur d'image refusee (nom « refus »). */
const PROFILE_FREQ_KHZ: Record<string, number> = { [RADIO_PROFILE.profalux]: 868425, [RADIO_PROFILE.eveno]: 868330 };
const ota: OtaStatus = {
  version: '0.2.15', part: 'ota_0', rb_auto: 1, img: 'valide', rb_possible: FLAG.on, precedente: '0.2.14',
  latest: '', latest_url: '', checks: 0, state: 0, written: 0, total: 0, msg: '',
};
let rejectNextImage = false;
const SERIALS: [string, string][] = [['0x0A1B2C3', '2'], ['0x0A1B2D7', '8'], ['0x0B44A21', '4'], ['0x0A1B2E0', '2'], ['0x0D12E77', '2'], ['0x0B44A30', '8']];
const frames: RfFrame[] = Array.from({ length: 64 }, (_, i) => {
  const [serial, button] = SERIALS[(i * 7) % SERIALS.length] ?? [NO_SERIAL, '0'];
  return { t: startedAt - i * 431 - 12, serial, button, hop: (0x3A5F12C0 + i * 0x1F3D7).toString(16).toUpperCase().slice(-8), rssi: -52 - ((i * 13) % 40) };
});
let lastCapture: CaptureLast | null = null;
let undo: UndoInfo = { available: true, t: startedAt - 86400 * 2 };
const LOG = [
  'I (812) main: OpenProfalux 0.2.15 (carte atom-lite)',
  'I (845) cc1101: radio 0 (868) : PARTNUM 0x00 VERSION 0x14, 868,425 MHz',
  'I (861) cc1101: radio 1 (433) : PARTNUM 0x00 VERSION 0x14, 433,920 MHz',
  'I (1290) wifi: connecte a « MaBox », IP 192.168.1.50, RSSI -54 dBm',
  'I (1302) mdns: openprofalux.local',
  'I (1544) mqtt: connecte a mqtt://192.168.1.10, 7 entites publiees',
  'I (1550) ota: retour automatique actif, image validee',
  'I (60412) rf: 0A1B2C3 bouton 0x2 RSSI -58 dBm -> Salon (montee)',
  'I (61420) shutters: Salon : position 100 %',
  'W (95120) rf: 0D12E77 inconnue, enregistree sans nom (7/16)',
  'I (180034) ota: verification GitHub : 0.2.15 = derniere version',
];

const copy = <T>(o: T): T => JSON.parse(JSON.stringify(o));
const ok = (): OkReply => ({ ok: 1 });
const SECRET_FIELDS = ['mqtt_pass', 'mqtt_ca', 'mqtt_cert', 'mqtt_key'] as const;

const NOT_PAIRED: CloudStatus = { paired: false, enabled: false, connected: false, name: '', device_id: '' };
let cloud: CloudStatus = { ...NOT_PAIRED };
let grant: { start: number } | null = null;   // appairage par QR en cours
const MOCK_GRANT_CODE = 'QKDW-7HPM';
const MOCK_GRANT_URI = `https://app.isno.fr/activer?code=${MOCK_GRANT_CODE}`;
const noDownloads = (): Record<DownloadKey, string> | null => null;

export default {
  /* Pas de fichier a telecharger chez le boitier : les ecrans fabriquent les fichiers eux-memes. */
  downloadLinks: noDownloads(),

  /* ── Reglages ── */
  async config(): Promise<Config> {
    const primary = radios[0];
    return copy({ ...config, radio_profile: primary?.radio_profile, freq_khz: primary?.freq_khz, rx_gain: primary?.rx_gain });
  },
  async saveConfig(body: ConfigUpdate): Promise<ConfigReply> {
    await wait(400);
    const { radio: n, ...rest } = body;
    /* Radio primaire : champs a plat ; secondaire : radio: N (comme le firmware). */
    const index = n != null ? n : rest.radio_profile !== undefined || rest.freq_khz !== undefined || rest.rx_gain !== undefined ? 0 : null;
    const r = index != null ? radios[index] : undefined;
    if (r) {
      if (rest.radio_profile) r.radio_profile = rest.radio_profile;
      if (rest.freq_khz) {
        if (rest.freq_khz < r.band_low_khz || rest.freq_khz > r.band_high_khz) throw 400;
        r.freq_custom_khz = rest.freq_khz;
      }
      if (rest.rx_gain) r.rx_gain = rest.rx_gain;
      r.freq_khz = PROFILE_FREQ_KHZ[r.radio_profile] ?? r.freq_custom_khz;
      return { ok: 1, radio_applied: 1 };
    }
    /* Un certificat envoye vide est efface, comme dans le firmware. */
    for (const field of SECRET_FIELDS) {
      const value = rest[field];
      if (value !== undefined) { config[`${field}_len`] = value.length; delete rest[field]; }
    }
    /* Drapeaux ecrits en booleen, relus en 0 / 1 (comme le firmware). */
    for (const field of WRITABLE_CONFIG_FLAGS) {
      const value = rest[field];
      if (value !== undefined) { config[field] = value ? FLAG.on : FLAG.off; delete rest[field]; }
    }
    if (rest.ui_pass !== undefined) { config.ui_auth = rest.ui_pass ? FLAG.on : FLAG.off; delete rest.ui_pass; }
    delete rest.wifi_pass; delete rest.reboot;
    Object.assign(config, rest);
    return ok();
  },
  async discoverMqtt() { await wait(1800); return { uri: 'mqtt://192.168.1.10:1883' }; },
  async radioDetails() { return { active: 0, radios: copy(radios) }; },
  async calibrateGain(radio: number, onGain: (gain: number | undefined) => void) {
    for (const g of [7, 39, 47]) { onGain(g); await wait(1200); }
    const r = radios[radio];
    if (r) r.rx_gain = 47;
    return { state: SCAN_STATE.done, result: 47 };
  },

  /* ── Cloud ISNO ── */
  async cloudStatus() { return { ...cloud }; },
  async cloudRequestGrant() {
    await wait(900);
    if (cloud.paired) return { ok: 0, err: 'paired' };
    grant = { start: Date.now() };
    return { ok: 1, uri: MOCK_GRANT_URI, user_code: MOCK_GRANT_CODE };
  },
  /* Confirme « sur le telephone » 6 s apres la demande. */
  async cloudPollGrant() {
    if (!grant) return { state: GRANT_STATE.idle };
    if (Date.now() - grant.start < 6000) return { state: GRANT_STATE.pending, uri: MOCK_GRANT_URI, user_code: MOCK_GRANT_CODE };
    grant = null;
    cloud = { paired: true, enabled: true, connected: true, name: 'Maison', device_id: 'opf-7f3a92c1' };
    return { state: GRANT_STATE.ready, device_id: cloud.device_id };
  },
  async cloudPair(code: string) {
    await wait(900);
    if (!code) return { ok: 0, err: 'code' };
    cloud = { paired: true, enabled: true, connected: true, name: '', device_id: 'opf-7f3a92c1' };
    return { ok: 1, device_id: cloud.device_id };
  },
  async cloudToggle(on: boolean) { cloud.enabled = on; cloud.connected = on; return ok(); },
  async cloudForget() { cloud = { ...NOT_PAIRED }; return ok(); },

  /* ── Etat et diagnostic ── */
  async health(): Promise<Health> {
    return { reset: 3, heap: 118 * 1024, heap_min: 74 * 1024, heap_blk: 56 * 1024, nvs_free: 214, nvs_avail: 200, nvs_total: 504, nvs_used: 290,
      nvs_ns: { shutters: 168, cfg: 61, 'nvs.net80211': 34, pfx_enrol: 19, phy: 8 }, volets: 7, volet_entries: 168,
      spiffs_used: 182 * 1024, spiffs_total: 896 * 1024, part: ota.part ?? '', version: ota.version,
      crash: { pc: '0x400d6f2a', cause: 28, bt: '0x400d6f2a 0x400d7b10 0x40091c3e 0x4008f6d1' }, net_reboots: 0 };
  },
  async diag() { return { partnum: 0, version: 20, tx_ok: 1, gdo0: 1, gdo0_lvl: 0, spi_retries: 0, spi_fails: 0 }; },
  async diagTx() { await wait(1500); return ok(); },
  async testReception(radio: number): Promise<ReceptionTest> {
    await wait(2600);
    return radio === 0 ? { serial: '0x0A1B2C3', button: '2', rssi: -58 } : Promise.reject(FAILURE.nothing);
  },
  async rawCapture(radio: number) {
    await wait(2600);
    const r = radios[radio];
    lastCapture = { n: 412, band: r?.band, freq_khz: r?.freq_khz, rssi: -61 };
    return { n: 412 };
  },
  async lastCapture(): Promise<CaptureLast> { return lastCapture ? copy(lastCapture) : { n: 0 }; },
  async lastCaptureText() { return JSON.stringify(lastCapture || { n: 0 }); },
  async replayCapture() { await wait(900); return ok(); },
  async multiCapture() { await wait(3000); lastCapture = { n: 388, band: BAND.mhz868, freq_khz: 868425, rssi: -60, nframes: 3 }; return { nframes: 3 }; },

  /* ── Trames RF, dataset, journal ── */
  async rf(offset: number, limit: number) { return { total: frames.length, frames: copy(frames.slice(offset, offset + limit)) }; },
  async replayFrame() { await wait(800); return ok(); },
  /* Comme /api/frames : par telecommande, le nombre de trames distinctes et les trames (hop, bouton, t). */
  async dataset(): Promise<Dataset> {
    const bySerial: Dataset['trames'] = {};
    for (const f of frames) {
      const entry = bySerial[f.serial] ?? (bySerial[f.serial] = { count: 0, frames: [] });
      const frame: DatasetFrame = { hop: f.hop ?? '', button: f.button, t: f.t ?? 0 };
      entry.count++; entry.frames.push(frame);
    }
    return { trames: bySerial };
  },
  async log() {
    const n = Math.floor(Date.now() / 1000) - startedAt;
    const extra = Array.from({ length: Math.min(6, Math.floor(n / 4)) }, (_, i) => `I (${200000 + i * 4000}) rf: 0A1B2C3 bouton 0x4 RSSI -${57 + i} dBm -> Salon (stop)`);
    return [...LOG, ...extra].join('\n') + '\n';
  },

  /* ── Mise a jour ── */
  async otaStatus() { return copy(ota); },
  async otaCheck(onPhase?: (phase: CheckPhase) => void) {
    if (onPhase) onPhase(CHECK_PHASE.github);
    await wait(2200);
    ota.checks += 1; ota.latest = 'v0.2.16'; ota.latest_url = 'https://github.com/.../openprofalux-atom-lite-ota.bin';
    return copy(ota);
  },
  async otaInstall(onProgress: Progress) {
    for (let p = 0; p <= 100; p += 5) { onProgress(p / 100, p < 100 ? `téléchargement ${Math.round(1480 * p / 100)} / 1480 Ko` : 'vérification…'); await wait(160); }
    await wait(800);
    ota.precedente = ota.version; ota.version = '0.2.16'; ota.latest = 'v0.2.16'; ota.img = 'essai';
    return { rebooting: true };
  },
  /* Une image dont le nom contient « refus » est refusee a la verification, comme par le boitier (etat 99). */
  async waitForReboot(): Promise<OtaStatus | null> {
    await wait(1500);
    if (rejectNextImage) { rejectNextImage = false; return Promise.reject('image refusée à la vérification (ESP_ERR_OTA_VALIDATE_FAILED)'); }
    return { ...ota, state: 0 };
  },
  async otaUpload(file: File, onProgress: Progress) {
    rejectNextImage = /refus/i.test(file.name || '');
    for (let p = 0; p <= 100; p += 4) { onProgress(p / 100, `envoi ${Math.round((file.size / 1024) * p / 100)} / ${Math.round(file.size / 1024)} Ko`); await wait(120); }
    return ok();
  },
  async otaRollback() {
    await wait(600);
    const previous = ota.precedente ?? ota.version;
    ota.precedente = ota.version; ota.version = previous;
    return ok();
  },

  /* ── Sauvegarde ── */
  async restore(content: string): Promise<OkReply> {
    await wait(600);
    try { const j: { version?: unknown } = JSON.parse(content); if (!j.version) throw 0; } catch { return { ok: 0, err: 'format' }; }
    undo = { available: true, t: Math.floor(Date.now() / 1000) };
    return ok();
  },
  async undoInfo() { return copy(undo); },
  async undoRestore() { await wait(500); return ok(); },

  /* ── Support ── */
  async report() { await wait(1500); return ok(); },
} satisfies typeof realSystemApi;
