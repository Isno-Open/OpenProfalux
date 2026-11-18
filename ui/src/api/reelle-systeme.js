// Les routes du firmware (web_ui.c) pour Reseau (Wi-Fi, MQTT, Cloud), OTA, Systeme et Support. Memes
// appels et memes corps que l'interface d'avant (app.js).
import { json, post, attendre, sonder } from './http.js';

/* Texte brut (journal), avec la meme gestion du 401 que json(). */
const texte = url => fetch(url, { cache: 'no-store' }).then(r => (r.status === 401 ? json(url) : r.ok ? r.text() : Promise.reject(r.status)));

export default {
  /* Les fichiers que le boitier sert lui-meme en telechargement (lien direct, le cookie de session suit). */
  liens: { sauvegarde: '/api/backup', trames: '/api/frames', journal: '/api/log' },

  config: () => json('/api/config'),
  /* Radio primaire : champs a plat (radio_profile, freq_khz, rx_gain) ; secondaire : { radio: N, ... }.
     400 = refuse (frequence hors de la plage de la radio, certificats trop volumineux...). */
  enregistrerConfig: corps => post('/api/config', corps),
  mqttDecouvrir: () => json('/api/mqtt/discover'),
  radiosDetail: () => json('/api/radios'),
  /* Calibration du gain : le boitier essaie chaque gain pendant qu'on presse la telecommande. */
  async calibrerGain(radio, suivi) {
    await post(`/api/rx/calibrate?radio=${radio}`).catch(() => null);
    const s = await sonder('/api/rx/calibrate', d => d.state !== 1, 40, 1000, d => d.state === 1 && suivi(d.testing));
    return s.state === 2 ? s : Promise.reject('rien');
  },

  /* ── Cloud ISNO (app.isno.fr) ── */
  cloudEtat: () => json('/api/cloud/status'),
  cloudDemander: server => post('/api/cloud/grant', { server }),
  cloudSuivi: () => json('/api/cloud/grant'),
  cloudAppairer: (code, server) => post('/api/cloud/pair', { code, server }),
  cloudBasculer: on => post('/api/cloud/toggle', { on }),
  cloudOublier: () => post('/api/cloud/forget'),

  /* ── Etat et diagnostic ── */
  sante: () => json('/api/health'),
  diag: () => json('/api/diag'),
  diagTx: () => post('/api/diag/tx'),
  async testerReception(radio) {
    await post(`/api/learn/start?radio=${radio}`).catch(() => null);
    return sonder('/api/learn/poll', d => d.serial && d.serial !== '0x0000000', 16).catch(e => Promise.reject(e === 401 ? 401 : 'rien'));
  },
  captureBrute: radio => post(`/api/capture/start?radio=${radio}`),
  derniereCapture: () => json('/api/capture/last'),
  derniereCaptureTexte: () => texte('/api/capture/last'),
  rejouerCapture: () => post('/api/capture/replay'),
  captureMulti: radio => post(`/api/capture/multi?radio=${radio}`),

  /* ── Trames RF, dataset, journal ── */
  rf: (offset, limit) => json(`/api/rf?offset=${offset}&limit=${limit}`),
  rejouerTrame: (serial, hop) => post('/api/rf/replay', { serial, hop }),
  dataset: () => json('/api/frames'),
  journal: () => texte('/api/log'),

  /* ── Mise a jour ── */
  otaEtat: () => json('/api/ota/status'),
  /* C'est le BOITIER qui interroge GitHub. Il dit ou il en est (checking, check_waiting) : juste apres une
     connexion du cloud, il attend jusqu'a quelques minutes. Seul son silence, cinq fois de suite, arrete
     l'attente. Rejets : 'muet' (le boitier ne repond pas), 'github' (fini sans resultat). */
  async otaVerifier(suivi) {
    const r = await post('/api/ota/check').catch(() => null);
    if (!r || !r.ok) return Promise.reject('muet');
    let silences = 0;
    while (silences < 5) {
      await attendre(1000);
      const s = await json('/api/ota/status').catch(() => null);
      if (!s) { silences++; continue; }
      silences = 0;
      if (s.checks > r.checks) return s;
      if (!s.checking) return Promise.reject('github');
      if (suivi) suivi(s.check_waiting ? 'attente' : 'github');
    }
    return Promise.reject('muet');
  },
  /* Le boitier telecharge et flashe ; on suit /api/ota/status. Injoignable trois fois de suite, ou etat 4
     (REBOOTING) : il redemarre. Etat 99 : echec. */
  async otaInstaller(suivi, url) {
    post('/api/ota/pull', { url }).catch(() => {});
    let rate = 0;
    for (;;) {
      await attendre(800);
      const s = await json('/api/ota/status').catch(() => null);
      if (!s) { if (++rate >= 3) return { redemarre: true }; continue; }
      rate = 0;
      if (s.total) suivi(s.written / s.total, s.msg || '');
      else if (s.msg) suivi(null, s.msg);
      if (s.state === 99) return Promise.reject(s.msg || 'voir le journal');
      if (s.state === 4) return { redemarre: true };
    }
  },
  /* Le serveur du boitier ne repond pas pendant l'envoi : l'avancement vient du navigateur (XHR). */
  otaEnvoyer: (fichier, suivi) => new Promise((ok, ko) => {
    const x = new XMLHttpRequest();
    x.open('POST', '/api/ota/upload');
    x.upload.onprogress = e => e.lengthComputable && suivi(e.loaded / e.total, `envoi ${Math.round(e.loaded / 1024)} / ${Math.round(e.total / 1024)} Ko`);
    x.onload = () => (x.status >= 200 && x.status < 300 ? ok({ ok: 1 }) : ko(`HTTP ${x.status}`));
    x.onerror = () => ko('lien coupé ?');
    fichier.arrayBuffer().then(b => x.send(b));
  }),
  otaRevenir: () => post('/api/ota/rollback'),
  /* Attendre la fin d'un redemarrage : que le boitier tombe, puis qu'il reponde de nouveau (45 s au plus). Tant
     qu'il repond sans etre tombe, il verifie encore l'image recue, ou va redemarrer (3 s apres l'etat 4) : une
     reponse de ce moment-la ne prouve rien. Image refusee a la verification (etat 99) : il garde sa version et
     ne redemarre pas, rejet avec la raison. Rend l'etat d'apres le redemarrage (version), ou null. */
  async attendreRetour() {
    let tombe = false;
    for (let i = 0; i < 45; i++) {
      await attendre(1000);
      const s = await json('/api/ota/status').catch(() => null);
      if (!s) { tombe = true; continue; }
      if (tombe) return s;
      if (s.state === 99) return Promise.reject(`image refusée à la vérification (${s.msg || 'voir le journal'})`);
    }
    return null;
  },

  /* ── Sauvegarde ── */
  restaurer: contenu => fetch('/api/restore', { method: 'POST', body: contenu }).then(r => r.json()).catch(() => ({})),
  avantRestauration: () => json('/api/restore/undo'),
  revenirAvantRestauration: () => fetch('/api/restore/undo', { method: 'POST' }).then(r => r.json()).catch(() => ({})),

  /* ── Support ── */
  rapport: corps => post('/api/report', corps),
};
