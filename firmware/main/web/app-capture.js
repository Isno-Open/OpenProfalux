/* UI "Capture brute / rejeu" (issue #95) : fichier ISOLE, charge apres app.js. Self-contained
 * (fetch nu, auth par cookie de session comme app.js) pour ne pas dependre de son scope. Rempli
 * dans #capture-root. Appelle /api/capture/* (POST start ?radio=N, GET last, POST replay) et
 * /api/radios (liste des radios declarees par la carte). */
(function () {
  const root = document.getElementById('capture-root');
  if (!root) return;
  const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const getj = (u, o) => fetch(u, o).then(r => r.json()).catch(() => null);

  root.innerHTML =
    '<h2>Capture brute / rejeu</h2>' +
    '<p class="hint">Enregistre le signal brut d\'une télécommande OOK à <b>code fixe</b> ' +
    '(portail CAME, capteur, télécommande cheap) que le firmware ne décode pas, et le rejoue à ' +
    'l\'identique. Choisis la bande, puis presse ta télécommande pendant la capture. Les codes ' +
    '<b>tournants</b> (Somfy RTS, Profalux) ne se rejouent pas ainsi.</p>' +
    '<div class="row" id="cap-btns"></div>' +
    '<p class="hint" id="cap-status"></p>' +
    '<div class="row" id="cap-replay-row" hidden><button id="cap-replay" class="btn primary" type="button">▶ Rejouer la dernière capture</button></div>';

  const btns = root.querySelector('#cap-btns');
  const status = root.querySelector('#cap-status');
  const replayRow = root.querySelector('#cap-replay-row');
  const replayBtn = root.querySelector('#cap-replay');

  const setBusy = b => { btns.querySelectorAll('button').forEach(x => x.disabled = b); replayBtn.disabled = b; };

  async function refreshLast() {
    const c = await getj('/api/capture/last');
    if (!c || !c.n) { replayRow.hidden = true; return; }
    status.className = 'hint';
    status.innerHTML = '✅ Capturé : <b>' + c.n + ' impulsions</b> sur la radio ' + esc(c.band) +
      ' MHz à ' + (c.freq_khz / 1000).toFixed(3) + ' MHz, RSSI ' + c.rssi + ' dBm. Tu peux rejouer.';
    replayRow.hidden = false;
  }

  async function doCapture(radio, band) {
    setBusy(true);
    status.className = 'hint';
    status.textContent = '📡 Presse ta télécommande ' + (band ? band + ' MHz ' : '') + 'MAINTENANT (~15 s)…';
    const res = await getj('/api/capture/start?radio=' + radio, { method: 'POST' });
    setBusy(false);
    if (!res) { status.innerHTML = '❌ Pas de réponse du boîtier.'; return; }
    if (res.n > 0) { await refreshLast(); }
    else {
      replayRow.hidden = true;
      status.innerHTML = '❌ Rien capté en 15 s sur ' + (band ? 'la radio ' + esc(band) + ' MHz' : 'la radio') +
        '. Vérifie que la télécommande émet bien sur cette bande, l\'antenne, et la distance (< 1 m).';
    }
  }

  async function build() {
    const d = await getj('/api/radios');
    const radios = d && Array.isArray(d.radios) && d.radios.length ? d.radios : [{ idx: 0, band: '' }];
    btns.innerHTML = radios.map(r =>
      '<button class="btn cap-do" type="button" data-radio="' + (r.idx || 0) + '" data-band="' + esc(r.band || '') + '">' +
      '📡 Capturer ' + (r.band ? esc(r.band) + ' MHz' : '') + '</button>'
    ).join('');
    btns.querySelectorAll('.cap-do').forEach(b => b.onclick = () => doCapture(+b.dataset.radio, b.dataset.band));
    refreshLast();
  }

  replayBtn.onclick = async () => {
    setBusy(true);
    const old = replayBtn.textContent;
    replayBtn.textContent = '▶ Rejeu…';
    const r = await getj('/api/capture/replay', { method: 'POST' });
    replayBtn.textContent = old;
    setBusy(false);
    status.innerHTML = (r && r.ok) ? '✅ Rejoué. Ton récepteur a-t-il réagi ?' : '❌ Rejeu échoué (radio occupée ?).';
  };

  build();
  /* Recharge la liste des radios en arrivant sur l'ecran radio (apres connexion, p.ex.). */
  window.addEventListener('hashchange', () => { if ((location.hash || '').indexOf('radio') >= 0) build(); });
})();
