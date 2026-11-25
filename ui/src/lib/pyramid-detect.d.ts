// Declaration de firmware/main/web/pyramid-detect.js (JavaScript, teste par son banc node), importe sous le nom
// « pyramid-detect » (alias de vite.config.ts) : la detection de profil de la pyramide « Ajouter un appareil ».
// Voir detection.ts. Les champs (profils, profil...) sont ceux du module et de profiles.json.
declare module 'pyramid-detect' {
  export interface RankedProfile<P> { index: number; profil: P; score: number }
  export interface Ranking<P> { ranked: RankedProfile<P>[]; proposed: RankedProfile<P>[]; ambiguous: boolean; confident: boolean }
  export interface PyramidDetect {
    traits(doc: object | null): { certain: string[]; detect: string[] };
    rankProfiles<P>(observed: Record<string, unknown> | null, doc: { profils: P[] } | null): Ranking<P>;
  }
  const detect: PyramidDetect | undefined;
  export default detect;
}
