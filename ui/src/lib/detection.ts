// La detection de profil de la pyramide « Ajouter un appareil » : firmware/main/web/pyramid-detect.js, partage
// avec son banc node (firmware/test/pyramid-detect). Le fichier se declare a node (module.exports) ou au
// navigateur (self.PyramidDetect). La construction le lit comme un module CommonJS (son export) ; le serveur de
// developpement le sert tel quel (il se pose alors sur self) : on accepte les deux.
import detectModule, { type PyramidDetect, type Ranking } from 'pyramid-detect';   // alias de vite.config.ts

declare global { var PyramidDetect: PyramidDetect | undefined }

function detection(): PyramidDetect {
  const found = detectModule && typeof detectModule.rankProfiles === 'function' ? detectModule : globalThis.PyramidDetect;
  if (!found) throw new Error('pyramid-detect.js introuvable');
  return found;
}
export function rankProfiles<P>(observed: Record<string, unknown> | null, doc: { profils: P[] } | null): Ranking<P> {
  return detection().rankProfiles(observed, doc);
}
export function traits(doc: object | null): { certain: string[]; detect: string[] } {
  return detection().traits(doc);
}
