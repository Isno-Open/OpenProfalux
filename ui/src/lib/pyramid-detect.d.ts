// Declaration de firmware/main/web/pyramid-detect.js (JavaScript, teste par son banc node), importe sous le nom
// « pyramid-detect » (alias de vite.config.ts) : la resolution des profils du catalogue et leur detection pour la
// pyramide « Ajouter un appareil ». Voir detection.ts.
declare module 'pyramid-detect' {
  export interface RankedProfile<P> { index: number; profile: P; score: number }
  export interface Ranking<P> { ranked: RankedProfile<P>[]; proposed: RankedProfile<P>[]; ambiguous: boolean; confident: boolean }
  export interface PyramidDetect {
    resolveProfiles<P>(doc: { profiles: P[] } | null): P[];
    traits(doc: object | null): { certain: string[]; detect: string[] };
    rankProfiles<P>(observed: Record<string, unknown> | null, doc: { profiles: P[] } | null): Ranking<P>;
  }
  const detect: PyramidDetect | undefined;
  export default detect;
}
