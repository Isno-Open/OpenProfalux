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
  if (['sys', 'wifi', 'mqtt', 'ota'].includes(name)) loadConfig();
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
}
/* Rafraichit la 1re page quand l'onglet RF est actif ET qu'on n'a pas defile (sinon on garde la position). */
setInterval(() => { if ((location.hash || '').includes('/rf') && typeof loadRf === 'function' && rfOffset <= RF_PAGE) loadRf(true); }, 5000);
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
$$('.tab').forEach(t => t.onclick = () => { location.hash = t.dataset.t; });
$$('.subtab').forEach(t => t.onclick = () => {
  location.hash = `${t.closest('.panel').dataset.p}/${t.dataset.s}`;
});
addEventListener('hashchange', applyRoute);
$('#theme').onclick = () => {
  const r = document.documentElement;
  const dark = r.getAttribute('data-theme') === 'dark' ||
    (!r.getAttribute('data-theme') && matchMedia('(prefers-color-scheme:dark)').matches);
  r.setAttribute('data-theme', dark ? 'light' : 'dark');
};

/* ── Helpers ── */
function toast(m) { const t = $('#toast'); t.textContent = m; t.classList.add('show'); setTimeout(() => t.classList.remove('show'), 1600); }
function savedBtn(b, label) { const o = b.textContent; b.textContent = '✓ Enregistré'; setTimeout(() => b.textContent = label || o, 1500); }
const sigLvl = d => d >= -55 ? 4 : d >= -68 ? 3 : d >= -80 ? 2 : 1;
const sig = d => (d == null) ? '' :
  `<span class="sig l${sigLvl(d)}"><i></i><i></i><i></i><i></i></span><span class="dbm">${d} dBm</span>`;
/* RSSI le plus récent par serial, depuis la liste rf (déjà triée du + récent au + ancien) */
function rssiForSerials(serials, rf) { for (const f of rf) if ((serials || []).includes(f.serial)) return f.rssi; return null; }

/* ── Volets ── */
function renderVolets(list, rf) {
  const box = $('#volets'); box.innerHTML = '';
  $('#control-empty').hidden = list.length > 0;
  /* Position affichee UNIQUEMENT si l'ecoute permanente est active : sinon un coup de
   * vraie telecommande desynchronise l'estimation (pas de retour moteur) et un % faux
   * est pire qu'aucun %. */
  const showPos = !!statusCache.listening;
  const ordered = [...list].sort((a, b) => (b.central ? 1 : 0) - (a.central ? 1 : 0));   /* centrales en 1er */
  for (const v of ordered) {
    const r = rssiForSerials(v.serials, rf);
    const el = document.createElement('div');
    el.className = 'card volet';
    const isC = !!v.central;
    const showP = showPos && !isC;
    el.innerHTML = `
      <div class="top"><span class="name">${isC ? '🎛' : '🪟'} ${esc(v.id)}${isC ? ' <span class="badge">centrale</span>' : ''}</span>${showP ? `<span class="pct">${v.position ?? '?'}%</span>` : ''}</div>
      <div class="dpad"><button data-cmd="up">▲</button><button data-cmd="stop">■</button><button data-cmd="down">▼</button></div>
      ${showP ? `<div class="slat" style="--p:${v.position ?? 50}"></div>` : ''}
      <div class="serials">${isC
        ? 'volets : ' + (esc(v.members || '') || 'aucun') + ' <span class="hint">(gérer dans Télécommandes -> Centrale)</span>'
        : 'serials : ' + ((v.serials || []).map(s => `<code>${esc(remoteName(s))}</code>`).join(' ') || 'aucun') + (r != null ? `<span style="margin-left:6px">· reçu ${sig(r)}</span>` : '')}</div>`;
    el.querySelectorAll('[data-cmd]').forEach(b =>
      b.onclick = () => sendCmd({ id: v.id, cmd: b.dataset.cmd }, b));
    const slat = el.querySelector('.slat');
    if (slat) slat.onclick = e => {
      const p = Math.round(100 * (e.offsetX / e.currentTarget.offsetWidth));
      sendCmd({ id: v.id, cmd: 'pos', value: p });
    };
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

function renderLearnSlots() {
  const box = $('#learn-slots'); if (!box) return;
  const id = activeVolet;
  const v = (statusCache.volets || []).find(x => x.id === id);
  if (v && v.central) {   // une centrale n'a pas de télécommande propre
    box.innerHTML = `<div class="statline ok"><span class="dot"></span><b>C'est une centrale</b> (groupe de volets) - pas d'apprentissage. Gère ses membres dans <b>Volets → Créer une centrale</b>.</div>`;
    return;
  }
  if (v && v.virt) {   // volet enrôlé (télécommande virtuelle) -> pas d'apprentissage/clonage
    box.innerHTML = `<div class="statline ok"><span class="dot"></span><b>Ce volet a une télécommande virtuelle.</b> Pas de clonage nécessaire - il est piloté par génération. Gère-le dans <b>Créer une télécommande</b>.</div>`;
    return;
  }
  const cmd = (v && v.cmd) || {};
  box.innerHTML = '';
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
    serialEl.innerHTML = sers.length > 1
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
  if (hint) hint.textContent = id
    ? `Volet « ${id} » : clique Capturer, puis appuie une fois sur le bouton de ta télécommande (< 1 m du boîtier).`
    : 'Choisis un volet ci-dessus (ou crée-en un) pour activer la capture.';
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

async function captureAction(action, btn) {
  const id = activeVolet;
  if (!id) { toast('Choisis d’abord un volet'); return; }
  const label = LEARN_ACTIONS.find(x => x.a === action).lbl;
  learning = true; renderLearnSlots();
  btn.disabled = true; btn.textContent = `⏳ Appuie sur ${label}…`;
  await api('/api/learn/start', { method: 'POST', body: JSON.stringify({ action }) });
  const t0 = Date.now();
  const poll = setInterval(async () => {
    const r = await api('/api/learn/poll').catch(() => null);
    if (r && r.bits) {
      clearInterval(poll); learning = false;
      /* meme volet = meme telecommande : refuse une trame d'un autre serial que
       * celui deja appris (evite un stop d'une telecommande + montee d'une autre). */
      const vol = (statusCache.volets || []).find(x => x.id === id);
      const known = (vol && vol.serials) || [];
      if (known.length && !known.includes(r.serial)) {
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
      toast(`Rien capté pour ${label}, rapproche-toi et réessaie`);
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
function renderRemotes(map, rf) {
  const box = $('#remotes-list'); if (!box) return;
  const entries = Object.entries(map || {});
  $('#remotes-empty').hidden = entries.length > 0;
  box.innerHTML = '';
  for (const [serial, name] of entries) {
    const r = rssiForSerials([serial], rf);
    const row = document.createElement('div'); row.className = 'row'; row.style.alignItems = 'center';
    row.innerHTML = `<code class="badge">${esc(serial)}</code>
      <input style="flex:1" value="${esc(name)}" placeholder="nom…">${sig(r)}`;
    const inp = row.querySelector('input');
    inp.onchange = () => api('/api/remote', { method: 'POST', body: JSON.stringify({ serial, name: inp.value.trim() }) });
    box.appendChild(row);
  }
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
    'Le volet fait un va-et-vient = enrôlé ✅. Teste ▲ ■ ▼ ci-dessous.',
  ],
  R7: [
    'Monte → descends ~4 lames → remonte (procédure proche, à confirmer).',
    'Va-et-vient = enrôlé. Teste ▲ ■ ▼. Rapporte le résultat sur GitHub.',
  ],
  R8: [
    'Monte → descends ~4 lames → remonte (NeoSol : à confirmer, compteur roulant).',
    'Va-et-vient = enrôlé. Teste ▲ ■ ▼. Rapporte le résultat sur GitHub.',
  ],
};
let pfxModels = [];
async function loadPfx() {
  const d = await api('/api/pfx').catch(() => null);
  if (!d) return;
  pfxModels = d.models || [];
  const sel = $('#pfx-model');
  if (sel && !sel.dataset.filled && pfxModels.length) {
    sel.innerHTML = pfxModels.map((m, i) => `<option value="${i}">${esc(m.name)} · TE ${m.te}µs · ${esc(m.routine)}</option>`).join('');
    sel.dataset.filled = '1';
  }
  const box = $('#pfx-ident'), steps = $('#pfx-steps');
  if (!box) return;
  if (d.active) {
    box.hidden = false; steps.hidden = false; box.className = 'statline ok';
    box.querySelector('b').textContent = `Identité ${d.serial} · slot ${d.idx + 1}/63 · compteur ${d.counter}`
      + (d.remote && d.remote !== '0x0000000' ? ` · télécommande ${d.remote}` : '');
    if (sel) sel.value = String(d.model);
    const ol = $('#pfx-gestures'); const m = pfxModels[d.model];
    if (ol && m) ol.innerHTML = (PFX_GESTURES[m.routine] || PFX_GESTURES.R6).map(s => `<li>${s}</li>`).join('');
  } else { box.hidden = true; steps.hidden = true; }
}
function pfxResetSteps() {   // nouvelle identité -> on recache les gestes jusqu'à la prochaine émission
  const after = $('#pfx-after-emit'); if (after) after.hidden = true;
  const st = $('#pfx-learn-status'); if (st) st.hidden = true;
}
function pfxCapStatus(txt, cls) {
  const st = $('#pfx-capture-status'); if (!st) return;
  st.hidden = false; st.className = 'statline' + (cls ? ' ' + cls : '');
  st.querySelector('b').textContent = txt;
}
if ($('#pfx-capture')) $('#pfx-capture').onclick = async () => {
  const b = $('#pfx-capture'), o = b.textContent;
  const model = Number($('#pfx-model').value || 0);
  const wait = ms => new Promise(r => setTimeout(r, ms));
  b.disabled = true; setRadioBusy(true);
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
  else if (d.rc === 0) pfxCapStatus(`✅ Moteur déjà connu - même identité ${d.serial} (télécommande ${d.remote})`, 'ok');
  else if (d.rc === -1) pfxCapStatus('⚠️ 63 identités déjà attribuées : oublie-en une pour libérer un slot.', 'bad');
  else if (d.rc === -3) pfxCapStatus("⏱ Aucune trame captée - réessaie en appuyant sur la télécommande.", 'bad');
  else pfxCapStatus('Télécommande non reconnue', 'bad');
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
      pfxStatus('✅ Émission finie - fais les gestes ci-dessous MAINTENANT.', 'ok');
      if (after) after.hidden = false;                     // révèle les gestes SEULEMENT à la fin
    } else pfxStatus('Échec (radio occupée ?)', 'bad');
  }
  catch (e) { clearInterval(timer); pfxStatus('Radio occupée, réessaie dans un instant.', 'bad'); }
  finally { b.disabled = false; b.textContent = o; setRadioBusy(false); await loadPfx(); }
};
async function pfxCmd(cmd, btn) {
  if (radioBusy) return;
  setRadioBusy(true); if (btn) btn.classList.add('sending');
  try { await api('/api/pfx/cmd', { method: 'POST', body: JSON.stringify({ cmd }) }); }
  catch (e) { toast('Radio occupée, réessaie dans un instant'); }
  finally { if (btn) btn.classList.remove('sending'); setRadioBusy(false); loadPfx(); }
}
if ($('#pfx-up')) $('#pfx-up').onclick = () => pfxCmd('up', $('#pfx-up'));
if ($('#pfx-stop')) $('#pfx-stop').onclick = () => pfxCmd('stop', $('#pfx-stop'));
if ($('#pfx-down')) $('#pfx-down').onclick = () => pfxCmd('down', $('#pfx-down'));
if ($('#pfx-save-volet')) $('#pfx-save-volet').onclick = async () => {
  const name = ($('#pfx-volet-name').value || '').trim();
  if (!name) { toast('Donne un nom au volet'); return; }
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

/* ── Config : Wi-Fi / MQTT / Système (sauvegardes séparées, partielles) ── */
async function loadConfig() {
  const c = await api('/api/config').catch(() => ({}));
  $('#wifi-ssid').value = c.wifi_ssid || '';
  $('#mqtt-uri').value = c.mqtt_uri || ''; $('#mqtt-user').value = c.mqtt_user || '';
  if ($('#mqtt-pass')) $('#mqtt-pass').placeholder = c.mqtt_pass_len
    ? `•••••••• (${c.mqtt_pass_len} car. enregistrés, laisser vide pour ne pas changer)`
    : 'mot de passe du broker';
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
  if ($('#sys-rxgain')) $('#sys-rxgain').value = c.rx_gain || 39;
  if ($('#sys-txte')) $('#sys-txte').value = c.tx_te || 455;
  if ($('#mqtt-device')) $('#mqtt-device').value = c.device || '';
  const st = await api('/api/ota/status').catch(() => ({}));
  $('#ota-version').textContent = st.version || '…';
  if ($('#version')) $('#version').textContent = st.version ? 'v' + st.version : '…';
}
/* Version du header. Appelee par boot(), donc APRES ouverture de session : en
   appel immediat au chargement du script, la requete partait avant le login et
   echouait en 401 -> la version restait vide jusqu'a l'ouverture d'un onglet. */
async function loadVersion() {
  try { const s = await api('/api/ota/status'); if (s && s.version && $('#version')) $('#version').textContent = 'v' + s.version; }
  catch (e) {}
}
$('#wifi-save').onclick = async () => {
  const b = { wifi_ssid: $('#wifi-ssid').value.trim(), reboot: $('#wifi-reboot').checked };
  if ($('#wifi-pass').value) b.wifi_pass = $('#wifi-pass').value;
  await api('/api/config', { method: 'POST', body: JSON.stringify(b) }).catch(() => {});
  b.reboot ? toast('Wi-Fi enregistré, redémarrage…') : savedBtn($('#wifi-save'), 'Enregistrer le Wi-Fi');
};
$('#mqtt-save').onclick = async () => {
  const b = { mqtt_uri: $('#mqtt-uri').value.trim(), mqtt_user: $('#mqtt-user').value.trim(), reboot: true };
  if ($('#mqtt-pass').value) b.mqtt_pass = $('#mqtt-pass').value;
  if ($('#mqtt-device')) b.device = $('#mqtt-device').value.trim();
  await api('/api/config', { method: 'POST', body: JSON.stringify(b) }).catch(() => {});
  toast('MQTT enregistré, redémarrage…');
};
$('#sys-save').onclick = async () => {
  const b = { device: $('#sys-device').value.trim(), log_frames: $('#sys-logframes').checked, debug: $('#sys-debug').checked, rx_gain: Number($('#sys-rxgain').value), tx_te: Number($('#sys-txte').value) || 455, reboot: $('#sys-reboot').checked };
  /* ui_pass n'est envoye QUE si le champ est REELLEMENT rempli. Un champ vide ne
     desactive rien (ce serait ambigu : il est vide par defaut, puisqu'on ne reaffiche
     jamais le mot de passe) -> la desactivation passe par le bouton dedie ci-dessous. */
  const up = $('#sys-uipass');
  const newPass = up && up.value !== '';
  if (newPass) b.ui_pass = up.value;
  await api('/api/config', { method: 'POST', body: JSON.stringify(b) }).catch(() => {});
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
    else if (!txOk) { cls = 'bad'; msg = '🔴 Détecté mais émission KO (TX HS)'; }
    else { cls = 'ok'; msg = '🟢 Module OK (détecté + émission)'; }
    vd.className = 'statline ' + cls; vd.querySelector('b').textContent = msg;
  }
  if (el) el.innerHTML = `PARTNUM ${ph} · VERSION ${vh} · TX ${txOk ? 'OK ✓' : (d.tx_ok === 0 ? 'HS ✗' : 'non testé')}. Réception : bouton ci-dessous.`;
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
  const span = $('#ota-latest'); span.textContent = '⏳ Interrogation de GitHub…'; $('#ota-github').hidden = true;
  try {
    /* variante de CETTE carte -> nom d'asset attendu (jamais le -full.bin, invalide en OTA) */
    const stt = await api('/api/ota/status').catch(() => ({}));
    const variant = stt.target === 'm5stack_atom' ? 'atom' : (stt.target === 'external' ? 'devkit' : null);
    const r = await fetch('https://api.github.com/repos/Shad107/OpenProfalux/releases/latest');
    if (!r.ok) { span.textContent = r.status === 404 ? '❌ Aucune release publique (dépôt privé ?)' : `❌ HTTP ${r.status}`; return; }
    const j = await r.json();
    const latest = (j.tag_name || j.name || '?').replace(/^v/, '');   /* "v0.1.1" -> "0.1.1" */
    const cur = ($('#ota-version').textContent || '').replace(/^v/, '');
    githubUrl = variant
      ? (j.assets || []).map(a => a.browser_download_url).find(u => u.endsWith(`openprofalux-${variant}-ota.bin`))
      : null;
    if (githubUrl) $('#ota-github').hidden = false;
    span.textContent = (cur && cur === latest) ? `✅ ${cur} = dernière version`
      : !variant ? `⚠️ Disponible ${latest} - variante inconnue, utilise le flash manuel`
      : `⚠️ Installée ${cur}, disponible ${latest}${githubUrl ? '' : ' (pas d’asset OTA pour cette variante)'}`;
  } catch { span.textContent = '❌ Pas d’accès Internet ?'; }
};
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
  if (r.ok) { toast('Sauvegarde restaurée'); loadStatus(); } else alert('Échec de la restauration (fichier invalide ?)');
};

/* ── Statut global (polling) ── */
let statusCache = { volets: [], rf: [], remotes: {} };
function fillVoletPickers(volets) {
  const ids = (volets || []).filter(v => !v.central).map(v => v.id);   /* pas de calibration pour une centrale */
  const sel = $('#calib-id');
  if (sel) { const cur = sel.value; sel.innerHTML = ids.map(id => `<option>${esc(id)}</option>`).join(''); if (ids.includes(cur)) sel.value = cur; }
  const dl = $('#volet-list'); if (dl) dl.innerHTML = ids.map(id => `<option value="${esc(id)}">`).join('');
}
function wifiStatusLine(w) {
  const el = $('#wifi-status'); if (!el) return;
  if (w && w.connected) {
    el.className = 'statline ok';
    el.innerHTML = `<span class="dot"></span><b>Connecté</b> à « ${esc(w.ssid)} » ${sig(w.rssi)}<span class="r">${esc(w.ip || '')}</span>`;
  } else {
    el.className = 'statline bad';
    el.innerHTML = `<span class="dot"></span><b>Non connecté</b> : le boîtier est en point d'accès de configuration`;
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
     <span class="r"><button class="btn danger cd" data-id="${esc(v.id)}" style="padding:2px 9px">🗑</button></span></div>`).join('');
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
  const members = $$('#central-members input:checked').map(c => c.value);
  if (!members.length) return toast('Coche au moins un volet');
  const r = await api('/api/central', { method: 'POST', body: JSON.stringify({ id: name, members }) }).catch(() => null);
  if (r && r.ok) { toast('Centrale enregistrée'); $('#central-name').value = ''; $$('#central-members input').forEach(c => c.checked = false); await loadStatus(); }
  else toast('Échec de l\'enregistrement');
};
async function loadStatus() {
  const s = await api('/api/status').catch(() => ({ volets: [], rf: [], remotes: {} }));
  statusCache = s;
  const rf = s.rf || [];
  renderVolets(s.volets || [], rf);
  renderCentralMembers();
  renderCentralsList();
  fillVoletPickers(s.volets || []);
  if ((location.hash || '').includes('/calib') && !calibLive) fillCalib();   /* affiche les temps enregistres */
  if (!learning) { renderVoletPicker(); renderLearnSlots(); }
  wifiStatusLine(s.wifi); mqttStatusLine(s.mqtt);
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
    <td><button class="replay" title="Rejouer cette trame" data-s="${esc(f.serial)}" data-h="${esc(f.hop)}">▶</button></td></tr>`;
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
  loadVersion();            /* version du header : apres session, sinon 401 */
  loadStatus();
  if (!pollTimer) pollTimer = setInterval(loadStatus, 3000);   /* un seul timer, meme apres reconnexion */
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
