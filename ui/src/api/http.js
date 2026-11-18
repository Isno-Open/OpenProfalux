// Les appels HTTP vers le boitier, partages par reelle.js et reelle-systeme.js. Un 401 veut dire que la
// session a expire (ou que le mot de passe vient de changer) : on previent le store, qui reaffiche la
// connexion si le boitier est protege.
let surNonAutorise = () => {};
export function quandNonAutorise(fn) { surNonAutorise = fn; }

export const json = (url, options) => fetch(url, options).then(r => {
  if (r.status === 401) { surNonAutorise(); return Promise.reject(401); }
  return r.ok ? r.json().catch(() => ({})) : Promise.reject(r.status);
});
/* Corps JSON, comme app.js : le firmware lit le corps brut. Sans corps : POST vide. */
export const post = (url, corps) => json(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: corps == null ? undefined : JSON.stringify(corps) });
export const attendre = ms => new Promise(r => setTimeout(r, ms));
/* Refus metier ({ ok: 0, err }) -> rejet, comme une erreur HTTP : les ecrans n'ont qu'un cas a traiter. */
export const exiger = p => p.then(r => (r && r.ok === 0 ? Promise.reject(r.err || 'refus') : r));
/* Interroge une route jusqu'a ce que le test passe (captures, balayage, etats). */
export async function sonder(url, fini, essais = 20, pas = 1000, suivi) {
  for (let i = 0; i < essais; i++) {
    await attendre(pas);
    const d = await json(url).catch(e => (e === 401 ? Promise.reject(401) : null));
    if (d && suivi) suivi(d);
    if (d && fini(d)) return d;
  }
  return Promise.reject('delai');
}
