// Fausses donnees des onglets Reseau, OTA, Systeme et Support : les memes champs que /api/config,
// /api/radios, /api/health, /api/diag, /api/ota/status, /api/rf et /api/log du firmware. Les actions
// longues (verification GitHub, mise a jour, calibration du gain, captures) reussissent apres un delai.
const attendre = ms => new Promise(r => setTimeout(r, ms));
const t0 = Math.floor(Date.now() / 1000);

const config = {
  wifi_ssid: 'MaBox', device: '', mdns_default: 'openprofalux', mdns_host: 'openprofalux',
  mqtt_uri: 'mqtt://192.168.1.10', mqtt_user: 'ha', mqtt_pass_len: 12, mqtt_ca_len: 0, mqtt_cert_len: 0, mqtt_key_len: 0,
  mqtt_obey: true, log_frames: true, debug: false, ui_auth: true, update_auto: true,
};
const radios = [
  { idx: 0, band: '868', band_low_khz: 863000, band_high_khz: 870000, partnum: 0, version: 20, present: true, primary: true,
    radio_profile: 'profalux', freq_khz: 868425, freq_custom_khz: 0, rx_gain: 39, profiles: ['profalux', 'eveno', 'custom'] },
  { idx: 1, band: '433', band_low_khz: 433050, band_high_khz: 434790, partnum: 0, version: 20, present: true, primary: false,
    radio_profile: 'custom', freq_khz: 433920, freq_custom_khz: 433920, rx_gain: 39, profiles: ['custom'] },
];
const ota = {
  version: '0.2.15', part: 'ota_0', rb_auto: 1, img: 'valide', rb_possible: true, precedente: '0.2.14',
  latest: '', latest_url: '', checks: 0, state: 0, written: 0, total: 0, msg: '',
};
const SERIALS = [['0x0A1B2C3', '2'], ['0x0A1B2D7', '8'], ['0x0B44A21', '4'], ['0x0A1B2E0', '2'], ['0x0D12E77', '2'], ['0x0B44A30', '8']];
const trames = Array.from({ length: 64 }, (_, i) => {
  const [serial, button] = SERIALS[(i * 7) % SERIALS.length];
  return { t: t0 - i * 431 - 12, serial, button, hop: (0x3A5F12C0 + i * 0x1F3D7).toString(16).toUpperCase().slice(-8), rssi: -52 - ((i * 13) % 40) };
});
let derniereCapture = null;
let avant = { available: true, t: t0 - 86400 * 2 };
const JOURNAL = [
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

const copie = o => JSON.parse(JSON.stringify(o));
const ok = (extra = {}) => ({ ok: 1, ...extra });

let cloud = { paired: false, enabled: false, connected: false, name: '', device_id: '' };
let demande = null;   // appairage par QR en cours

export default {
  /* Pas de fichier a telecharger chez le boitier : les ecrans fabriquent les fichiers eux-memes. */
  liens: null,

  /* ── Reglages ── */
  async config() { return copie({ ...config, radio_profile: radios[0].radio_profile, freq_khz: radios[0].freq_khz, rx_gain: radios[0].rx_gain }); },
  async enregistrerConfig(corps) {
    await attendre(400);
    const { radio: n, ...reste } = corps;
    /* Radio primaire : champs a plat ; secondaire : radio: N (comme le firmware). */
    const radio = n != null ? n : ['radio_profile', 'freq_khz', 'rx_gain'].some(k => k in reste) ? 0 : null;
    if (radio != null) {
      const r = radios[radio];
      if (reste.radio_profile) r.radio_profile = reste.radio_profile;
      if (reste.freq_khz) {
        if (reste.freq_khz < r.band_low_khz || reste.freq_khz > r.band_high_khz) throw 400;
        r.freq_custom_khz = reste.freq_khz;
      }
      if (reste.rx_gain) r.rx_gain = reste.rx_gain;
      r.freq_khz = r.radio_profile === 'profalux' ? 868425 : r.radio_profile === 'eveno' ? 868330 : r.freq_custom_khz;
      return ok({ radio_applied: 1 });
    }
    /* Un certificat envoye vide est efface, comme dans le firmware. */
    for (const k of ['mqtt_pass', 'mqtt_ca', 'mqtt_cert', 'mqtt_key']) {
      if (k in reste) { config[`${k}_len`] = reste[k].length; delete reste[k]; }
    }
    if ('ui_pass' in reste) { config.ui_auth = !!reste.ui_pass; delete reste.ui_pass; }
    delete reste.wifi_pass; delete reste.reboot;
    Object.assign(config, reste);
    return ok();
  },
  async mqttDecouvrir() { await attendre(1800); return { uri: 'mqtt://192.168.1.10:1883' }; },
  async radiosDetail() { return { active: 0, radios: copie(radios) }; },
  async calibrerGain(radio, suivi) {
    for (const g of [7, 39, 47]) { suivi(g); await attendre(1200); }
    radios[radio].rx_gain = 47;
    return { state: 2, result: 47 };
  },

  /* ── Cloud ISNO ── */
  async cloudEtat() { return { ...cloud }; },
  async cloudDemander() {
    await attendre(900);
    if (cloud.paired) return { ok: 0, err: 'paired' };
    demande = { debut: Date.now() };
    return { ok: 1, uri: 'https://app.isno.fr/activer?code=QKDW-7HPM', user_code: 'QKDW-7HPM' };
  },
  /* Confirme « sur le telephone » 6 s apres la demande. */
  async cloudSuivi() {
    if (!demande) return { state: 'error' };
    if (Date.now() - demande.debut < 6000) return { state: 'pending' };
    demande = null;
    cloud = { paired: true, enabled: true, connected: true, name: 'Maison', device_id: 'opf-7f3a92c1' };
    return { state: 'ready', device_id: cloud.device_id };
  },
  async cloudAppairer(code) {
    await attendre(900);
    if (!code) return { ok: 0, err: 'code' };
    cloud = { paired: true, enabled: true, connected: true, name: '', device_id: 'opf-7f3a92c1' };
    return { ok: 1, device_id: cloud.device_id };
  },
  async cloudBasculer(on) { cloud.enabled = on; cloud.connected = on; return ok(); },
  async cloudOublier() { cloud = { paired: false, enabled: false, connected: false, name: '', device_id: '' }; return ok(); },

  /* ── Etat et diagnostic ── */
  async sante() {
    return { reset: 3, heap: 118 * 1024, heap_min: 74 * 1024, heap_blk: 56 * 1024, nvs_free: 214, nvs_avail: 200, nvs_total: 504, nvs_used: 290,
      nvs_ns: { shutters: 168, cfg: 61, 'nvs.net80211': 34, pfx_enrol: 19, phy: 8 }, volets: 7, volet_entries: 168,
      spiffs_used: 182 * 1024, spiffs_total: 896 * 1024, part: ota.part, version: ota.version,
      crash: { pc: '0x400d6f2a', cause: 28, bt: '0x400d6f2a 0x400d7b10 0x40091c3e 0x4008f6d1' }, net_reboots: 0 };
  },
  async diag() { return { partnum: 0, version: 20, tx_ok: 1, gdo0: 1, gdo0_lvl: 0, spi_retries: 0, spi_fails: 0 }; },
  async diagTx() { await attendre(1500); return ok(); },
  async testerReception(radio) {
    await attendre(2600);
    return radio === 0 ? { serial: '0x0A1B2C3', button: '2', rssi: -58 } : Promise.reject('rien');
  },
  async captureBrute(radio) {
    await attendre(2600);
    derniereCapture = { n: 412, band: radios[radio].band, freq_khz: radios[radio].freq_khz, rssi: -61 };
    return { n: 412 };
  },
  async derniereCapture() { return derniereCapture ? copie(derniereCapture) : { n: 0 }; },
  async derniereCaptureTexte() { return JSON.stringify(derniereCapture || { n: 0 }); },
  async rejouerCapture() { await attendre(900); return ok(); },
  async captureMulti() { await attendre(3000); derniereCapture = { n: 388, band: '868', freq_khz: 868425, rssi: -60, nframes: 3 }; return { nframes: 3 }; },

  /* ── Trames RF, dataset, journal ── */
  async rf(offset, limit) { return { total: trames.length, frames: copie(trames.slice(offset, offset + limit)) }; },
  async rejouerTrame() { await attendre(800); return ok(); },
  /* Comme /api/frames : par telecommande, le nombre de trames distinctes et les trames (hop, bouton, t). */
  async dataset() {
    const t = {};
    for (const f of trames) {
      const e = t[f.serial] || (t[f.serial] = { count: 0, frames: [] });
      e.count++; e.frames.push({ hop: f.hop, button: f.button, t: f.t });
    }
    return { trames: t };
  },
  async journal() {
    const n = Math.floor(Date.now() / 1000) - t0;
    return [...JOURNAL, ...Array.from({ length: Math.min(6, Math.floor(n / 4)) }, (_, i) => `I (${200000 + i * 4000}) rf: 0A1B2C3 bouton 0x4 RSSI -${57 + i} dBm -> Salon (stop)`)].join('\n') + '\n';
  },

  /* ── Mise a jour ── */
  async otaEtat() { return copie(ota); },
  async otaVerifier(suivi) {
    if (suivi) suivi('github');
    await attendre(2200);
    ota.checks += 1; ota.latest = 'v0.2.16'; ota.latest_url = 'https://github.com/.../openprofalux-atom-lite-ota.bin';
    return copie(ota);
  },
  async otaInstaller(suivi) {
    for (let p = 0; p <= 100; p += 5) { suivi(p / 100, p < 100 ? `téléchargement ${Math.round(1480 * p / 100)} / 1480 Ko` : 'vérification…'); await attendre(160); }
    await attendre(800);
    ota.precedente = ota.version; ota.version = '0.2.16'; ota.latest = 'v0.2.16'; ota.img = 'essai';
    return { redemarre: true };
  },
  /* Une image dont le nom contient « refus » est refusee a la verification, comme par le boitier (etat 99). */
  async attendreRetour() {
    await attendre(1500);
    if (ota.refus) { ota.refus = false; return Promise.reject('image refusée à la vérification (ESP_ERR_OTA_VALIDATE_FAILED)'); }
    return { ...ota, state: 0 };
  },
  async otaEnvoyer(fichier, suivi) {
    ota.refus = /refus/i.test(fichier.name || '');
    for (let p = 0; p <= 100; p += 4) { suivi(p / 100, `envoi ${Math.round((fichier.size / 1024) * p / 100)} / ${Math.round(fichier.size / 1024)} Ko`); await attendre(120); }
    return ok();
  },
  async otaRevenir() { await attendre(600); [ota.version, ota.precedente] = [ota.precedente, ota.version]; return ok(); },

  /* ── Sauvegarde ── */
  async restaurer(texte) {
    await attendre(600);
    try { const j = JSON.parse(texte); if (!j.version) throw 0; } catch { return { ok: 0, err: 'format' }; }
    avant = { available: true, t: Math.floor(Date.now() / 1000) };
    return ok();
  },
  async avantRestauration() { return copie(avant); },
  async revenirAvantRestauration() { await attendre(500); return ok(); },

  /* ── Support ── */
  async rapport() { await attendre(1500); return ok(); },
};
