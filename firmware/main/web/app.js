'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
/* Un 401 signifie que la session a expire (ou que le mot de passe vient de
   changer) : on réaffiche l'ecran de connexion au lieu de laisser l'UI echouer
   silencieusement en boucle. */
const api = (u, o) => fetch(u, o).then(r => {
  if (r.status === 401) { showLogin(); return Promise.reject(401); }
  return r.ok ? r.json().catch(() => ({})) : Promise.reject(r.status);
});
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

/* ── Session ──────────────────────────────────────────────────────────────── */
function showLogin() {
  const l = $('#login'); if (!l || !l.hidden) return;   /* deja affiche : ne pas reinitialiser la saisie */
  /* Garde : on ne bloque l'UI que si le firmware confirme une protection active.
     Un 401 residuel (requete partie avant un retrait de protection) ne doit pas
     laisser un ecran de login sur une interface devenue ouverte. */
  fetch('/api/session').then(r => r.json()).then(s => {
    if (!s.protected) { boot(); return; }
    l.hidden = false;
    const p = $('#login-pass'); if (p) { p.value = ''; setTimeout(() => p.focus(), 50); }
  }).catch(() => {
    l.hidden = false;
    const p = $('#login-pass'); if (p) { p.value = ''; setTimeout(() => p.focus(), 50); }
  });
}
function hideLogin() { const l = $('#login'); if (l) l.hidden = true; }

if ($('#login-form')) $('#login-form').onsubmit = async e => {
  e.preventDefault();
  const pass = $('#login-pass').value, btn = $('#login-btn'), err = $('#login-err');
  err.hidden = true; btn.disabled = true; btn.textContent = 'Connexion…';
  try {
    const r = await fetch('/api/login', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pass })
    });
    if (r.ok) { hideLogin(); boot(); }        /* session ouverte -> on demarre l'app */
    else { err.hidden = false; $('#login-pass').select(); }
  } catch (_) { err.hidden = false; }
  finally { btn.disabled = false; btn.textContent = 'Se connecter'; }
};

if ($('#logout')) $('#logout').onclick = async () => {
  await fetch('/api/logout', { method: 'POST' }).catch(() => {});
  location.reload();
};

/* ── Radio = ressource unique (mutex), TX synchrone ~1s. Pendant qu'une commande
 *    part, on desactive TOUS les boutons volet + on montre "envoi" pour eviter le
 *    matraquage : sinon on empile les emissions et on croit que ca ne marche pas.
 *    La classe est sur <body> -> elle survit aux re-render du polling /api/status. ── */
let radioBusy = false;
const setRadioBusy = on => { radioBusy = on; document.body.classList.toggle('radio-busy', on); };
async function sendCmd(body, activeBtn) {
  if (radioBusy) return;                       // radio occupee : on ignore le clic en trop
  setRadioBusy(true);
  if (activeBtn) activeBtn.classList.add('sending');
  try { await api('/api/shutter', { method: 'POST', body: JSON.stringify(body) }); }
  catch (e) { toast('Radio occupée, réessaie dans un instant'); }
  finally { if (activeBtn) activeBtn.classList.remove('sending'); setRadioBusy(false); }
}

/* ── Onglets + routes (hash de l'URL, ex #sys/wifi) + thème ── */
function activateMain(name) {
  const t = $(`.tab[data-t="${name}"]`); if (!t) return false;
  $$('.tab').forEach(x => x.classList.toggle('active', x === t));
  $$('.panel').forEach(p => p.classList.toggle('active', p.dataset.p === name));
  if (['sys', 'wifi', 'mqtt', 'ota', 'remotes'].includes(name)) loadConfig();   /* remotes : profil radio rappele aux captures */
  return true;
}
function activateSub(sec, name) {
  const found = $$('.subtab', sec).some(x => x.dataset.s === name);
  $$('.subtab', sec).forEach(x => x.classList.toggle('active', x.dataset.s === name));
  $$('.subpanel', sec).forEach(p => p.classList.toggle('active', p.dataset.sp === name));
  return found;
}
function applyRoute() {
  const [main, sub] = (location.hash || '#control').replace(/^#/, '').split('/');
  if (!activateMain(main)) { activateMain('control'); return; }
  const sec = $(`.panel[data-p="${main}"]`);
  if (sub && sec) activateSub(sec, sub);
  if (typeof loadStatus === 'function') loadStatus();   /* refresh immediat a chaque changement d'onglet (ex: RF debug) */
  if (sub === 'rf') { if (typeof loadRf === 'function') loadRf(true); if (typeof loadFrames === 'function') loadFrames(); }
  if (sub === 'calib' && !calibLive && typeof fillCalib === 'function') fillCalib();   /* affiche les temps enregistres */
  if (sub === 'enrol' && typeof loadPfx === 'function') loadPfx();
  if (sub === 'radio' && typeof loadDiag === 'function') loadDiag();
  if (sub === 'log' && typeof loadLog === 'function') loadLog(true);
  if (sub === 'backup' && typeof loadPrevCfg === 'function') loadPrevCfg();
  if (sub === 'health' && typeof loadHealth === 'function') loadHealth();
}
/* Rafraichit la 1re page quand l'onglet RF est actif ET qu'on n'a pas defile (sinon on garde la position). */
setInterval(() => { if (!document.hidden && (location.hash || '').includes('/rf') && typeof loadRf === 'function' && rfOffset <= RF_PAGE) loadRf(true); }, 5000);
async function loadFrames() {
  const box = $('#frames-dataset'); if (!box) return;
  box.innerHTML = '<p class="hint">Chargement…</p>';
  const d = await api('/api/frames').catch(() => null);
  const fr = d && d.trames;
  if (!fr || !Object.keys(fr).length) {
    box.innerHTML = '<p class="hint">Aucune trame enregistrée (active « Écoute RF permanente » puis presse une télécommande).</p>';
    return;
  }
  box.innerHTML = Object.entries(fr).map(([serial, info]) => {
    const frames = info.frames || [];
    const shown = frames.slice(0, 300).map(f => `<code title="bouton ${esc(f.button)}">${esc(f.hop)}</code>`).join(' ');
    const more = frames.length > 300 ? ` <span class="hint">+${frames.length - 300} autres</span>` : '';
    return `<div class="card" style="margin-top:8px;padding:10px 12px"><b>${esc(remoteName(serial))}</b>
      <span class="hint">· ${info.count} trame(s) distincte(s) · hop + bouton + t (slide)</span><div class="hops">${shown}${more}</div></div>`;
  }).join('');
}
if ($('#frames-reload')) $('#frames-reload').onclick = loadFrames;

/* ── Journal du firmware (Systeme > Journal) : texte brut de /api/log ── */
let logText = '';
async function loadLog(toEnd) {
  const pre = $('#log-text'); if (!pre) return;
  const r = await fetch('/api/log', { cache: 'no-store' }).catch(() => null);
  if (r && r.status === 401) { showLogin(); return; }
  if (!r || !r.ok) { $('#log-info').textContent = 'Journal indisponible pour le moment.'; return; }
  logText = await r.text();
  const atEnd = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 40;   /* on ne deplace pas qui relit plus haut */
  pre.innerHTML = logText ? logText.split('\n').map(l =>
    /^E \(/.test(l) ? `<span class="e">${esc(l)}</span>` : /^W \(/.test(l) ? `<span class="w">${esc(l)}</span>` : esc(l)).join('\n') : '(vide)';
  if (toEnd || atEnd) pre.scrollTop = pre.scrollHeight;
  const n = logText.split('\n').filter(Boolean).length;
  $('#log-info').textContent = `${n} ligne(s), ${logText.length} octets · ${new Date().toLocaleTimeString()}`;
}
setInterval(() => { if (!document.hidden && (location.hash || '').includes('sys/log') && $('#log-follow') && $('#log-follow').checked) loadLog(false); }, 2000);
if ($('#log-reload')) $('#log-reload').onclick = () => loadLog(true);
if ($('#log-copy')) $('#log-copy').onclick = async () => {
  /* navigator.clipboard exige HTTPS : l'interface est en HTTP, d'ou le repli par selection */
  try { await navigator.clipboard.writeText(logText); toast('Journal copié'); return; } catch (e) {}
  const ta = document.createElement('textarea'); ta.value = logText; document.body.appendChild(ta); ta.select();
  const ok = document.execCommand('copy'); ta.remove();
  toast(ok ? 'Journal copié' : 'Copie impossible : utilise « Télécharger »');
};
/* ── Etat du boitier (Systeme > Etat) : /api/health + statusCache ── */
const WDT = 'chien de garde (blocage)', RESET_TXT = ['inconnue', 'mise sous tension', 'bouton reset',
  'redémarrage logiciel (mise à jour, réglages, retour du Wi-Fi…)', 'plantage', WDT, WDT, WDT, 'réveil de veille',
  'alimentation trop faible', 'SDIO', 'console USB', 'console USB', 'eFuse', "coupure brève de l'alimentation", 'plantage'];
const RESET_BAD = [4, 5, 6, 7, 9, 14, 15];
const ko = b => `${Math.round(b / 1024)} Ko`;
const dur = s => { const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return d ? `${d} j ${h} h` : h ? `${h} h ${m} min` : `${m} min`; };
/* Occupation de la NVS par espace de noms (entrees), du plus gros au plus petit. */
const NVS_NS_TXT = { shutters: 'volets', cfg: 'réglages', pfx_enrol: 'enrôlement',
  'nvs.net80211': 'pile Wi-Fi', phy: 'calibration radio' };
function nvsDetail(h) {
  const ns = Object.entries(h.nvs_ns || {}).sort((a, b) => b[1] - a[1]);
  if (!ns.length) return '…';
  return ns.map(([k, v]) => `${esc(NVS_NS_TXT[k] || k)} ${v}`).join(' · ')
    + ` (entrées, sur ${h.nvs_total - (h.nvs_free - h.nvs_avail)} utilisables)`;
}
async function loadHealth() {
  const box = $('#health'); if (!box) return;
  const h = await api('/api/health').catch(() => null);
  if (!h) { box.innerHTML = '<dt>État indisponible pour le moment.</dt><dd></dd>'; return; }
  const s = statusCache, w = s.wifi || {};
  /* NVS sans la page reservee ; volets de plus = place libre / taille moyenne (banc cfg_store T13) */
  const usable = h.nvs_total - (h.nvs_free - h.nvs_avail);
  const pct = usable > 0 ? Math.round(100 * (1 - h.nvs_avail / usable)) : 0;
  const more = h.volets && h.volet_entries ? Math.floor(h.nvs_avail * h.volets / h.volet_entries) : null;
  const rows = [
    ['Allumé depuis', s.uptime != null ? dur(s.uptime) : '…'],
    ['Dernier redémarrage', RESET_TXT[h.reset] || `code ${h.reset}`, RESET_BAD.includes(h.reset)],
    ['Mémoire', `${ko(h.heap)} libres · au plus bas ${ko(h.heap_min)} · plus gros bloc ${ko(h.heap_blk)}`, h.heap_min < 20480],
    ['Réglages (NVS)', `${pct} % utilisés` + (more != null ? ` · environ ${more} volet${more > 1 ? 's' : ''} de plus` : ''), more != null && more < 2],
    ['NVS par usage', nvsDetail(h)],
    ['Stockage', `${ko(h.spiffs_used)} sur ${ko(h.spiffs_total)}`],
    ['Wi-Fi', w.connected ? sig(w.rssi) : 'non connecté'],
    ['MQTT', s.mqtt ? 'connecté' : 'déconnecté'],
    ['Firmware', `v${esc(h.version)} · partition ${esc(h.part)}`],
  ];
  box.innerHTML = rows.map(([k, v, warn]) => `<dt>${k}</dt><dd${warn ? ' class="warn"' : ''}>${v}</dd>`).join('');
}
setInterval(() => { if (!document.hidden && (location.hash || '').includes('sys/health')) loadHealth(); }, 5000);

$$('.tab').forEach(t => t.onclick = () => { location.hash = t.dataset.t; });
$$('.subtab').forEach(t => t.onclick = () => {
  location.hash = `${t.closest('.panel').dataset.p}/${t.dataset.s}`;
});
addEventListener('hashchange', applyRoute);
$('#theme').onclick = () => {
  const r = document.documentElement;
  const dark = r.getAttribute('data-theme') === 'dark' ||
    (!r.getAttribute('data-theme') && matchMedia('(prefers-color-scheme:dark)').matches);
  const t = dark ? 'light' : 'dark';
  r.setAttribute('data-theme', t);
  try { localStorage.setItem('opfx_theme', t); } catch (e) {}   /* memorise, reapplique dans le <head> */
};
/* Libelles des boutons-symboles ▲ ■ ▼ (lecteurs d'ecran, infobulles), poses apres chaque rendu :
 * les cartes de volets sont regenerees toutes les 3 s. */
const SYM = { up: 'Monter', stop: 'Stop', down: 'Descendre' };
function labelSymbols() {
  const set = (b, l) => { if (b && !b.hasAttribute('aria-label')) { b.setAttribute('aria-label', l); if (!b.title) b.title = l; } };
  $$('.dpad button[data-cmd]').forEach(b => set(b, SYM[b.dataset.cmd]));
  $$('.reorder button[data-mv]').forEach(b => set(b, b.dataset.mv === 'up' ? 'Monter dans la liste' : 'Descendre dans la liste'));
  set($('#pfx-up'), SYM.up); set($('#pfx-stop'), SYM.stop); set($('#pfx-down'), SYM.down);
}
labelSymbols();
if ($('#volets')) new MutationObserver(labelSymbols).observe($('#volets'), { childList: true });

/* ── Helpers ── */
function toast(m) { const t = $('#toast'); t.textContent = m; t.classList.add('show'); setTimeout(() => t.classList.remove('show'), 1600); }
function savedBtn(b, label) { const o = b.textContent; b.textContent = '✓ Enregistré'; setTimeout(() => b.textContent = label || o, 1500); }
const sigLvl = d => d >= -55 ? 4 : d >= -68 ? 3 : d >= -80 ? 2 : 1;
const sig = d => (d == null) ? '' :
  `<span class="sig l${sigLvl(d)}"><i></i><i></i><i></i><i></i></span><span class="dbm">${d} dBm</span>`;
/* RSSI le plus récent par serial, depuis la liste rf (déjà triée du + récent au + ancien) */
function rssiForSerials(serials, rf) { for (const f of rf) if ((serials || []).includes(f.serial)) return f.rssi; return null; }

/* Mode reorganisation : masque les % et neutralise le pilotage, affiche des
   fleches par carte. Purement local a l'UI. */
let reorderMode = false;
if ($('#reorder-toggle')) $('#reorder-toggle').onclick = () => {
  reorderMode = !reorderMode;
  $('#reorder-toggle').textContent = reorderMode ? '✓ Terminé' : '↕ Réorganiser';
  $('#reorder-toggle').classList.toggle('primary', reorderMode);
  renderVolets(statusCache.volets || [], statusCache.rf || []);
};

let dragId = null;   /* volet en cours de glissement */

/* Applique et persiste un nouvel ordre complet. Le cache local est mis a jour
   AVANT l'aller-retour reseau, sinon le prochain poll (3 s) ecraserait l'ordre
   affiche par celui du firmware. */
async function applyOrder(ids) {
  (statusCache.volets || []).forEach(v => { v.order = ids.indexOf(v.id); });
  renderVolets(statusCache.volets || [], statusCache.rf || []);
  await api('/api/volet/order', { method: 'POST', body: JSON.stringify({ ids }) })
    .catch(() => toast('Ordre non enregistré'));
}

/* Deplace un volet d'un cran (dir = -1 monter, +1 descendre). */
async function moveVolet(id, dir) {
  const ids = sortVolets(statusCache.volets || []).map(v => v.id);
  const i = ids.indexOf(id), j = i + dir;
  if (i < 0 || j < 0 || j >= ids.length) return;          /* deja en bout de liste */
  [ids[i], ids[j]] = [ids[j], ids[i]];
  await applyOrder(ids);
}

/* Depose `srcId` a la place de `dstId` : insertion (et non echange), pour que
   le reste de la liste se decale comme on s'y attend en glisser-deposer. */
async function dropVolet(srcId, dstId) {
  const ids = sortVolets(statusCache.volets || []).map(v => v.id);
  const from = ids.indexOf(srcId);
  if (from < 0) return;
  ids.splice(from, 1);
  const to = ids.indexOf(dstId);
  if (to < 0) return;
  ids.splice(to, 0, srcId);
  await applyOrder(ids);
}

/* Tri d'affichage des volets. `order` >= 0 = rang choisi par l'utilisateur ;
   -1 = non range, renvoye en fin de liste dans l'ordre de creation. A defaut
   d'ordre explicite, les centrales restent en tete (comportement d'origine). */
function sortVolets(list) {
  return [...list].sort((a, b) => {
    const oa = (a.order ?? -1), ob = (b.order ?? -1);
    if (oa >= 0 && ob >= 0) return oa - ob;
    if (oa >= 0) return -1;          /* range avant non range */
    if (ob >= 0) return 1;
    return (b.central ? 1 : 0) - (a.central ? 1 : 0);
  });
}

/* ── Volets ── */
function renderVolets(list, rf) {
  const box = $('#volets'); box.innerHTML = '';
  $('#control-empty').hidden = list.length > 0;
  /* Le rangement n'a de sens qu'a partir de 2 volets. */
  const rr = $('#reorder-row'); if (rr) rr.hidden = list.length < 2;
  const rh = $('#reorder-hint'); if (rh) rh.hidden = !reorderMode;
  /* Position affichee UNIQUEMENT si l'ecoute permanente est active : sinon un coup de
   * vraie telecommande desynchronise l'estimation (pas de retour moteur) et un % faux
   * est pire qu'aucun %. */
  const showPos = !!statusCache.listening;
  /* Ordre d'affichage : champ `order` du firmware (rang choisi par l'utilisateur).
     -1 = jamais range -> le volet passe apres les autres, dans son ordre de creation.
     A egalite, les centrales restent en premier (comportement d'origine). */
  const ordered = sortVolets(list);
  for (const v of ordered) {
    const r = rssiForSerials(v.serials, rf);
    const el = document.createElement('div');
    el.className = 'card volet' + (reorderMode ? ' draggable' : '');
    const isC = !!v.central;
    const showP = showPos && !isC;
    if (reorderMode) { el.draggable = true; el.dataset.id = v.id; }
    el.innerHTML = `
      <div class="top"><span class="name">${reorderMode ? '<span class="grip" title="Glisser pour déplacer">⠿</span> ' : ''}${isC ? '🎛' : '🪟'} ${esc(v.id)}${isC ? ' <span class="badge">centrale</span>' : ''}</span>${
        reorderMode ? '<span class="reorder"><button data-mv="up" title="Monter">▲</button><button data-mv="down" title="Descendre">▼</button></span>'
                    : (showP ? `<span class="pct">${v.position ?? '?'}%</span>` : '')}</div>
      <div class="dpad"><button data-cmd="up">▲</button><button data-cmd="stop">■</button><button data-cmd="down">▼</button></div>
      ${showP ? `<div class="slat" style="--p:${v.position ?? 50}"></div>` : ''}
      <div class="serials">${isC
        ? 'volets : ' + (esc(v.members || '') || 'aucun') + ' <span class="hint">(gérer dans Télécommandes -> Centrale)</span>'
        : (v.virt
            ? 'identité : <code>' + esc(remoteName(v.virt_serial)) + '</code>'
              + ((v.serials || []).length ? ' · télécommandes : ' + v.serials.map(s => `<code>${esc(remoteName(s))}</code>`).join(' ') : '')
            : 'serials : ' + ((v.serials || []).map(s => `<code>${esc(remoteName(s))}</code>`).join(' ') || 'aucun'))
          + (r != null ? `<span style="margin-left:6px">· reçu ${sig(r)}</span>` : '')}</div>`;
    /* En mode reorganisation, le pilotage est NEUTRALISE : un clic mal place ne
       doit pas faire partir un volet pendant qu'on range l'affichage. */
    el.querySelectorAll('[data-cmd]').forEach(b => {
      if (reorderMode) { b.disabled = true; return; }
      b.onclick = () => sendCmd({ id: v.id, cmd: b.dataset.cmd }, b);
    });
    const slat = el.querySelector('.slat');
    if (slat && !reorderMode) slat.onclick = e => {
      const p = Math.round(100 * (e.offsetX / e.currentTarget.offsetWidth));
      sendCmd({ id: v.id, cmd: 'pos', value: p });
    };
    el.querySelectorAll('[data-mv]').forEach(b =>
      b.onclick = () => moveVolet(v.id, b.dataset.mv === 'up' ? -1 : 1));
    /* Glisser-deposer (souris/trackpad). Les fleches restent disponibles : le
       drag HTML5 ne fonctionne pas au doigt sur mobile. */
    if (reorderMode) {
      el.ondragstart = e => { dragId = v.id; el.classList.add('dragging'); e.dataTransfer.effectAllowed = 'move'; };
      el.ondragend   = () => { dragId = null; el.classList.remove('dragging'); $$('.volet').forEach(c => c.classList.remove('drop-target')); };
      el.ondragover  = e => { if (dragId && dragId !== v.id) { e.preventDefault(); el.classList.add('drop-target'); } };
      el.ondragleave = () => el.classList.remove('drop-target');
      el.ondrop      = e => { e.preventDefault(); el.classList.remove('drop-target'); if (dragId && dragId !== v.id) dropVolet(dragId, v.id); };
    }
    box.appendChild(el);
  }
}

/* ── Apprentissage (centré volet) ── */
const LEARN_ACTIONS = [
  { a: 'up',   ico: '▲', lbl: 'Montée' },
  { a: 'stop', ico: '■', lbl: 'Stop' },
  { a: 'down', ico: '▼', lbl: 'Descente' },
];
let learning = false;   // capture en cours
let activeVolet = '';   // volet selectionne pour l'apprentissage

function renderVoletPicker() {
  const box = $('#volet-picker'); if (!box) return;
  const ids = (statusCache.volets || []).filter(v => !v.central).map(v => v.id);   /* pas d'apprentissage pour une centrale */
  const nr = $('#new-volet-row');
  const newOpen = nr && !nr.hidden;
  if (activeVolet && !ids.includes(activeVolet) && !newOpen) activeVolet = '';
  box.innerHTML = '';
  for (const id of ids) {
    const c = document.createElement('button');
    c.className = 'chip' + (id === activeVolet ? ' on' : '');
    c.textContent = '🪟 ' + id;
    c.onclick = () => { activeVolet = id; if (nr) { nr.hidden = true; $('#new-volet').value = ''; } renderVoletPicker(); renderLearnSlots(); };
    box.appendChild(c);
  }
  const add = document.createElement('button');
  add.className = 'chip add' + (newOpen ? ' on' : '');
  add.textContent = '+ Nouveau volet';
  add.onclick = () => { if (!nr) return; nr.hidden = false; const inp = $('#new-volet'); inp.focus(); activeVolet = inp.value.trim(); renderVoletPicker(); renderLearnSlots(); };
  box.appendChild(add);
}

/* ── Rappel du profil radio aux captures (issue #63) ──
 * Une capture qui echoue vient souvent du mauvais profil (Eveno emet ~100 kHz plus bas que
 * Profalux) : on rappelle le profil actif sur les ecrans de capture, et le message d'echec
 * renvoie vers le choix du profil ou le calage sur la telecommande (balayage RSSI). */
const PROFILE_NAMES = { profalux: 'Profalux', eveno: 'Eveno', custom: 'Personnalisé' };
function radioProfileNow() {
  const p = cfgCache.radio_profile || 'profalux';
  const khz = cfgCache.freq_khz || (p === 'eveno' ? 868330 : 868425);
  return { p, name: PROFILE_NAMES[p] || p, mhz: (khz / 1000).toFixed(3).replace('.', ',') + ' MHz' };
}
const RADIO_LINKS = '<a href="#sys/device" data-goto="sys-radioprof">Vérifier</a> · '
                  + '<a href="#sys/device" data-goto="sys-sweep">Caler sur ma télécommande</a>';
function refreshRadioReminder() {
  const r = radioProfileNow();
  $$('.radio-reminder').forEach(el => el.innerHTML = `📻 Profil radio : <b>${esc(r.name)} (${r.mhz})</b> · ${RADIO_LINKS}`);
}
function radioProfileHelp() {
  const r = radioProfileNow();
  const why = r.p === 'eveno' ? 'Profalux émet environ 100 kHz plus haut'
            : r.p === 'custom' ? 'elle ne correspond peut-être pas à ta télécommande'
            : 'Eveno émet environ 100 kHz plus bas';
  return `Toujours rien ? Le profil radio est peut-être le mauvais (actuel : <b>${esc(r.name)}</b>, ${r.mhz}) : ${why}. ${RADIO_LINKS}`;
}
function learnFail(show, label) {
  const el = $('#learn-fail'); if (!el) return;
  el.hidden = !show;
  if (show) el.querySelector('span:last-child').innerHTML =
    `<b>Rien capté pour ${esc(label)}.</b> Rapproche la télécommande à moins d'un mètre, tiens le bouton une seconde, et réessaie.<br>${radioProfileHelp()}`;
}
/* Lien vers un reglage de Systeme > Appareil : ouvre l'onglet, amene le champ a l'ecran et
 * l'allume un instant pour qu'on voie ou cliquer. */
document.addEventListener('click', e => {
  const a = e.target.closest('a[data-goto]'); if (!a) return;
  e.preventDefault();
  location.hash = a.getAttribute('href');
  setTimeout(() => {
    const el = document.getElementById(a.dataset.goto); if (!el) return;
    el.scrollIntoView({ behavior: 'smooth', block: 'center' });
    el.classList.remove('flash'); void el.offsetWidth; el.classList.add('flash');
  }, 150);
});
function renderLearnSlots() {
  const box = $('#learn-slots'); if (!box) return;
  const id = activeVolet;
  const v = (statusCache.volets || []).find(x => x.id === id);
  if (v && v.central) {   // une centrale n'a pas de télécommande propre
    box.innerHTML = `<div class="statline ok"><span class="dot"></span><b>C'est une centrale</b> (groupe de volets) - pas d'apprentissage. Gère ses membres dans <b>Volets → Créer une centrale</b>.</div>`;
    return;
  }
  const cmd = (v && v.cmd) || {};
  /* Volet enrôlé : piloté par génération, le clonage n'est pas nécessaire. On autorise
   * quand même d'apprendre ici sa VRAIE télécommande d'origine, uniquement pour SUIVRE
   * la position (le pilotage reste par génération, ces trames ne sont pas rejouées). */
  const virtNote = (v && v.virt)
    ? `<div class="statline ok"><span class="dot"></span><b>Volet enrôlé (télécommande virtuelle).</b> Il est piloté par génération, pas besoin de cloner. En option, apprends ci-dessous sa <b>télécommande d'origine</b> : le boîtier suivra alors la position quand tu l'utilises.</div>`
    : '';
  box.innerHTML = virtNote;
  for (const A of LEARN_ACTIONS) {
    const c = cmd[A.a];
    const learned = c != null;
    const row = document.createElement('div');
    row.className = 'slot' + (learned ? ' done' : '');
    const moveSel = learned
      ? `<select class="slot-move" data-from="${A.a}" title="Déplacer cette trame vers une autre action"><option value="">déplacer…</option>${LEARN_ACTIONS.filter(x => x.a !== A.a).map(x => `<option value="${x.a}">→ ${x.lbl}</option>`).join('')}</select>`
      : '';
    row.innerHTML = `<span class="slot-ico">${A.ico}</span><span class="slot-lbl">${A.lbl}</span>
      <span class="slot-state">${learned ? `✓ appris <code>0x${(c.b).toString(16).toUpperCase()}</code> <code>${esc(c.s)}</code>` : 'à capturer'}</span>
      ${moveSel}<button class="btn slot-btn" data-a="${A.a}">${learned ? 'Recapturer' : 'Capturer'}</button>`;
    /* drag-and-drop : glisser une commande apprise sur un autre slot -> reassignation */
    const act = A.a;
    if (learned) {
      row.draggable = true;
      row.addEventListener('dragstart', e => { e.dataTransfer.setData('text/plain', act); e.dataTransfer.effectAllowed = 'move'; row.classList.add('dragging'); });
      row.addEventListener('dragend', () => row.classList.remove('dragging'));
    }
    row.addEventListener('dragover', e => { e.preventDefault(); e.dataTransfer.dropEffect = 'move'; row.classList.add('dragover'); });
    row.addEventListener('dragleave', () => row.classList.remove('dragover'));
    row.addEventListener('drop', e => {
      e.preventDefault(); row.classList.remove('dragover');
      const from = e.dataTransfer.getData('text/plain');
      if (from && from !== act && activeVolet && !learning) reassign(from, act);
    });
    box.appendChild(row);
  }
  const enabled = !!id && !learning;
  box.querySelectorAll('.slot-btn').forEach(b => { b.disabled = !enabled; b.onclick = () => captureAction(b.dataset.a, b); });
  box.querySelectorAll('.slot-move').forEach(sel => { sel.disabled = !enabled; sel.onchange = () => { if (sel.value) reassign(sel.dataset.from, sel.value); }; });
  const serialEl = $('#learn-serial');
  if (serialEl) {
    const sers = (v && v.serials) || [];
    serialEl.innerHTML = (v && v.virt)   /* virtuel : son identité d'émission (virt_serial) + les vraies télécommandes associées */
      ? `Identité virtuelle : <code>${esc(remoteName(v.virt_serial))}</code>`
        + (sers.length ? ` · Télécommandes : ${sers.map(s => `<code>${esc(remoteName(s))}</code>`).join(' ')}` : '')
      : sers.length > 1
        ? `⚠ Commandes de plusieurs télécommandes : ${sers.map(s => `<code>${esc(s)}</code>`).join(' ')}`
        : sers.length ? `Télécommande : <code>${esc(sers[0])}</code>` : '';
  }
  const orientRow = $('#orient-row');
  if (orientRow) orientRow.hidden = !v;
  const orientInp = $('#orient-input');
  if (orientInp && v && document.activeElement !== orientInp)
    orientInp.value = (v.orientation != null && v.orientation >= 0) ? v.orientation : '';
  const delRow = $('#del-volet-row');
  if (delRow) delRow.hidden = !v;
  const hint = $('#learn-hint');
  if (hint) hint.textContent = !id
    ? 'Choisis un volet ci-dessus (ou crée-en un) pour activer la capture.'
    : (v && v.virt)
      ? `Volet enrôlé « ${id} » : pour suivre la position, clique Capturer puis appuie sur sa télécommande d'origine en tenant le bouton une seconde (< 1 m du boîtier). Facultatif.`
      : `Volet « ${id} » : clique Capturer, puis appuie sur le bouton de ta télécommande en le tenant une seconde (< 1 m du boîtier).`;
  updateRemoteNameField();
}

/* Nom de la telecommande : prerempli avec le nom actuel (modifiable via Renommer)
 * quand le volet a deja une telecommande apprise ; vide + cache sinon. */
function updateRemoteNameField() {
  const inp = $('#cap-name'); if (!inp) return;
  const btn = $('#rename-btn');
  const vol = (statusCache.volets || []).find(x => x.id === activeVolet);
  const serial = vol && vol.serials && vol.serials[0];
  inp.dataset.serial = serial || '';
  /* pre-rempli : nom actuel de la telecommande, sinon le nom du volet (defaut modifiable), sinon vide */
  if (document.activeElement !== inp) inp.value = serial ? ((statusCache.remotes || {})[serial] || activeVolet || '') : '';
  inp.placeholder = serial ? 'Nom de cette télécommande' : 'ex : Murale chambre parents';
  if (btn) btn.hidden = !serial;
}

const renameBtn = $('#rename-btn');
if (renameBtn) renameBtn.onclick = async () => {
  const inp = $('#cap-name'); const serial = inp.dataset.serial;
  if (!serial) return;
  await api('/api/remote', { method: 'POST', body: JSON.stringify({ serial, name: inp.value.trim() }) });
  toast('Télécommande renommée'); await loadStatus();
};
const orientSave = $('#orient-save');
if (orientSave) orientSave.onclick = async () => {
  if (!activeVolet) return;
  const val = $('#orient-input').value.trim();
  const ori = val === '' ? -1 : parseInt(val, 10);
  await api('/api/volet/orientation', { method: 'POST', body: JSON.stringify({ id: activeVolet, orientation: ori }) });
  toast('Orientation enregistrée'); await loadStatus();
};

async function reassign(from, to) {
  if (!activeVolet || from === to) { renderLearnSlots(); return; }
  await api('/api/learn/reassign', { method: 'POST', body: JSON.stringify({ id: activeVolet, from, to }) });
  toast(`Commande déplacée vers ${LEARN_ACTIONS.find(x => x.a === to).lbl}`);
  await loadStatus();
}

/* Un NOUVEAU nom de volet ne peut pas contenir de virgule : les membres d'une
   centrale sont une liste separee par des virgules (le firmware refuse aussi). */
const COMMA_MSG = 'La virgule n’est pas permise dans un nom de volet';
const isNewNameWithComma = name => name.includes(',') && !(statusCache.volets || []).some(v => v.id === name);
async function captureAction(action, btn) {
  const id = activeVolet;
  if (!id) { toast('Choisis d’abord un volet'); return; }
  if (isNewNameWithComma(id)) { toast(COMMA_MSG); return; }
  const label = LEARN_ACTIONS.find(x => x.a === action).lbl;
  learning = true; renderLearnSlots();
  learnFail(false);
  btn.disabled = true; btn.textContent = `⏳ Appuie sur ${label}…`;
  await api('/api/learn/start', { method: 'POST', body: JSON.stringify({ action }) });
  const t0 = Date.now();
  const poll = setInterval(async () => {
    const r = await api('/api/learn/poll').catch(() => null);
    if (r && r.bits) {
      clearInterval(poll); learning = false;
      /* Volet CLONE : les 3 trames up/down/stop doivent venir de la MEME telecommande
       * (sinon on rejoue un melange) -> garde-fou. Volet ENROLE : `serials` est l'ENSEMBLE
       * des vraies telecommandes a reconnaitre pour le suivi de position (origine, devmel,
       * centrale...), on en associe plusieurs -> pas de garde-fou "meme telecommande". */
      const vol = (statusCache.volets || []).find(x => x.id === id);
      const known = (vol && vol.serials) || [];
      if (!(vol && vol.virt) && known.length && !known.includes(r.serial)) {
        toast(`Pas la bonne télécommande : trame de ${r.serial}, le volet utilise ${known[0]}`);
        renderLearnSlots();
        return;
      }
      const name = $('#cap-name').value.trim();
      if (name) await api('/api/remote', { method: 'POST', body: JSON.stringify({ serial: r.serial, name }) });
      await api('/api/learn/assign', { method: 'POST', body: JSON.stringify({ id, action, bits: r.bits }) });
      toast(`✓ ${label} apprise (0x${r.button}, ${r.rssi} dBm)`);
      await loadStatus();     // rafraîchit -> le slot passe en ✓
    } else if (Date.now() - t0 > 16000) {
      clearInterval(poll); learning = false;
      learnFail(true, label);
      renderLearnSlots();
    }
  }, 400);
}

const newVoletInput = $('#new-volet');
if (newVoletInput) newVoletInput.oninput = () => { activeVolet = newVoletInput.value.trim(); renderLearnSlots(); };
if ($('#goto-enrol')) $('#goto-enrol').onclick = (e) => { e.preventDefault(); location.hash = 'remotes/enrol'; };

const delVoletBtn = $('#del-volet');
if (delVoletBtn) delVoletBtn.onclick = async () => {
  const id = activeVolet;
  if (!id) return;
  if (!confirm(`Supprimer le volet « ${id} » et toutes ses commandes apprises ?`)) return;
  await api('/api/volet/delete', { method: 'POST', body: JSON.stringify({ id }) });
  activeVolet = '';
  const nr = $('#new-volet-row'); if (nr) { nr.hidden = true; $('#new-volet').value = ''; }
  toast('Volet supprimé');
  await loadStatus();
};

/* ── Télécommandes ── */
function renderRemotes(s, rf) {
  const box = $('#remotes-list'); if (!box) return;
  renderIgnored(s.remotes_ignored || []);
  const ae = document.activeElement;   /* saisie d'un nom en cours : ne pas l'ecraser au poll */
  if (ae && ae.tagName === 'INPUT' && box.contains(ae)) return;
  const entries = Object.entries(s.remotes || {});
  const max = s.remotes_max || 16;
  $('#remotes-empty').hidden = entries.length > 0;
  $('#remotes-count').textContent = `${entries.length} / ${max}`;
  /* Liste pleine : le firmware refuse les nouvelles et dit laquelle (issue #39). */
  const full = $('#remotes-full');
  full.hidden = entries.length < max;
  full.querySelector('b').textContent = s.remote_refused
    ? `Liste pleine : la télécommande ${s.remote_refused} n'a pas pu être enregistrée. Supprime une télécommande inutile.`
    : `Liste pleine : une nouvelle télécommande ne pourra pas être enregistrée. Supprime une télécommande inutile.`;
  box.innerHTML = '';
  for (const [serial, name] of entries) {
    const r = rssiForSerials([serial], rf);
    const used = voletForSerial(serial);
    /* Grille a colonnes fixes (style.css .remote-row) : cellules toujours presentes, meme
     * vides, pour que le champ du nom ait la meme largeur sur toutes les lignes. */
    const row = document.createElement('div'); row.className = 'remote-row';
    row.innerHTML = `<code class="badge r-ser">${esc(serial)}</code>
      <input class="r-name" value="${esc(name)}" placeholder="nom…">
      <span class="r-sig">${sig(r)}</span>
      <span class="hint r-used" title="${esc(used || '')}">${used ? esc(used)
        : '<button class="btn r-mk" title="Créer un volet avec ce nom, à partir des dernières trames ▲ ■ ▼ de cette télécommande dans le journal RF">➕ Créer le volet</button>'}</span>
      <span class="r-act"><button class="btn r-ign" title="Ignorer : ne plus l'enregistrer quand elle est captée">🚫</button><button class="btn danger r-del" title="Supprimer cette télécommande">🗑</button></span>`;
    const inp = row.querySelector('input');
    inp.onchange = () => api('/api/remote', { method: 'POST', body: JSON.stringify({ serial, name: inp.value.trim() }) });
    const label = name ? `« ${name} » (${serial})` : serial;
    /* Sans confirmation : une telecommande supprimee ou ignoree se recupere d'un appui
     * (ou d'un clic sur sa croix dans les ignorees). */
    row.querySelector('.r-del').onclick = async () => {
      const rep = await fetch('/api/remote/delete', { method: 'POST', body: JSON.stringify({ serial }) });
      toast(rep.ok ? `${label} supprimée` : `Refusé : ${await rep.text()}`);
      await loadStatus();
    };
    /* Creer le volet : un champ « nom du volet » apparait dans la cellule, distinct du nom de la
     * telecommande (prerempli avec celui-ci). ✓ ou Entree cree, Echap annule. */
    const mk = row.querySelector('.r-mk');
    if (mk) mk.onclick = () => {
      const cell = mk.parentElement;
      cell.classList.add('r-mkform');
      cell.innerHTML = `<input class="r-vname" placeholder="nom du volet" spellcheck="false"><button class="btn r-mkok" title="Créer le volet">✓</button>`;
      const vin = cell.querySelector('input');
      vin.value = inp.value.trim();
      vin.focus(); vin.select();
      const create = async () => {
        const vname = vin.value.trim();
        if (!vname) { toast('Donne un nom au volet'); vin.focus(); return; }
        if (isNewNameWithComma(vname)) { toast(COMMA_MSG); vin.focus(); return; }
        const rep = await fetch('/api/remote/create_volet', { method: 'POST', body: JSON.stringify({ serial, name: vname }) });
        if (!rep.ok) { toast(`Refusé : ${await rep.text()}`); vin.focus(); return; }
        const d = await rep.json().catch(() => ({}));
        const lab = { up: '▲', stop: '■', down: '▼' };
        const got = (d.learned || '').split(',').filter(Boolean);
        const miss = ['up', 'stop', 'down'].filter(a => !got.includes(a));
        toast(`Volet « ${vname} » créé : ${got.map(a => lab[a]).join(' ')} appris`
          + (miss.length ? ` · ${miss.map(a => lab[a]).join(' ')} à apprendre (aucune trame dans le journal)` : ''));
        vin.blur(); await loadStatus();
      };
      cell.querySelector('.r-mkok').onclick = create;
      vin.onkeydown = e => {
        if (e.key === 'Enter') create();
        else if (e.key === 'Escape') { vin.blur(); loadStatus(); }   /* annule : la ligne est redessinee */
      };
    };
    row.querySelector('.r-ign').onclick = async () => {
      const rep = await fetch('/api/remote/ignore', { method: 'POST', body: JSON.stringify({ serial }) });
      toast(rep.ok ? `${label} ignorée` : `Refusé : ${await rep.text()}`);
      await loadStatus();
    };
    box.appendChild(row);
  }
}

/* Telecommandes ignorees : une bulle par serial (croix = ne plus l'ignorer), et un champ pour
 * en ajouter une a la main. */
function renderIgnored(list) {
  const box = $('#ignored-box'); if (!box) return;
  const inp = $('#ignored-input');
  const key = list.join(',');
  if (box.dataset.k === key) return;   /* inchange : ne pas recreer (garde la saisie) */
  box.dataset.k = key;
  $$('.tag', box).forEach(t => t.remove());
  for (const serial of list) {
    const t = document.createElement('span'); t.className = 'tag';
    t.innerHTML = `<code>${esc(serial)}</code><button title="Ne plus ignorer">×</button>`;
    t.querySelector('button').onclick = async (e) => {
      e.stopPropagation();
      await api('/api/remote/unignore', { method: 'POST', body: JSON.stringify({ serial }) }).catch(() => {});
      toast(`${serial} n'est plus ignorée`);
      await loadStatus();
    };
    box.insertBefore(t, inp);
  }
}
if ($('#ignored-box')) {
  $('#ignored-box').onclick = () => $('#ignored-input').focus();
  $('#ignored-input').onkeydown = async (e) => {
    if (e.key !== 'Enter') return;
    const serial = e.target.value.trim(); if (!serial) return;
    const rep = await fetch('/api/remote/ignore', { method: 'POST', body: JSON.stringify({ serial }) });
    toast(rep.ok ? 'Télécommande ignorée' : `Refusé : ${await rep.text()}`);
    if (rep.ok) e.target.value = '';
    await loadStatus();
  };
}
const remoteName = s => (statusCache.remotes || {})[s] || s;

/* ── Calibration ── */
let calT = {}, calStart = 0, calibLive = false;   /* calibLive : true pendant un chrono -> ne pas ecraser l'affichage */
function chrono(dir) { calibLive = true; calStart = performance.now(); $(`#cal-${dir}-start`).disabled = true; $(`#cal-${dir}-stop`).disabled = false; }
function chronoStop(dir) {
  calT[dir] = Math.round(performance.now() - calStart);
  $(`#cal-${dir}-t`).textContent = (calT[dir] / 1000).toFixed(1) + 's';
  const m = $(`#cal-${dir}-manual`); if (m) m.value = (calT[dir] / 1000).toFixed(1);   /* refleter dans le champ manuel */
  $(`#cal-${dir}-start`).disabled = false; $(`#cal-${dir}-stop`).disabled = true;
  $('#cal-save').disabled = !(calT.up && calT.down);
}
/* Relit les temps ENREGISTRES du volet selectionne (le statut expose travel_up_ms/down_ms). */
function fillCalib() {
  const sel = $('#calib-id'); if (!sel) return;
  const v = (statusCache.volets || []).find(x => x.id === sel.value);
  if (!v) return;
  calT = { up: v.travel_up_ms || 0, down: v.travel_down_ms || 0 };
  const ut = $('#cal-up-t'), dt = $('#cal-down-t');
  if (ut) ut.textContent = (calT.up / 1000).toFixed(1) + 's';
  if (dt) dt.textContent = (calT.down / 1000).toFixed(1) + 's';
  const um = $('#cal-up-manual'), dm = $('#cal-down-manual');
  if (um && document.activeElement !== um) um.value = calT.up ? (calT.up / 1000) : '';
  if (dm && document.activeElement !== dm) dm.value = calT.down ? (calT.down / 1000) : '';
  const sv = $('#cal-save'); if (sv) sv.disabled = !(calT.up && calT.down);
}
/* Saisie manuelle des temps (secondes) -> met a jour calT (ms) et active Enregistrer. */
function calManual(sel, key) {
  const el = $(sel); if (!el) return;
  el.oninput = () => {
    calibLive = true;   /* saisie en cours -> le poll ne doit pas ecraser */
    const s = parseFloat(el.value);
    calT[key] = (isNaN(s) || s < 0) ? 0 : Math.round(s * 1000);
    const t = $(`#cal-${key}-t`); if (t) t.textContent = ((calT[key] || 0) / 1000).toFixed(1) + 's';
    $('#cal-save').disabled = !(calT.up && calT.down);
  };
}
calManual('#cal-up-manual', 'up');
calManual('#cal-down-manual', 'down');
const calId = () => $('#calib-id').value.trim();
if ($('#calib-id')) $('#calib-id').onchange = () => { calibLive = false; fillCalib(); };   /* change de volet -> relit ses temps enregistres */
$('#cal-down-start').onclick = () => { chrono('down'); api('/api/shutter', { method:'POST', body: JSON.stringify({ id: calId(), cmd:'down' }) }); };
$('#cal-down-stop').onclick  = () => { chronoStop('down'); api('/api/shutter', { method:'POST', body: JSON.stringify({ id: calId(), cmd:'stop' }) }); };
$('#cal-up-start').onclick   = () => { chrono('up'); api('/api/shutter', { method:'POST', body: JSON.stringify({ id: calId(), cmd:'up' }) }); };
$('#cal-up-stop').onclick    = () => { chronoStop('up'); api('/api/shutter', { method:'POST', body: JSON.stringify({ id: calId(), cmd:'stop' }) }); };
$('#cal-save').onclick = () => { api('/api/calibrate', { method: 'POST',
  body: JSON.stringify({ id: calId(), travel_up_ms: calT.up, travel_down_ms: calT.down }) }); calibLive = false; toast('Calibration enregistrée'); };

/* ── Enrôlement d'une identité virtuelle 0x067 (expérimental, sans télécommande) ── */
/* Gestes PHYSIQUES à faire APRÈS l'émission (révélés seulement à la fin des 5 s). */
const PFX_GESTURES = {
  R6: [
    'Monte le volet jusqu\'en butée haute.',
    'Descends de ~4 lames.',
    'Remonte jusqu\'en butée haute.',
    'Le volet fait un va-et-vient = enrôlé ✅. S\'il ne bouge pas, attends ~10 s puis teste ▲ ■ ▼ quand même : beaucoup de moteurs confirment sans bouger.',
  ],
  R7: [
    'Monte → descends ~4 lames → remonte (procédure proche, à confirmer).',
    'Va-et-vient = enrôlé. S\'il ne bouge pas, attends ~10 s puis teste ▲ ■ ▼ quand même. Rapporte le résultat sur GitHub.',
  ],
  R8: [
    'Montée → laisse le volet s\'arrêter seul en butée haute.',
    'Descente → laisse ~4 lames apparentes.',
    'Montée → laisse s\'arrêter seul.',
    'Le volet fait un va-et-vient = enrôlé ✅. S\'il ne bouge pas, attends ~10 s puis teste ▲ ■ ▼ quand même. Rapporte le résultat sur GitHub.',
  ],
};
/* Geste PHYSIQUE à faire AVANT l'émission (certains modèles l'exigent, ex. NeoSol :
 * le volet doit partir d'une position de référence). Vide = rien à faire avant. */
const PFX_PREGESTURES = {
  R8: 'NeoSol : AVANT d\'émettre, descends le volet et laisse ~4 lames apparentes.',
};
let pfxModels = [];
let pfxTestLocked = true;   // dpad de test (▲ ■ ▼) verrouille tant que le delai de confirmation du moteur n'est pas ecoule
async function loadPfx() {
  const d = await api('/api/pfx').catch(() => null);
  if (!d) return;
  pfxModels = d.models || [];
  const sel = $('#pfx-model');
  if (sel && !sel.dataset.filled && pfxModels.length) {
    sel.innerHTML = pfxModels.map((m, i) => `<option value="${i}">${esc(m.name)} · ${esc(m.routine)}</option>`).join('');
    sel.dataset.filled = '1';
  }
  const box = $('#pfx-ident'), steps = $('#pfx-steps');
  if (!box) return;
  /* L'apprentissage n'est permis qu'apres une capture reussie dans cette tentative (issue #53). */
  const lb = $('#pfx-learn');
  if (lb) { lb.disabled = !d.learn_ok; lb.title = d.learn_ok ? '' : 'Capte d’abord la télécommande de ce volet'; }
  if (d.active) {
    box.hidden = false; steps.hidden = false; box.className = 'statline ok';
    box.querySelector('b').textContent = `Identité ${d.serial} · slot ${d.idx + 1}/63 · compteur ${d.counter}`
      + (d.remote && d.remote !== '0x0000000' ? ` · télécommande ${d.remote}` : '');
    if (sel) sel.value = String(d.model);
    const ol = $('#pfx-gestures'); const m = pfxModels[d.model];
    if (ol && m) ol.innerHTML = (PFX_GESTURES[m.routine] || PFX_GESTURES.R6).map(s => `<li>${s}</li>`).join('');
    const pre = $('#pfx-pre');
    if (pre && m) { const p = PFX_PREGESTURES[m.routine]; pre.hidden = !p; if (p) pre.querySelector('b').textContent = p; }
  } else { box.hidden = true; steps.hidden = true; }
}
function pfxResetSteps() {   // nouvelle identité -> on recache les gestes jusqu'à la prochaine émission
  const after = $('#pfx-after-emit'); if (after) after.hidden = true;
  const st = $('#pfx-learn-status'); if (st) st.hidden = true;
  const w = $('#pfx-wait'); if (w) w.hidden = true;
  const go = $('#pfx-gestes-ok'); if (go) { go.hidden = false; go.disabled = false; }
  pfxLockTest(true);
}
/* Verrouille/déverrouille le dpad de test (▲ ■ ▼) pendant le délai de confirmation du moteur. */
function pfxLockTest(locked) {
  pfxTestLocked = locked;
  ['#pfx-up', '#pfx-stop', '#pfx-down'].forEach(s => { const b = $(s); if (b) b.disabled = locked; });
}
function pfxWait(txt, cls) {
  const w = $('#pfx-wait'); if (!w) return;
  w.hidden = false; w.className = 'statline' + (cls ? ' ' + cls : '');
  w.querySelector('b').textContent = txt;
}
function pfxCapStatus(txt, cls, moreHtml) {
  const st = $('#pfx-capture-status'); if (!st) return;
  st.hidden = false; st.className = 'statline' + (cls ? ' ' + cls : '');
  st.querySelector('b').textContent = txt;
  st.querySelector('.more').innerHTML = moreHtml || '';
}
if ($('#pfx-capture')) $('#pfx-capture').onclick = async () => {
  const b = $('#pfx-capture'), o = b.textContent;
  const model = Number($('#pfx-model').value || 0);
  const wait = ms => new Promise(r => setTimeout(r, ms));
  b.disabled = true; setRadioBusy(true);
  if ($('#pfx-renew-row')) $('#pfx-renew-row').hidden = true;
  const r = await api('/api/pfx/capture', { method: 'POST', body: JSON.stringify({ model }) }).catch(() => null);
  if (!r || !r.ok) { pfxCapStatus('Échec du démarrage de la capture', 'bad'); b.disabled = false; setRadioBusy(false); return; }
  let n = 15, d = null;
  const timer = setInterval(() => { n = Math.max(0, n - 1); pfxCapStatus(`📡 Appuie MAINTENANT sur ta télécommande d'origine… ${n} s`); }, 1000);
  pfxCapStatus("📡 Appuie MAINTENANT sur ta télécommande d'origine… 15 s");
  for (let i = 0; i < 20; i++) { await wait(1000); d = await api('/api/pfx/capture/poll').catch(() => null); if (d && d.done) break; }
  clearInterval(timer);
  b.disabled = false; b.textContent = o; setRadioBusy(false);
  if (!d || !d.done) pfxCapStatus('Aucune réponse du boîtier', 'bad');
  else if (d.rc === 0 && d.new) { pfxCapStatus(`✅ Nouveau moteur - identité ${d.serial} attribuée (télécommande ${d.remote})`, 'ok'); pfxResetSteps(); }
  else if (d.rc === 0) {
    /* Identite reprise : proposer une identite neuve, au cas ou d'autres moteurs la connaissent (issue #53). */
    pfxCapStatus(`✅ Moteur déjà connu - même identité ${d.serial} (télécommande ${d.remote})`, 'ok',
      d.used_by ? `⚠️ Cette identité pilote déjà le volet virtuel « <b>${esc(d.used_by)}</b> » : tout moteur qui la connaît bouge avec lui.` : '');
    if ($('#pfx-renew-row')) $('#pfx-renew-row').hidden = false;
  }
  else if (d.rc === -1) pfxCapStatus('⚠️ 63 identités déjà attribuées : oublie-en une pour libérer un slot.', 'bad');
  /* Capture ratee : l'identite d'avant est deselectionnee, rien ne partira avec elle (issue #53). */
  else if (d.rc === -3) pfxCapStatus("❌ La capture a échoué : le boîtier n'a rien reçu. Rapproche la télécommande du boîtier (moins d'un mètre), tiens le bouton une seconde, et recommence.", 'bad', radioProfileHelp());
  else pfxCapStatus("❌ La capture a échoué : télécommande non reconnue. Recommence avec la télécommande de ce volet.", 'bad');
  await loadPfx();
};
if ($('#pfx-renew')) $('#pfx-renew').onclick = async () => {
  const b = $('#pfx-renew'); b.disabled = true;
  const r = await api('/api/pfx/renew', { method: 'POST' }).catch(() => null);
  b.disabled = false;
  if (r && r.ok) {
    $('#pfx-renew-row').hidden = true;
    pfxCapStatus(`✅ Identité neuve ${r.serial} attribuée : l'ancienne reste réservée, aucun autre moteur ne la recevra.`, 'ok');
    pfxResetSteps();
  } else if (r && r.rc === -1) pfxCapStatus('⚠️ 63 identités déjà attribuées : plus de slot libre pour une identité neuve.', 'bad');
  else pfxCapStatus('❌ Refusé : refais la capture de la télécommande, puis recommence.', 'bad');
  await loadPfx();
};
function pfxStatus(txt, cls) {
  const st = $('#pfx-learn-status'); if (!st) return;
  st.hidden = false; st.className = 'statline' + (cls ? ' ' + cls : '');
  st.querySelector('b').textContent = txt;
}
if ($('#pfx-learn')) $('#pfx-learn').onclick = async () => {
  const b = $('#pfx-learn');
  if (radioBusy) { toast('Radio occupée, réessaie dans un instant'); return; }   // plus de clic avalé en silence
  const after = $('#pfx-after-emit');
  if (after) after.hidden = true;                          // cache les gestes PENDANT l'émission
  setRadioBusy(true); b.disabled = true; const o = b.textContent; b.textContent = '📡 Émission…';
  let n = 5; pfxStatus('⏳ Émission ~5 s - ne touche à rien… ' + n + ' s');
  const timer = setInterval(() => { n = Math.max(0, n - 1); pfxStatus('⏳ Émission ~5 s - ne touche à rien… ' + n + ' s'); }, 1000);
  try {
    const r = await api('/api/pfx/learn', { method: 'POST' });
    clearInterval(timer);
    if (r && r.ok) {
      pfxStatus('✅ Émission finie. Fais les gestes ci-dessous MAINTENANT, puis clique « J\'ai fait les gestes ».', 'ok');
      if (after) after.hidden = false;                     // révèle les gestes SEULEMENT à la fin
      pfxLockTest(true);                                   // le test reste bloqué jusqu'à la fin du délai de confirmation
      const go = $('#pfx-gestes-ok'); if (go) { go.hidden = false; go.disabled = false; }
      const w = $('#pfx-wait'); if (w) w.hidden = true;
    } else pfxStatus(r && r.err ? '❌ ' + r.err : 'Échec (radio occupée ?)', 'bad');   /* refus : pas de capture reussie */
  }
  catch (e) { clearInterval(timer); pfxStatus('Radio occupée, réessaie dans un instant.', 'bad'); }
  finally { b.disabled = false; b.textContent = o; setRadioBusy(false); await loadPfx(); }
};
async function pfxCmd(cmd, btn) {
  if (pfxTestLocked) return;   // délai de confirmation du moteur pas encore écoulé
  if (radioBusy) return;
  setRadioBusy(true); if (btn) btn.classList.add('sending');
  try { await api('/api/pfx/cmd', { method: 'POST', body: JSON.stringify({ cmd }) }); }
  catch (e) { toast('Radio occupée, réessaie dans un instant'); }
  finally { if (btn) btn.classList.remove('sending'); setRadioBusy(false); loadPfx(); }
}
if ($('#pfx-up')) $('#pfx-up').onclick = () => pfxCmd('up', $('#pfx-up'));
if ($('#pfx-stop')) $('#pfx-stop').onclick = () => pfxCmd('stop', $('#pfx-stop'));
if ($('#pfx-down')) $('#pfx-down').onclick = () => pfxCmd('down', $('#pfx-down'));
/* Les gestes faits : on force le délai de confirmation du moteur (~10 s) avant d'autoriser le test. */
if ($('#pfx-gestes-ok')) $('#pfx-gestes-ok').onclick = () => {
  const go = $('#pfx-gestes-ok'); go.disabled = true; go.hidden = true;
  pfxLockTest(true);
  let n = 10;
  pfxWait('⏳ Le moteur confirme, patiente… ' + n + ' s');
  const t = setInterval(() => {
    n = Math.max(0, n - 1);
    if (n > 0) { pfxWait('⏳ Le moteur confirme, patiente… ' + n + ' s'); return; }
    clearInterval(t); pfxLockTest(false);
    pfxWait('✅ Tu peux tester maintenant avec ▲ ■ ▼.', 'ok');
  }, 1000);
};
if ($('#pfx-save-volet')) $('#pfx-save-volet').onclick = async () => {
  const name = ($('#pfx-volet-name').value || '').trim();
  if (!name) { toast('Donne un nom au volet'); return; }
  if (isNewNameWithComma(name)) { toast(COMMA_MSG); return; }
  const r = await api('/api/pfx/save_volet', { method: 'POST', body: JSON.stringify({ id: name }) }).catch(() => null);
  if (r && r.ok) {
    toast('Volet créé - voir l’onglet Volets');
    $('#pfx-volet-name').value = '';
    await loadPfx();                       // l'identité a migré vers le volet
    if (typeof loadStatus === 'function') await loadStatus();
  } else toast('Échec de l’enregistrement');
};
if ($('#pfx-forget')) $('#pfx-forget').onclick = async () => {
  await api('/api/pfx/forget', { method: 'POST' }).catch(() => {});
  toast('Identité désélectionnée · compteur conservé'); await loadPfx();
};

/* ── Calage sur la télécommande : balayage RSSI (issue #51) ── */
let sweepKhz = 0;
function sweepShow(txt, cls, applyKhz) {
  const st = $('#sys-sweep-res'); if (!st) return;
  st.hidden = false; st.className = 'statline' + (cls ? ' ' + cls : '');
  st.querySelector('b').textContent = txt;
  sweepKhz = applyKhz || 0;
  $('#sys-sweep-apply-row').hidden = !sweepKhz;
}
if ($('#sys-sweep')) $('#sys-sweep').onclick = async () => {
  const b = $('#sys-sweep');
  const wait = ms => new Promise(res => setTimeout(res, ms));
  const r = await api('/api/rx/sweep', { method: 'POST' }).catch(() => null);
  if (!r || !r.ok) { sweepShow('Lancement impossible', 'bad'); return; }
  b.disabled = true; setRadioBusy(true);
  sweepShow('📡 Balayage en cours, garde la télécommande sur P…');
  let d = null;
  for (let i = 0; i < 20; i++) { await wait(1000); d = await api('/api/rx/sweep').catch(() => null); if (d && d.state === 2) break; }
  b.disabled = false; setRadioBusy(false);
  if (!d || d.state !== 2) { sweepShow('Pas de réponse du boîtier', 'bad'); return; }
  const mhz = k => (k / 1000).toFixed(3).replace('.', ',') + ' MHz';
  const lvl = `signal ${Math.round(d.peak_dbm)} dBm, bruit ${Math.round(d.floor_dbm)} dBm`;
  if (d.status === 0) {
    const off = d.center_khz - d.from_khz;
    sweepShow(`Porteuse de la télécommande : ${mhz(d.center_khz)} (${off >= 0 ? '+' : ''}${off} kHz par rapport à la fréquence actuelle) · ${lvl}`,
      'ok', d.center_khz);
  } else if (d.status === -2) sweepShow(`Signal trop faible (${lvl}) : rapproche la télécommande, vérifie qu'elle est bien sur P, et recommence.`, 'bad');
  else if (d.status === -3) sweepShow(`Porteuse hors de la plage balayée (±150 kHz) : choisis l'autre profil radio et recommence.`, 'bad');
  else sweepShow(`Échec du balayage (${d.status})`, 'bad');
};
if ($('#sys-sweep-apply')) $('#sys-sweep-apply').onclick = async () => {
  if (!sweepKhz) return;
  const ok = await api('/api/config', { method: 'POST', body: JSON.stringify({ radio_profile: 'custom', freq_khz: sweepKhz }) })
    .then(() => true).catch(() => false);
  toast(ok ? `Fréquence personnalisée : ${sweepKhz} kHz` : 'Refusé (fréquence hors de la plage de la carte ?)');
  if (ok) { $('#sys-sweep-apply-row').hidden = true; if (typeof loadConfig === 'function') await loadConfig(); }
};

/* ── Config : Wi-Fi / MQTT / Système (sauvegardes séparées, partielles) ── */
async function loadConfig() {
  const c = await api('/api/config').catch(() => ({}));
  $('#wifi-ssid').value = c.wifi_ssid || '';
  cfgCache = c;
  if ($('#wifi-device')) {
    $('#wifi-device').value = c.device || '';
    $('#wifi-device').placeholder = c.mdns_default || c.mdns_host || 'openprofalux';   /* nom pris si le champ reste vide */
    refreshWifiUrl();
  }
  $('#mqtt-uri').value = c.mqtt_uri || ''; $('#mqtt-user').value = c.mqtt_user || '';
  if ($('#mqtt-pass')) $('#mqtt-pass').placeholder = c.mqtt_pass_len
    ? `•••••••• (${c.mqtt_pass_len} car. enregistrés, laisser vide pour ne pas changer)`
    : 'mot de passe du broker';
  /* Certificats TLS : on n'affiche que la longueur enregistrée, jamais le contenu. */
  const certPh = (id, len, empty) => { const e = $(id); if (e) e.placeholder = len
    ? `${len} octets enregistrés, laisser vide pour ne pas changer` : empty; };
  certPh('#mqtt-ca', c.mqtt_ca_len, '-----BEGIN CERTIFICATE-----');
  certPh('#mqtt-cert', c.mqtt_cert_len, '-----BEGIN CERTIFICATE-----');
  certPh('#mqtt-key', c.mqtt_key_len, '-----BEGIN PRIVATE KEY-----');
  $('#sys-device').value = c.device || ''; $('#sys-logframes').checked = !!c.log_frames;
  const uph = $('#sys-uipass-hint'), upi = $('#sys-uipass'), upc = $('#sys-uipass-clear');
  if (uph) uph.innerHTML = c.ui_auth
    ? '🔒 <b>Protection active.</b> Le champ ci-dessous reste vide : le mot de passe enregistré n\'est jamais réaffiché. Saisis-en un nouveau pour le remplacer.'
    : '⚠️ <b>Interface ouverte</b> : tout appareil du réseau peut piloter les volets et envoyer un firmware. Saisis un mot de passe ci-dessous pour la protéger.';
  /* Le placeholder ne doit pas contredire l'etat : "vide = ouverte" etait faux quand
     une protection est deja active (le champ est vide parce qu'on ne reaffiche jamais
     le mot de passe, pas parce qu'il n'y en a pas). */
  if (upi) upi.placeholder = c.ui_auth ? 'nouveau mot de passe (inchangé si vide)' : 'mot de passe à définir';
  if (upc) upc.hidden = !c.ui_auth;   /* desactivation par un bouton EXPLICITE, pas par un champ vide */
  if ($('#sys-debug')) $('#sys-debug').checked = !!c.debug;
  if ($('#sys-txrpt')) $('#sys-txrpt').checked = !!c.tx_repeat_bit;
  if ($('#sys-txrmt')) $('#sys-txrmt').checked = !!c.tx_rmt;
  if ($('#sys-rxgain')) $('#sys-rxgain').value = c.rx_gain || 39;
  if ($('#sys-txte')) $('#sys-txte').value = c.tx_te || 400;
  const rp = $('#sys-radioprof'), fq = $('#sys-freq');
  if (rp && fq) {
    rp.value = c.radio_profile || 'profalux';
    fq.min = c.band_low_khz || 863000; fq.max = c.band_high_khz || 870000;
    fq.value = c.freq_custom_khz || c.freq_khz || '';
    fq.hidden = rp.value !== 'custom';
    const fh = $('#sys-freq-hint');
    if (fh) fh.textContent = `Fréquence active : ${c.freq_khz ? (c.freq_khz / 1000).toFixed(3) + ' MHz' : '?'}` +
      (rp.value === 'custom' ? ` (saisie en kHz, entre ${fq.min} et ${fq.max})` : '');
  }
  if ($('#mqtt-device')) $('#mqtt-device').value = c.device || '';
  refreshRadioReminder();
  const st = await api('/api/ota/status').catch(() => ({}));
  $('#ota-version').textContent = st.version || '…';
  if ($('#ota-auto')) $('#ota-auto').checked = c.update_auto !== 0;   /* absent (ancien firmware) = active */
}
if ($('#ota-auto')) $('#ota-auto').onchange = async (e) => {
  const on = e.target.checked;
  const r = await api('/api/config', { method: 'POST', body: JSON.stringify({ update_auto: on }) }).catch(() => null);
  if (r && r.ok) toast(on ? 'Vérification automatique activée' : 'Vérification automatique désactivée');
  else { e.target.checked = !on; toast('Échec de l\'enregistrement'); }
};
if ($('#wifi-device')) $('#wifi-device').oninput = refreshWifiUrl;
$('#wifi-save').onclick = async () => {
  const b = { wifi_ssid: $('#wifi-ssid').value.trim(), reboot: $('#wifi-reboot').checked };
  if ($('#wifi-pass').value) b.wifi_pass = $('#wifi-pass').value;
  if ($('#wifi-device')) b.device = $('#wifi-device').value.trim();   /* nom du boitier = son adresse */
  const url = boxUrl(b.device);
  await api('/api/config', { method: 'POST', body: JSON.stringify(b) }).catch(() => {});
  b.reboot ? toast('Wi-Fi enregistré, redémarrage…') : savedBtn($('#wifi-save'), 'Enregistrer le Wi-Fi');
  /* Le portail (point d'acces) disparait au redemarrage : dire ou retrouver le boitier. */
  const nx = $('#wifi-next');
  if (nx && b.reboot) {
    nx.hidden = false;
    nx.querySelector('b').innerHTML = `Après le redémarrage, reconnecte-toi à « ${esc(b.wifi_ssid)} » et ouvre `
      + `<a href="${esc(url)}/">${esc(url)}</a>. `
      + `Sur Android, si l'adresse ne répond pas, utilise l'adresse IP du boîtier indiquée par ta box internet.`;
  }
};
$('#mqtt-save').onclick = async () => {
  const b = { mqtt_uri: $('#mqtt-uri').value.trim(), mqtt_user: $('#mqtt-user').value.trim(), reboot: true };
  if ($('#mqtt-pass').value) b.mqtt_pass = $('#mqtt-pass').value;
  /* Certificats TLS : champ rempli = remplace, case « effacer » cochée = envoie "" (supprime en
   * NVS cote firmware), sinon inchangé (comme le mot de passe). */
  const certField = (id, key) => {
    const el = $('#' + id), clr = $('#' + id + '-clear');
    if (clr && clr.checked) b[key] = '';
    else if (el && el.value.trim()) b[key] = el.value.trim();
  };
  certField('mqtt-ca', 'mqtt_ca');
  certField('mqtt-cert', 'mqtt_cert');
  certField('mqtt-key', 'mqtt_key');
  if ($('#mqtt-device')) b.device = $('#mqtt-device').value.trim();
  try {
    await api('/api/config', { method: 'POST', body: JSON.stringify(b) });
    toast('MQTT enregistré, redémarrage…');
  } catch (e) {
    /* 400 = corps rejeté (certificats trop volumineux) ; sinon erreur générique. On NE dit
     * PAS « enregistré » sur un échec, sinon l'utilisateur croit ses certificats pris. */
    toast(e === 400 ? 'Échec : certificats trop volumineux ou requête invalide' : 'Échec de l’enregistrement MQTT');
  }
};
if ($('#sys-radioprof')) $('#sys-radioprof').onchange = () => { $('#sys-freq').hidden = $('#sys-radioprof').value !== 'custom'; };
$('#sys-save').onclick = async () => {
  const b = { device: $('#sys-device').value.trim(), log_frames: $('#sys-logframes').checked, debug: $('#sys-debug').checked, rx_gain: Number($('#sys-rxgain').value), tx_te: Number($('#sys-txte').value) || 400, reboot: $('#sys-reboot').checked };
  if ($('#sys-txrpt')) b.tx_repeat_bit = $('#sys-txrpt').checked;
  if ($('#sys-txrmt')) b.tx_rmt = $('#sys-txrmt').checked;
  /* ui_pass n'est envoye QUE si le champ est REELLEMENT rempli. Un champ vide ne
     desactive rien (ce serait ambigu : il est vide par defaut, puisqu'on ne reaffiche
     jamais le mot de passe) -> la desactivation passe par le bouton dedie ci-dessous. */
  /* Profil radio : le firmware refuse toute la requete (400) si la frequence sort de la plage
     de la carte. On le verifie ici pour un message clair, et on ne dit pas « enregistré » sur
     un refus. */
  const rp = $('#sys-radioprof'), fq = $('#sys-freq');
  if (rp) {
    b.radio_profile = rp.value;
    if (rp.value === 'custom') {
      const f = Number(fq.value);
      if (!(f >= Number(fq.min) && f <= Number(fq.max))) { toast(`Fréquence invalide : entre ${fq.min} et ${fq.max} kHz`); return; }
      b.freq_khz = f;
    }
  }
  const up = $('#sys-uipass');
  const newPass = up && up.value !== '';
  if (newPass) b.ui_pass = up.value;
  let res;
  try { res = await api('/api/config', { method: 'POST', body: JSON.stringify(b) }); }
  catch (e) { toast(e === 400 ? 'Refusé : profil radio ou fréquence invalide, rien n’a été enregistré' : 'Échec de l’enregistrement'); return; }
  if (res && res.radio_applied === 0) toast('Enregistré, mais la radio n’a pas pris la nouvelle fréquence (voir Diagnostic)');
  if (up) up.value = '';
  if (newPass) {
    /* Le firmware invalide toutes les sessions quand le mot de passe change :
       il faut donc se reconnecter. On affiche l'ecran de login DIRECTEMENT au
       lieu de recharger : un reload conservait le fragment d'onglet (#sys) et
       rechargeait toute l'UI pour rien. */
    toast('Mot de passe enregistré, reconnexion…');
    setTimeout(() => {
      if (history.replaceState) history.replaceState(null, '', location.pathname);
      const l = $('#login');
      if (l) { l.hidden = false; const p = $('#login-pass'); if (p) { p.value = ''; p.focus(); } }
    }, 1000);
    return;
  }
  b.reboot ? toast('Enregistré, redémarrage…') : savedBtn($('#sys-save'), 'Enregistrer');
  loadConfig().catch(() => {});
};
/* Desactivation EXPLICITE : envoie ui_pass:"" (le firmware efface alors la cle NVS). */
if ($('#sys-uipass-clear')) $('#sys-uipass-clear').onclick = async () => {
  if (!confirm('Retirer le mot de passe ?\n\nL\'interface et l\'API redeviendront accessibles à tout appareil du réseau, y compris pour envoyer un firmware.')) return;
  await api('/api/config', { method: 'POST', body: JSON.stringify({ ui_pass: '' }) }).catch(() => {});
  /* Le firmware a invalide toutes les sessions (y compris la notre) en changeant
     le mot de passe. Sans protection il n'en faut plus, mais l'ecran de login
     pouvait rester affiche par-dessus une UI pourtant accessible : boot() le
     masque et remet a jour le bouton de deconnexion. */
  toast('Protection retirée : interface ouverte');
  boot();
  loadConfig().catch(() => {});
};
const hex2 = n => '0x' + Number(n).toString(16).toUpperCase().padStart(2, '0');
if ($('#rxcal-btn')) $('#rxcal-btn').onclick = async () => {
  const st = $('#rxcal-status'); const btn = $('#rxcal-btn');
  btn.disabled = true; st.textContent = 'Appuie sur ta télécommande maintenant…';
  await api('/api/rx/calibrate', { method: 'POST' }).catch(() => {});
  const poll = setInterval(async () => {
    const s = await api('/api/rx/calibrate').catch(() => null);
    if (!s) return;
    if (s.state === 1) { st.textContent = `Appuie sur ta télécommande… (test ${hex2(s.testing)})`; return; }
    clearInterval(poll); btn.disabled = false;
    if (s.state === 2) {
      st.textContent = `✓ Calibré : gain ${hex2(s.result)} (enregistré)`;
      if ($('#sys-rxgain')) $('#sys-rxgain').value = String(s.result);
      await api('/api/config', { method: 'POST', body: JSON.stringify({ rx_gain: s.result }) }).catch(() => {});
    } else {
      st.textContent = '✗ Aucune trame captée. Vérifie l\'antenne, ou essaie un autre module CC1101.';
    }
  }, 1000);
};
async function loadDiag() {
  const vd = $('#diag-verdict'), el = $('#diag-line'); if (!vd && !el) return;
  const d = await api('/api/diag').catch(() => null);
  if (!d) { if (vd) vd.querySelector('b').textContent = 'Diagnostic indisponible'; if (el) el.textContent = ''; return; }
  const ph = '0x' + Number(d.partnum).toString(16).toUpperCase().padStart(2, '0');
  const vh = '0x' + Number(d.version).toString(16).toUpperCase().padStart(2, '0');
  const chipOk = (d.partnum === 0 && d.version !== 0 && d.version !== 255);
  const txOk = d.tx_ok === 1;
  if (vd) {
    let cls, msg;
    if (!chipOk) { cls = 'bad'; msg = '🔴 Module NON détecté (câblage / SPI). VERSION=' + vh; }
    else if (d.spi_fails > 0) { cls = 'bad'; msg = '🔴 Réglages radio non pris par le module : couper l\'alimentation puis redémarrer'; }
    else if (!txOk) { cls = 'bad'; msg = '🔴 Détecté mais émission KO (TX HS)'; }
    else if (d.gdo0 === 0) { cls = 'bad'; msg = '🔴 GDO0 tenu par un autre composant : l\'émission n\'atteint pas le module'; }
    else { cls = 'ok'; msg = '🟢 Module OK (détecté + émission)'; }
    vd.className = 'statline ' + cls; vd.querySelector('b').textContent = msg;
  }
  /* Ecritures SPI reessayees / jamais prises : un module sain reste a 0. Des reprises qui
     montent signalent un module qui decroche (il a deja ete vu chaud et instable). */
  const spi = (d.spi_retries || d.spi_fails) ? ` · SPI : ${d.spi_retries} reprise(s), ${d.spi_fails} échec(s)` : '';
  /* GDO0 (issue #60) : libre si la broche suit les tirages internes, sinon un autre composant la tient */
  const gdo0 = d.gdo0 === 1 ? ' · GDO0 libre ✓'
             : d.gdo0 === 0 ? ` · GDO0 tenu par un autre composant (bloqué à ${d.gdo0_lvl}) ✗` : '';
  if (el) el.innerHTML = `PARTNUM ${ph} · VERSION ${vh} · TX ${txOk ? 'OK ✓' : (d.tx_ok === 0 ? 'HS ✗' : 'non testé')}${gdo0}${spi}. Réception : bouton ci-dessous.`;
}
if ($('#diag-tx-btn')) $('#diag-tx-btn').onclick = async () => {
  const btn = $('#diag-tx-btn'); btn.disabled = true;
  const vd = $('#diag-verdict'); if (vd) vd.querySelector('b').textContent = 'Test d\'émission en cours...';
  await api('/api/diag/tx', { method: 'POST' }).catch(() => {});
  await loadDiag(); btn.disabled = false;
};
if ($('#diag-rx-btn')) $('#diag-rx-btn').onclick = async () => {
  const btn = $('#diag-rx-btn'), st = $('#diag-rx-status');
  btn.disabled = true; setRadioBusy(true);
  st.className = 'hint'; st.textContent = '📡 Presse ta télécommande MAINTENANT (~15 s)...';
  await api('/api/learn/start', { method: 'POST' }).catch(() => {});
  const wait = ms => new Promise(r => setTimeout(r, ms));
  let got = null;
  for (let i = 0; i < 16; i++) { await wait(1000); const p = await api('/api/learn/poll').catch(() => null); if (p && p.serial && p.serial !== '0x0000000') { got = p; break; } }
  btn.disabled = false; setRadioBusy(false);
  if (got) st.innerHTML = `✅ Réception OK : serial <code>${esc(got.serial)}</code>, bouton 0x${esc(got.button)}, RSSI ${got.rssi} dBm. Module bien en 868 et qui reçoit.`;
  else st.innerHTML = '❌ Rien capté en 15 s. Si TX est OK mais RX ne capte jamais, c\'est souvent la <b>mauvaise fréquence (433 au lieu de 868)</b>. Vérifie aussi antenne, distance < 1 m, gain RX.';
};
loadDiag();
$('#mqtt-detect').onclick = async () => {
  const b = $('#mqtt-detect'); b.textContent = 'Recherche…'; b.disabled = true;
  const r = await api('/api/mqtt/discover').catch(() => ({}));
  if (r.uri) { $('#mqtt-uri').value = r.uri; b.textContent = 'Trouvé : ' + r.uri; }
  else b.textContent = 'Aucun broker trouvé';
  b.disabled = false;
  setTimeout(() => b.textContent = '🔍 Détecter le broker (mDNS)', 2500);
};

/* ── OTA ── */
$('#ota-btn').onclick = async () => {
  const f = $('#ota-file').files[0]; if (!f) return alert('Choisis un fichier .bin');
  if (/full/i.test(f.name) && !confirm(`« ${f.name} » ressemble à un binaire COMPLET (flash série à 0x0), pas à une image OTA - l'OTA le refusera. Prends plutôt le fichier « -ota.bin ». Continuer quand même ?`)) return;
  $('#ota-prog').hidden = false; $('#ota-btn').disabled = true; $('#ota-bar').value = 0;
  /* Le httpd de l'ESP est mono-tache : pendant l'upload il ne peut PAS repondre a /api/ota/status.
     On pilote donc la barre cote navigateur (octets reellement pousses vers l'ESP) via XHR. */
  const buf = await f.arrayBuffer();
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota/upload');
  xhr.upload.onprogress = (e) => {
    if (!e.lengthComputable) return;
    $('#ota-bar').value = Math.round(100 * e.loaded / e.total);
    $('#ota-msg').textContent = `envoi ${Math.round(e.loaded / 1024)} / ${Math.round(e.total / 1024)} Ko`;
  };
  xhr.onload = () => {
    const ok = xhr.status >= 200 && xhr.status < 300;
    $('#ota-bar').value = ok ? 100 : $('#ota-bar').value;
    $('#ota-msg').textContent = ok ? '✓ reçu - vérification + redémarrage (~5 s)…' : `✗ échec (HTTP ${xhr.status})`;
    if (!ok) $('#ota-btn').disabled = false;
  };
  xhr.onerror = () => { $('#ota-msg').textContent = '✗ échec de l\'envoi (lien coupé ?)'; $('#ota-btn').disabled = false; };
  xhr.send(buf);
};
$('#ota-rollback').onclick = async () => {
  if (!confirm('Revenir à la version précédente et redémarrer ?')) return;
  await api('/api/ota/rollback', { method: 'POST' }).catch(() => {});
};
/* OTA depuis GitHub (la dernière release publique) */
let githubUrl = null;
$('#ota-check').onclick = async () => {
  /* C'est le BOITIER qui interroge GitHub (pas le navigateur) : on lui demande une
     verification, puis on attend qu'elle se termine (compteur checks de /api/ota/status). */
  const span = $('#ota-latest'); span.textContent = '⏳ Le boîtier interroge GitHub…'; $('#ota-github').hidden = true;
  const r = await api('/api/ota/check', { method: 'POST' }).catch(() => null);
  if (!r || !r.ok) { span.textContent = '❌ Le boîtier ne répond pas'; return; }
  let st = null;
  for (let i = 0; i < 25; i++) {   /* ~25 s max : la requete GitHub a 10 s de delai */
    await new Promise(res => setTimeout(res, 1000));
    st = await api('/api/ota/status').catch(() => null);
    if (st && st.checks > r.checks) break;
    st = null;
  }
  if (!st) { span.textContent = '❌ Pas de réponse de GitHub (Internet ?)'; return; }
  const latest = (st.latest || '').replace(/^v/, '');
  if (!latest) { span.textContent = '❌ Le boîtier n\'a pas pu lire la dernière version (Internet ? limite de l\'API GitHub ?)'; return; }
  const cur = (st.version || '').replace(/^v/, '');
  githubUrl = st.latest_url || null;   /* asset de CETTE carte : openprofalux-<carte>-ota.bin */
  const plus = newerThan(latest, cur);   /* jamais proposer une release PLUS ANCIENNE (version de dev) */
  if (githubUrl && plus) $('#ota-github').hidden = false;
  span.textContent = plus ? `⚠️ Installée ${cur}, disponible ${latest}`
    : cur === latest ? `✅ ${cur} = dernière version` : `✅ Installée ${cur}, dernière release ${latest}`;
  updateBadge(st);
};
/* "0.2.12" plus recente que "0.2.11" ? Comparaison numerique ; suffixe ("-dev") ignore. */
function newerThan(a, b) {
  const p = v => ((String(v || '').replace(/^v/, '').match(/^\d+(?:\.\d+)*/) || [null])[0]);
  const x = p(a), y = p(b); if (!x || !y) return false;
  const xa = x.split('.').map(Number), ya = y.split('.').map(Number);
  for (let i = 0; i < Math.max(xa.length, ya.length); i++) { const d = (xa[i] || 0) - (ya[i] || 0); if (d) return d > 0; }
  return false;
}
/* Mise a jour disponible (verification GitHub faite par le boitier, toutes les 6 h) : pastille
 * sur l'onglet OTA, et dans l'onglet la ligne + le bouton d'installation, sans cliquer. */
function updateBadge(st) {
  const avail = !!(st && st.latest_url && newerThan(st.latest, st.version));
  const b = $('#ota-badge'); if (b) b.hidden = !avail;
  if (!avail) return;
  githubUrl = st.latest_url; $('#ota-github').hidden = false;
  $('#ota-latest').textContent = `⚠️ Installée ${String(st.version).replace(/^v/, '')}, disponible ${String(st.latest).replace(/^v/, '')}`;
}
async function checkUpdate() { updateBadge(await api('/api/ota/status').catch(() => null)); }
$('#ota-github').onclick = () => {
  if (!githubUrl) return;
  /* Confirmation IN-PAGE (pas d'alert natif) : on affiche un avertissement + 2 boutons. */
  $('#ota-prog').hidden = false; $('#ota-bar').value = 0;
  $('#ota-msg').innerHTML = `⚠️ Le boîtier va télécharger la mise à jour puis <b>redémarrer</b> (~30 s).
    <button id="ota-go" class="btn primary" style="margin-left:6px">Confirmer</button>
    <button id="ota-cancel" class="btn">Annuler</button>`;
  $('#ota-cancel').onclick = () => { $('#ota-prog').hidden = true; $('#ota-msg').textContent = ''; };
  $('#ota-go').onclick = () => {
    $('#ota-github').disabled = true; $('#ota-msg').textContent = 'téléchargement…';
    api('/api/ota/pull', { method: 'POST', body: JSON.stringify({ url: githubUrl }) }).catch(() => {});
    let done = false, misses = 0;
    const reboot = () => {   /* fin d'OTA : le boîtier redémarre -> on attend qu'il revienne et on recharge */
      if (done) return; done = true;
      $('#ota-bar').value = 100;
      let n = 15;
      const t = setInterval(async () => {
        const s = await api('/api/ota/status').catch(() => null);
        if (s || --n <= 0) { clearInterval(t); $('#ota-msg').textContent = '✅ Mise à jour installée - rechargement…'; setTimeout(() => location.reload(), 900); }
        else $('#ota-msg').textContent = `✅ Installée - redémarrage… reconnexion (${n})`;
      }, 1000);
    };
    const poll = setInterval(async () => {
      const s = await api('/api/ota/status').catch(() => null);
      if (s) {
        misses = 0;
        if (s.total) $('#ota-bar').value = Math.round(100 * s.written / s.total);
        $('#ota-msg').textContent = s.msg || 'en cours…';
        if (s.state === 99) { clearInterval(poll); $('#ota-msg').textContent = '✗ échec : ' + (s.msg || 'voir logs'); $('#ota-github').disabled = false; }
        else if (s.state === 4) { clearInterval(poll); reboot(); }   /* REBOOTING */
      } else if (++misses >= 3) { clearInterval(poll); reboot(); }   /* injoignable = redémarrage */
    }, 800);
  };
};

/* ── Restauration ── */
$('#restore-btn').onclick = async () => {
  const f = $('#restore-file').files[0];
  if (!f) return alert('Choisis un fichier de sauvegarde .json');
  if (!confirm('Remplacer toute la config actuelle par cette sauvegarde ?')) return;
  const text = await f.text();
  const r = await fetch('/api/restore', { method: 'POST', body: text }).then(x => x.json()).catch(() => ({}));
  restoreDone(r, 'Sauvegarde restaurée');
};
/* Restauration refusee : le boitier dit pourquoi. Toute restauration acceptee garde une
 * copie de la config qu'elle remplace (bouton "Revenir", aller-retour possible). */
const RESTORE_ERR = {
  format: "Ce fichier n'est pas une sauvegarde de config (l'export des trames ?). Rien n'a été modifié.",
  copie: "Copie de la config actuelle impossible : restauration annulée, rien n'a été modifié.",
  nvs: 'Config restaurée mais pas enregistrée (mémoire pleine) : elle sera perdue au redémarrage.',
  absente: "Aucune config d'avant à restaurer." };
function restoreDone(r, okMsg) {
  if (r.ok) { toast(okMsg); loadStatus(); } else alert(RESTORE_ERR[r.err] || 'Échec de la restauration');
  loadPrevCfg();
}
async function loadPrevCfg() {
  const el = $('#restore-prev'); if (!el) return;
  const p = await api('/api/restore/undo').catch(() => null);
  el.hidden = !(p && p.available); if (el.hidden) return;
  const when = p.t > 1600000000 ? ' du ' + new Date(p.t * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }) : '';
  el.innerHTML = `Config d'avant la dernière restauration${when} : <button id="restore-undo" class="btn" type="button">↩ Revenir à cette config</button>`;
  $('#restore-undo').onclick = async () => {
    if (!confirm("Revenir à la config d'avant la dernière restauration ? La config actuelle sera gardée à sa place.")) return;
    restoreDone(await fetch('/api/restore/undo', { method: 'POST' }).then(x => x.json()).catch(() => ({})), "Config d'avant restaurée");
  };
}

/* ── Statut global (polling) ── */
let statusCache = { volets: [], rf: [], remotes: {} };
function fillVoletPickers(volets) {
  const ids = (volets || []).filter(v => !v.central).map(v => v.id);   /* pas de calibration pour une centrale */
  const sel = $('#calib-id');
  if (sel) { const cur = sel.value; sel.innerHTML = ids.map(id => `<option>${esc(id)}</option>`).join(''); if (ids.includes(cur)) sel.value = cur; }
  const dl = $('#volet-list'); if (dl) dl.innerHTML = ids.map(id => `<option value="${esc(id)}">`).join('');
}
/* Adresse mDNS du boitier, comme le firmware la calcule (mdns_name.c) : nom choisi nettoye
 * (minuscules, a-z/0-9, autre caractere -> tiret, pas de tiret en tete/queue/double, 32 max),
 * sinon le nom fige au premier demarrage (mdns_default). */
let cfgCache = {};
function mdnsSanitize(n) {
  let o = '';
  for (const ch of (n || '').toLowerCase()) {
    if (o.length >= 32) break;
    if (/[a-z0-9]/.test(ch)) o += ch; else if (o && !o.endsWith('-')) o += '-';
  }
  return o.replace(/-+$/, '');
}
function boxHost(device) {
  const d = (device ?? cfgCache.device ?? '').trim();
  const dflt = !d || ['openprofalux', 'volet_test'].includes(d.toLowerCase());   /* comme mdns_name.c */
  return (!dflt && mdnsSanitize(d)) || cfgCache.mdns_default || cfgCache.mdns_host || 'openprofalux';
}
function boxUrl(device) { return `http://${boxHost(device)}.local`; }
function refreshWifiUrl() {
  const u = $('#wifi-url'); if (u) u.textContent = boxUrl($('#wifi-device') ? $('#wifi-device').value : undefined);
  wifiStatusLine(statusCache.wifi);
}
function wifiStatusLine(w) {
  const el = $('#wifi-status'); if (!el) return;
  if (w && w.connected) {
    el.className = 'statline ok';
    el.innerHTML = `<span class="dot"></span><b>Connecté</b> à « ${esc(w.ssid)} » ${sig(w.rssi)}<span class="r">${esc(w.ip || '')}</span>`;
  } else {
    el.className = 'statline bad';
    const dv = $('#wifi-device') ? $('#wifi-device').value : undefined;
    el.innerHTML = `<span class="dot"></span><b>Non connecté</b> : le boîtier est en point d'accès de configuration.`
      + ` Une fois sur ton Wi-Fi, il sera à l'adresse <b>${esc(boxUrl(dv))}</b>`;
  }
}
function mqttStatusLine(ok) {
  const el = $('#mqtt-status'); if (!el) return;
  el.className = 'statline ' + (ok ? 'ok' : 'bad');
  el.innerHTML = `<span class="dot"></span><b>${ok ? 'Connecté' : 'Déconnecté'}</b>${ok ? '' : '<span class="r">aucun broker</span>'}`;
}
function renderCentralMembers() {
  const box = $('#central-members'); if (!box) return;
  const checked = new Set($$('#central-members input:checked').map(c => c.value));   /* garde les coches au re-render (polling) */
  const vols = (statusCache.volets || []).filter(v => !v.central);
  box.innerHTML = vols.length
    ? vols.map(v => `<label class="chip" style="cursor:pointer"><input type="checkbox" value="${esc(v.id)}"${checked.has(v.id) ? ' checked' : ''} style="margin-right:5px">${esc(v.id)}</label>`).join('')
    : '<span class="hint">Aucun volet à grouper pour l\'instant.</span>';
}
function renderCentralsList() {
  const box = $('#central-list'); if (!box) return;
  const centrals = (statusCache.volets || []).filter(v => v.central);
  if (!centrals.length) { box.innerHTML = '<p class="hint">Aucune centrale pour l\'instant.</p>'; return; }
  box.innerHTML = '<p class="hint" style="margin-bottom:6px">Clique une centrale pour la modifier ci-dessous.</p>' + centrals.map(v =>
    `<div class="statline ce" data-id="${esc(v.id)}" style="cursor:pointer"><span class="dot"></span><b>🎛 ${esc(v.id)}</b> <span class="hint" style="margin-left:6px">${esc(v.members || 'aucun volet')}</span>
     <span class="r"><button class="btn danger cd" data-id="${esc(v.id)}" title="Supprimer" aria-label="Supprimer la centrale ${esc(v.id)}" style="padding:2px 9px">🗑</button></span></div>`).join('');
  box.querySelectorAll('.ce').forEach(row => row.onclick = () => {
    const v = (statusCache.volets || []).find(x => x.id === row.dataset.id); if (v) editCentral(v);
  });
  box.querySelectorAll('.cd').forEach(b => b.onclick = async (e) => {
    e.stopPropagation();
    if (!confirm(`Supprimer la centrale « ${b.dataset.id} » ? (les volets ne sont pas touchés)`)) return;
    await api('/api/volet/delete', { method: 'POST', body: JSON.stringify({ id: b.dataset.id }) }).catch(() => {});
    toast('Centrale supprimée'); await loadStatus();
  });
}
function editCentral(v) {
  location.hash = 'remotes/central';
  $('#central-name').value = v.id;
  renderCentralMembers();
  const members = (v.members || '').split(',').map(s => s.trim()).filter(Boolean);
  $$('#central-members input').forEach(c => { c.checked = members.includes(c.value); });
}
if ($('#central-cancel')) $('#central-cancel').onclick = () => {
  $('#central-name').value = ''; $$('#central-members input').forEach(c => c.checked = false);
};
if ($('#central-create')) $('#central-create').onclick = async () => {
  const name = ($('#central-name').value || '').trim();
  if (!name) return toast('Donne un nom à la centrale');
  if (isNewNameWithComma(name)) return toast(COMMA_MSG);
  const members = $$('#central-members input:checked').map(c => c.value);
  if (!members.length) return toast('Coche au moins un volet');
  const r = await api('/api/central', { method: 'POST', body: JSON.stringify({ id: name, members }) }).catch(() => null);
  if (r && r.ok) { toast('Centrale enregistrée'); $('#central-name').value = ''; $$('#central-members input').forEach(c => c.checked = false); await loadStatus(); }
  else toast('Échec de l\'enregistrement' + (r && r.err ? ' : ' + r.err : ''));
};
/* Rafraichissement de l'etat : une requete a la fois, abandonnee au bout de 4 s (sans delai,
 * elles s'empilaient pendant une coupure). Un echec ne vide plus l'interface : on garde la
 * derniere reponse, et apres 2 echecs de suite un bandeau dit que le boitier ne repond pas
 * (mise a jour, redemarrage, Wi-Fi). Un 401 reste l'affaire d'api() : ecran de connexion. */
let statusBusy = false, statusFails = 0, statusOkAt = 0, lastUptime = null;
function offline(n) {
  document.body.classList.toggle('offline', n >= 2);
  const bar = $('#offline-bar'); if (!bar) return;
  bar.hidden = n < 2;
  if (!bar.hidden) bar.textContent = 'Boîtier injoignable : nouvelle tentative toutes les 3 s.' +
    (statusOkAt ? ` Affiché : l'état de ${new Date(statusOkAt).toLocaleTimeString('fr-FR')}.` : '');
}
async function loadStatus() {
  if (statusBusy) return;
  statusBusy = true;
  const ctl = new AbortController(), timer = setTimeout(() => ctl.abort(), 4000);
  let s = null, err = null;
  try { s = await api('/api/status', { signal: ctl.signal }); } catch (e) { err = e; }
  clearTimeout(timer); statusBusy = false;
  if (!s) { if (err !== 401) offline(++statusFails); return; }
  if (lastUptime != null && s.uptime < lastUptime) toast('Le boîtier a redémarré');
  statusFails = 0; statusOkAt = Date.now(); lastUptime = s.uptime; offline(0);
  statusCache = s;
  const rf = s.rf || [];
  renderVolets(s.volets || [], rf);
  renderCentralMembers();
  renderCentralsList();
  renderRemotes(s, rf);
  fillVoletPickers(s.volets || []);
  if ((location.hash || '').includes('/calib') && !calibLive) fillCalib();   /* affiche les temps enregistres */
  if (!learning) { renderVoletPicker(); renderLearnSlots(); }
  wifiStatusLine(s.wifi); mqttStatusLine(s.mqtt);
  /* Mode secours (reseau configure mais muet, le boitier reessaie seul) : visible sur toutes
   * les pages, a cote du titre. */
  const fb = $('#hdr-fallback'); if (fb) fb.hidden = !(s.wifi && s.wifi.fallback);
  const ci = $('#calib-info'); if (ci) ci.hidden = !!s.listening;   /* bandeau visible seulement si option OFF */
}
/* Volet auquel une telecommande (serial) est rattachee, sinon null. */
function voletForSerial(s) { for (const v of (statusCache.volets || [])) if ((v.serials || []).includes(s)) return v.id; return null; }
/* Nom a afficher pour un serial : nom telecommande > nom volet > le serial brut. */
function frameName(s) { return (statusCache.remotes || {})[s] || voletForSerial(s) || s; }
/* Libelle du bouton (Haut/Bas/Stop) s'il correspond a une commande apprise d'un volet, sinon null. */
const BTN_LABEL = { up: 'Haut', down: 'Bas', stop: 'Stop' };
function buttonLabel(serial, buttonHex) {
  const b = parseInt(buttonHex, 16);
  for (const v of (statusCache.volets || [])) {
    if (!(v.serials || []).includes(serial)) continue;
    const c = v.cmd || {};
    for (const k of ['up', 'down', 'stop']) if (c[k] && c[k].b === b) return BTN_LABEL[k];
  }
  return null;
}
/* Une ligne de trame : heure reelle + nom cliquable (vers Telecommandes) + bouton nomme si connu. */
function rfRow(f) {
  const ts = f.t > 1600000000
    ? new Date(f.t * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit' })
    : '-';
  const name = frameName(f.serial);
  const vid = voletForSerial(f.serial);   /* volet rattache -> lien qui le selectionne dans l'apprentissage */
  const nameCell = vid
    ? `<a href="#remotes/learn" class="frame-link" data-volet="${esc(vid)}" title="${esc(f.serial)} - paramétrer ${esc(vid)}">${esc(name)}</a>`
    : (name !== f.serial
        ? `<a href="#remotes/learn" title="${esc(f.serial)} - Télécommandes">${esc(name)}</a>`
        : `<span class="m" title="télécommande non nommée">${esc(f.serial)}</span>`);
  const bl = buttonLabel(f.serial, f.button);
  const btnCell = bl ? `<span class="badge">${esc(bl)}</span>` : `<span class="badge m">0x${esc(f.button)}</span>`;
  return `<tr><td class="m">${ts}</td><td>${nameCell}</td>
    <td>${btnCell}</td><td class="m">0x${esc(f.hop)}</td><td class="m">${f.rssi} dBm</td>
    <td><button class="replay" title="Rejouer cette trame" aria-label="Rejouer cette trame" data-s="${esc(f.serial)}" data-h="${esc(f.hop)}">▶</button></td></tr>`;
}
/* Onglet RF : trames du ring, chargees par pages (scroll infini). Trie serveur par date. */
let rfOffset = 0, rfTotal = 0, rfLoading = false;
const RF_PAGE = 50;
function updateRfFooter() {
  const f = $('#rf-footer'); if (!f) return;
  f.textContent = rfTotal
    ? `${Math.min(rfOffset, rfTotal)} / ${rfTotal} trame(s)` + (rfOffset < rfTotal ? ' - défile pour charger la suite' : '')
    : 'Aucune trame captée.';
}
async function loadRf(reset) {
  const tb = $('#rf'); if (!tb || rfLoading) return;
  rfLoading = true;
  if (reset) rfOffset = 0;
  const d = await api(`/api/rf?offset=${rfOffset}&limit=${RF_PAGE}`).catch(() => null);
  rfLoading = false;
  if (!d || !Array.isArray(d.frames)) return;
  rfTotal = d.total || 0;
  const html = d.frames.map(rfRow).join('');
  if (reset) tb.innerHTML = html; else tb.insertAdjacentHTML('beforeend', html);
  rfOffset += d.frames.length;
  updateRfFooter();
}
/* Scroll infini : charge la page suivante quand on approche du bas du conteneur (hauteur limitee). */
{
  const wrap = $('.rf-scroll');
  if (wrap) wrap.addEventListener('scroll', () => {
    if (!rfLoading && rfOffset < rfTotal && wrap.scrollTop + wrap.clientHeight >= wrap.scrollHeight - 48) loadRf(false);
  });
}
/* Rejouer une trame captee : renvoie la trame brute (serial+hop) au boitier. Delegation (le tbody est re-rendu). */
const rfBody = $('#rf');
if (rfBody) rfBody.addEventListener('click', async (e) => {
  /* clic sur un nom rattache a un volet -> selectionne ce volet dans l'apprentissage (le href navigue). */
  const a = e.target.closest('a.frame-link');
  if (a) { activeVolet = a.dataset.volet; setTimeout(() => { renderVoletPicker(); renderLearnSlots(); }, 0); return; }
  const b = e.target.closest('button.replay'); if (!b) return;
  b.disabled = true;
  const r = await api('/api/rf/replay', { method: 'POST', body: JSON.stringify({ serial: b.dataset.s, hop: b.dataset.h }) }).catch(() => null);
  toast(r && r.ok ? 'Trame rejouée' : 'Échec du rejeu');
  setTimeout(() => { b.disabled = false; }, 800);
});
/* Demarrage de l'application. Appele soit directement (UI ouverte ou session
   deja valide), soit apres une connexion reussie. Garde anti-double-appel : le
   polling ne doit pas etre arme deux fois. */
let booted = false, pollTimer = null;
function boot() {
  hideLogin();              /* toujours : un rappel de boot() doit lever l'ecran de login */
  refreshAuthUi();
  loadStatus();
  if (!pollTimer) {   /* un seul timer, meme apres reconnexion ; rien tant que l'onglet est cache */
    pollTimer = setInterval(() => { if (!document.hidden) loadStatus(); }, 3000);
    checkUpdate(); setInterval(() => { if (!document.hidden) checkUpdate(); }, 30 * 60 * 1000);
    document.addEventListener('visibilitychange', () => { if (!document.hidden) loadStatus(); });
  }
  if (booted) return;       /* le routage ne s'arme qu'une fois */
  booted = true;
  applyRoute();
}

/* Affiche le bouton de deconnexion seulement si une protection est active. */
function refreshAuthUi() {
  const lo = $('#logout');
  if (lo) fetch('/api/session').then(r => r.json()).then(s => { lo.hidden = !s.protected; }).catch(() => {});
}

/* Au chargement : on demande l'etat d'authentification AVANT de lancer l'app,
   pour ne pas declencher une volee de requetes qui echoueraient toutes en 401. */
fetch('/api/session')
  .then(r => r.json())
  .then(s => { if (s.authed) boot(); else showLogin(); })
  .catch(() => boot());   /* firmware plus ancien (route absente) : comportement historique */
