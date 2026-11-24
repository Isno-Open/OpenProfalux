// Comparaison de versions du firmware, comme app.js : « 0.2.12 » plus recente que « 0.2.11 », numeriquement.
// A numeros egaux, une version de test (« 0.2.12-test9 ») est PLUS ANCIENNE que la version publiee
// (« 0.2.12 »), comme en semver : sinon un testeur ne se voyait jamais proposer la release (revue de la
// PR #70). Deux suffixes entre eux ne se comparent pas (pas d'ordre fiable entre deux builds de test).
import type { OtaStatus } from './api/types';

export const withoutV = (v: string | null | undefined): string => String(v || '').replace(/^v/, '');

export function isNewer(a: string, b: string): boolean {
  const numeric = (v: string): string | null => (withoutV(v).match(/^\d+(?:\.\d+)*/) || [null])[0] ?? null;
  const x = numeric(a), y = numeric(b);
  if (!x || !y) return false;
  const xa = x.split('.').map(Number), ya = y.split('.').map(Number);
  for (let i = 0; i < Math.max(xa.length, ya.length); i++) {
    const d = (xa[i] ?? 0) - (ya[i] ?? 0);
    if (d) return d > 0;
  }
  const hasSuffix = (v: string): boolean => withoutV(v).length > (numeric(v) ?? '').length;
  return !hasSuffix(a) && hasSuffix(b);
}

/* Une mise a jour attend : le boitier a trouve une release plus recente, avec l'image de SA carte. */
export const updateAvailable = (st: OtaStatus | null): boolean => !!(st && st.latest_url && isNewer(st.latest, st.version));
