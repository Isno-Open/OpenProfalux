// Les routes du firmware (web_ui.c) pour la session, le pilotage et les appareils. Memes appels et memes
// corps que l'interface d'avant (app.js) : le firmware n'a rien a changer.
import { json, post, exiger, sonder } from './http.js';

/* Les routes des telecommandes captees repondent par un texte quand elles refusent : on le garde pour le
   message (« Refuse : ... »). */
async function postTexte(url, corps) {
  const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(corps) });
  if (!r.ok) return Promise.reject(await r.text().catch(() => String(r.status)));
  return r.json().catch(() => ({}));
}

export default {
  session: () => json('/api/session'),
  connexion: pass => fetch('/api/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pass }) })
    .then(r => r.ok, () => false),
  deconnexion: () => fetch('/api/logout', { method: 'POST' }),
  statut: signal => json('/api/status', { signal }),
  profils: () => json('/profiles.json'),
  ordre: o => post('/api/shutter', o),
  ordreAffichage: ids => post('/api/volet/order', { ids }),

  /* ── Appareils ── */
  radios: () => json('/api/radios').then(d => d.radios || []),
  /* Detection : la radio ecoute (~15 s, la requete attend la fin), puis on lit ce qu'elle a entendu. */
  ecouter: async radio => { await post(`/api/capture/start?radio=${radio}`).catch(() => null); return json('/api/capture/last'); },
  /* Capture d'une commande : rejeu brut (CAME, code fixe) ou clone KeeLoq. Rejets : 'rien' (rien capte),
     { autre: serial } (une autre telecommande que celle du volet). */
  async capturer({ id, action, rejeu, type, radio, freq_khz, rx, nomTelec, existe, serialsConnus = [], virt }) {
    if (rejeu) {
      if (!existe) await exiger(post('/api/rejeu/create', { id, type })).catch(() => Promise.reject('rien'));
      const corps = { id, cmd: action, radio };
      if (freq_khz) corps.freq_khz = freq_khz;
      if (rx && typeof rx.datarate_baud === 'number' && typeof rx.bandwidth_khz === 'number') {
        corps.datarate_baud = rx.datarate_baud;
        corps.bandwidth_khz = rx.bandwidth_khz;
      }
      const d = await post('/api/rejeu/capture', corps).catch(() => null);
      return d && d.n > 0 ? d : Promise.reject('rien');
    }
    await post(`/api/learn/start?radio=${radio}${freq_khz ? `&freq_khz=${freq_khz}` : ''}`, { action });
    const r = await sonder('/api/learn/poll', d => !!d.bits, 40, 400).catch(e => Promise.reject(e === 401 ? 401 : 'rien'));
    /* Un volet clone rejoue les trames d'UNE telecommande ; un volet virtuel en suit plusieurs. */
    if (!virt && serialsConnus.length && !serialsConnus.includes(r.serial)) return Promise.reject({ autre: r.serial });
    if (nomTelec) await post('/api/remote', { serial: r.serial, name: nomTelec });
    await post('/api/learn/assign', { id, action, bits: r.bits });
    return r;
  },
  deplacerCapture: ({ id, de, vers }) => post('/api/learn/reassign', { id, from: de, to: vers }),
  /* « Caler sur ma telecommande » : balayage autour de la porteuse ; rend { status, center_khz, from_khz,
     peak_dbm, floor_dbm }. status 0 = trouve, -2 = trop faible, -3 = hors de la plage balayee. */
  async caler(radio) {
    const r = await post(`/api/rx/sweep?radio=${radio}`).catch(() => null);
    if (r && r.ok === 0) return Promise.reject('lancement');
    return sonder(`/api/rx/sweep?radio=${radio}`, x => x.state === 2);
  },
  emission: corps => exiger(post('/api/volet/txparams', corps)),
  renommer: (id, nom) => exiger(post('/api/volet/rename', { id, name: nom })),
  changerType: (id, type) => exiger(post('/api/volet/type', { id, type })),
  supprimer: id => post('/api/volet/delete', { id }),
  orientation: (id, valeur) => post('/api/volet/orientation', { id, orientation: valeur }),
  suiveurs: (id, membres) => exiger(post('/api/volet/followers', { id, members: membres })),

  /* Telecommande virtuelle (enrolement) */
  pfxEtat: () => json('/api/pfx'),
  async pfxCapturer(corps) {
    const r = await post('/api/pfx/capture', corps).catch(() => null);
    if (!r || !r.ok) return Promise.reject('demarrage');
    return sonder('/api/pfx/capture/poll', d => d.done, 20, 1000);
  },
  pfxRenouveler: () => post('/api/pfx/renew'),
  pfxApprendre: () => post('/api/pfx/learn'),
  pfxCmd: cmd => post('/api/pfx/cmd', { cmd }),
  pfxEnregistrer: id => exiger(post('/api/pfx/save_volet', { id })),
  pfxOublier: () => post('/api/pfx/forget'),

  somfyCreer: corps => exiger(post('/api/somfy/create', corps)),
  centrale: ({ id, members }) => exiger(post('/api/central', { id, members })),
  calibrer: corps => post('/api/calibrate', corps),

  /* Telecommandes captees */
  telecNommer: (serial, name) => post('/api/remote', { serial, name }),
  telecSupprimer: serial => postTexte('/api/remote/delete', { serial }),
  telecIgnorer: serial => postTexte('/api/remote/ignore', { serial }),
  telecRetablir: serial => post('/api/remote/unignore', { serial }),
  voletDepuisTelec: (serial, name) => postTexte('/api/remote/create_volet', { serial, name }),
};
