// Les appels HTTP vers le boitier, partages par real.ts et real-system.ts. Un 401 veut dire que la session a
// expire (ou que le mot de passe vient de changer) : on previent le store, qui reaffiche la connexion si le
// boitier est protege.
import { FAILURE, HTTP_UNAUTHORIZED } from '../domain';
import type { OkReply } from './types';

let onUnauthorized: () => void = () => {};
export function whenUnauthorized(callback: () => void): void { onUnauthorized = callback; }

export function json<T>(url: string, options?: RequestInit): Promise<T> {
  return fetch(url, options).then(r => {
    if (r.status === HTTP_UNAUTHORIZED) { onUnauthorized(); return Promise.reject(HTTP_UNAUTHORIZED); }
    if (!r.ok) return Promise.reject(r.status);
    /* Un corps qui n'est pas du JSON (reponse sans corps) vaut {}. Un corps qui n'a pas pu etre lu (requete
       abandonnee par son delai, connexion coupee) est un echec : {} passerait pour une reponse. */
    const parsed: Promise<T> = r.json().catch(e => (e instanceof SyntaxError ? {} : Promise.reject(e)));
    return parsed;
  });
}
/* Corps JSON, comme app.js : le firmware lit le corps brut. Sans corps : POST vide. */
export function post<T = OkReply>(url: string, body?: unknown): Promise<T> {
  return json<T>(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: body == null ? undefined : JSON.stringify(body) });
}
/* POST dont le refus porte sa raison dans le corps ({ ok: 0, err }, meme en 400) : on la garde pour le message. */
export async function postWithReason<T extends OkReply = OkReply>(url: string, body: unknown): Promise<T> {
  const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  if (r.status === HTTP_UNAUTHORIZED) { onUnauthorized(); return Promise.reject(HTTP_UNAUTHORIZED); }
  const d: T = await r.json().catch(() => ({}));
  return r.ok && d.ok !== 0 ? d : Promise.reject(d.err || `HTTP ${r.status}`);
}
export const wait = (ms: number): Promise<void> => new Promise(resolve => setTimeout(resolve, ms));
/* Refus metier ({ ok: 0, err }) -> rejet, comme une erreur HTTP : les ecrans n'ont qu'un cas a traiter. */
export function requireOk<T extends OkReply>(p: Promise<T>): Promise<T> {
  return p.then(r => (r && r.ok === 0 ? Promise.reject(r.err || FAILURE.refused) : r));
}
/* Interroge une route jusqu'a ce que le test passe (captures, balayage, etats). */
export async function poll<T>(url: string, done: (d: T) => boolean, attempts = 20, stepMs = 1000, onEach?: (d: T) => void): Promise<T> {
  for (let i = 0; i < attempts; i++) {
    await wait(stepMs);
    const d = await json<T>(url).catch(e => (e === HTTP_UNAUTHORIZED ? Promise.reject(HTTP_UNAUTHORIZED) : null));
    if (d && onEach) onEach(d);
    if (d && done(d)) return d;
  }
  return Promise.reject(FAILURE.timeout);
}
