// Les routes du firmware (web_ui.c) pour Reseau (Wi-Fi, MQTT, Cloud), OTA, Systeme et Support. Memes
// appels et memes corps que l'interface d'avant (app.js).
import { CHECK_PHASE, DOWNLOAD, FAILURE, HTTP_UNAUTHORIZED, NO_SERIAL, OTA_STATE, SCAN_STATE, type CheckPhase, type DownloadKey } from '../domain';
import { json, post, wait, poll } from './http';
import type {
  CaptureLast, CloudGrantPoll, CloudGrantReply, CloudPairReply, CloudStatus, Config, ConfigReply, ConfigUpdate, Dataset, Diag,
  GainCalibration, Health, MqttDiscovery, MultiCaptureReply, OkReply, OtaInstallReply, OtaStatus, RadiosReply, ReceptionTest,
  ReportBody, ReportReply, RfPage, UndoInfo,
} from './types';

/* Avancement d'une mise a jour : fraction (null = inconnue) et message. */
export type Progress = (fraction: number | null, text: string) => void;

/* Texte brut (journal), avec la meme gestion du 401 que json(). */
const text = (url: string): Promise<string> => fetch(url, { cache: 'no-store' })
  .then(r => (r.status === HTTP_UNAUTHORIZED ? json<string>(url) : r.ok ? r.text() : Promise.reject(r.status)));

/* Les fichiers que le boitier sert lui-meme en telechargement (lien direct, le cookie de session suit). */
const DOWNLOAD_LINKS: Record<DownloadKey, string> = { [DOWNLOAD.backup]: '/api/backup', [DOWNLOAD.frames]: '/api/frames', [DOWNLOAD.log]: '/api/log' };
/* Les fausses donnees n'ont pas de fichiers : elles mettent null, et l'ecran fabrique le fichier. */
const downloadLinks = (): Record<DownloadKey, string> | null => DOWNLOAD_LINKS;
const asReply = (d: OkReply): OkReply => d;

export default {
  downloadLinks: downloadLinks(),

  config: () => json<Config>('/api/config'),
  /* Radio primaire : champs a plat (radio_profile, freq_khz, rx_gain) ; secondaire : { radio: N, ... }.
     400 = refuse (frequence hors de la plage de la radio, certificats trop volumineux...). */
  saveConfig: (body: ConfigUpdate) => post<ConfigReply>('/api/config', body),
  discoverMqtt: () => json<MqttDiscovery>('/api/mqtt/discover'),
  radioDetails: () => json<RadiosReply>('/api/radios'),
  /* Calibration du gain : le boitier essaie chaque gain pendant qu'on presse la telecommande. */
  async calibrateGain(radio: number, onGain: (gain: number | undefined) => void): Promise<GainCalibration> {
    await post(`/api/rx/calibrate?radio=${radio}`).catch(() => null);
    const s = await poll<GainCalibration>('/api/rx/calibrate', d => d.state !== SCAN_STATE.running, 40, 1000,
      d => { if (d.state === SCAN_STATE.running) onGain(d.testing); });
    return s.state === SCAN_STATE.done ? s : Promise.reject(FAILURE.nothing);
  },

  /* ── Cloud ISNO (app.isno.fr) ── */
  cloudStatus: () => json<CloudStatus>('/api/cloud/status'),
  cloudRequestGrant: (server: string) => post<CloudGrantReply>('/api/cloud/grant', { server }),
  cloudPollGrant: () => json<CloudGrantPoll>('/api/cloud/grant'),
  cloudPair: (code: string, server: string) => post<CloudPairReply>('/api/cloud/pair', { code, server }),
  cloudToggle: (on: boolean) => post('/api/cloud/toggle', { on }),
  cloudForget: () => post('/api/cloud/forget'),

  /* ── Etat et diagnostic ── */
  health: () => json<Health>('/api/health'),
  diag: () => json<Diag>('/api/diag'),
  diagTx: () => post('/api/diag/tx'),
  async testReception(radio: number): Promise<ReceptionTest> {
    await post(`/api/learn/start?radio=${radio}`).catch(() => null);
    return poll<ReceptionTest>('/api/learn/poll', d => !!d.serial && d.serial !== NO_SERIAL, 16)
      .catch(e => Promise.reject(e === HTTP_UNAUTHORIZED ? HTTP_UNAUTHORIZED : FAILURE.nothing));
  },
  rawCapture: (radio: number) => post<{ n: number }>(`/api/capture/start?radio=${radio}`),
  lastCapture: () => json<CaptureLast>('/api/capture/last'),
  lastCaptureText: () => text('/api/capture/last'),
  replayCapture: () => post('/api/capture/replay'),
  multiCapture: (radio: number) => post<MultiCaptureReply>(`/api/capture/multi?radio=${radio}`),

  /* ── Trames RF, dataset, journal ── */
  rf: (offset: number, limit: number) => json<RfPage>(`/api/rf?offset=${offset}&limit=${limit}`),
  replayFrame: (serial: string, hop: string) => post('/api/rf/replay', { serial, hop }),
  dataset: () => json<Dataset>('/api/frames'),
  log: () => text('/api/log'),

  /* ── Mise a jour ── */
  otaStatus: () => json<OtaStatus>('/api/ota/status'),
  /* C'est le BOITIER qui interroge GitHub. Il dit ou il en est (checking, check_waiting) : juste apres une
     connexion du cloud, il attend jusqu'a quelques minutes. Seul son silence, cinq fois de suite, arrete
     l'attente. Rejets : FAILURE.silent (le boitier ne repond pas), FAILURE.github (fini sans resultat). */
  async otaCheck(onPhase?: (phase: CheckPhase) => void): Promise<OtaStatus> {
    const r = await post<OkReply & { checks?: number }>('/api/ota/check').catch(() => null);
    if (!r || !r.ok) return Promise.reject(FAILURE.silent);
    let silences = 0;
    while (silences < 5) {
      await wait(1000);
      const s = await json<OtaStatus>('/api/ota/status').catch(() => null);
      if (!s) { silences++; continue; }
      silences = 0;
      if (s.checks > (r.checks ?? 0)) return s;
      if (!s.checking) return Promise.reject(FAILURE.github);
      if (onPhase) onPhase(s.check_waiting ? CHECK_PHASE.waiting : CHECK_PHASE.github);
    }
    return Promise.reject(FAILURE.silent);
  },
  /* Le boitier telecharge et flashe ; on suit /api/ota/status. Injoignable trois fois de suite, ou etat
     OTA_STATE.rebooting : il redemarre. Etat OTA_STATE.failed : echec. */
  async otaInstall(onProgress: Progress, url: string): Promise<OtaInstallReply> {
    post('/api/ota/pull', { url }).catch(() => {});
    let missed = 0;
    for (;;) {
      await wait(800);
      const s = await json<OtaStatus>('/api/ota/status').catch(() => null);
      if (!s) { if (++missed >= 3) return { rebooting: true }; continue; }
      missed = 0;
      if (s.total) onProgress(s.written / s.total, s.msg || '');
      else if (s.msg) onProgress(null, s.msg);
      if (s.state === OTA_STATE.failed) return Promise.reject(s.msg || 'voir le journal');
      if (s.state === OTA_STATE.rebooting) return { rebooting: true };
    }
  },
  /* Le serveur du boitier ne repond pas pendant l'envoi : l'avancement vient du navigateur (XHR). */
  otaUpload: (file: File, onProgress: Progress) => new Promise<OkReply>((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/api/ota/upload');
    x.upload.onprogress = e => { if (e.lengthComputable) onProgress(e.loaded / e.total, `envoi ${Math.round(e.loaded / 1024)} / ${Math.round(e.total / 1024)} Ko`); };
    x.onload = () => (x.status >= 200 && x.status < 300 ? resolve({ ok: 1 }) : reject(`HTTP ${x.status}`));
    x.onerror = () => reject('lien coupé ?');
    file.arrayBuffer().then(b => x.send(b));
  }),
  otaRollback: () => post('/api/ota/rollback'),
  /* Attendre la fin d'un redemarrage : que le boitier tombe, puis qu'il reponde de nouveau (45 s au plus). Tant
     qu'il repond sans etre tombe, il verifie encore l'image recue, ou va redemarrer (3 s apres l'etat
     OTA_STATE.rebooting) : une reponse de ce moment-la ne prouve rien. Image refusee a la verification
     (OTA_STATE.failed) : il garde sa version et ne redemarre pas, rejet avec la raison. Rend l'etat d'apres le
     redemarrage (version), ou null. */
  async waitForReboot(): Promise<OtaStatus | null> {
    let down = false;
    for (let i = 0; i < 45; i++) {
      await wait(1000);
      const s = await json<OtaStatus>('/api/ota/status').catch(() => null);
      if (!s) { down = true; continue; }
      if (down) return s;
      if (s.state === OTA_STATE.failed) return Promise.reject(`image refusée à la vérification (${s.msg || 'voir le journal'})`);
    }
    return null;
  },

  /* ── Sauvegarde ── */
  restore: (content: string) => fetch('/api/restore', { method: 'POST', body: content }).then(r => r.json()).then(asReply).catch((): OkReply => ({})),
  undoInfo: () => json<UndoInfo>('/api/restore/undo'),
  undoRestore: () => fetch('/api/restore/undo', { method: 'POST' }).then(r => r.json()).then(asReply).catch((): OkReply => ({})),

  /* ── Support ── */
  report: (body: ReportBody) => post<ReportReply>('/api/report', body),
};
