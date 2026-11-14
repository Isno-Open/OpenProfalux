// Engendre src/kinds.generated.ts depuis firmware/main/web/profiles.json : les types d'appareils de
// la famille OpenProfalux sont ceux que le FIRMWARE declare, une seule declaration (docs/FAMILLES.md
// d'OpenCloudApp). Lance a la construction et par les tests ; le fichier engendre n'est pas commis.
//   node scripts/gen-kinds.ts [profiles.json] [sortie]
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { CONTROL } from "../opencloudapp/server/src/family-contract.ts";
import { CONTROL_ID, ACTION_IDS } from "../src/ids.ts";

const here = dirname(fileURLToPath(import.meta.url));
const SRC = process.argv[2] ?? join(here, "../../firmware/main/web/profiles.json");
const OUT = process.argv[3] ?? join(here, "../src/kinds.generated.ts");

type Text = { fr: string; en: string };
type Dpad = { up: string; stop: string; down: string };
const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const isDpad = (v: unknown): v is Dpad => isObj(v) && typeof v.up === "string" && typeof v.stop === "string" && typeof v.down === "string";

/** Icone du firmware -> icone du portail : un caractere tel quel, ou le trace d'un SVG a UN seul path
 *  (le portail ne prend jamais de balisage). Autre chose : erreur, plutot qu'une icone fausse. */
export function iconOf(type: string, icon: unknown): { glyph: string } | { path: string } {
  if (typeof icon !== "string" || !icon) throw new Error(`type ${type} : icone absente`);
  if (!icon.startsWith("<")) return { glyph: icon };
  const paths = [...icon.matchAll(/<path\b[^>]*\bd=['"]([^'"]+)['"]/g)];
  if (paths.length !== 1) throw new Error(`type ${type} : icone SVG a ${paths.length} traces, un seul attendu`);
  return { path: paths[0]![1]! };
}

/** Les types d'appareils du firmware, dans le vocabulaire du portail. */
export function kindsOf(profiles: unknown): unknown[] {
  if (!isObj(profiles) || !isObj(profiles.types)) throw new Error("profiles.json : objet types attendu");
  return Object.entries(profiles.types).map(([id, t]) => {
    if (!isObj(t) || typeof t.label !== "string" || !isDpad(t.dpad)) throw new Error(`type ${id} : label et dpad requis`);
    const en = t.en;
    if (!isObj(en) || typeof en.label !== "string" || !en.label || !isDpad(en.dpad)) throw new Error(`type ${id} : libelles anglais requis (en.label, en.dpad)`);
    const dpad = t.dpad, enDpad = en.dpad;
    const text = (fr: string, e: string): Text => ({ fr, en: e });
    const glyphs: Dpad = { up: "▲", stop: "■", down: "▼" };
    const controls: unknown[] = ACTION_IDS.map((k) => ({
      kind: CONTROL.action, id: k, label: text(dpad[k], enDpad[k]), icon: { glyph: glyphs[k] },
    }));
    // un type positionnel a une position, reglable (decide le 2026-11-14) ; chaque appareil peut la masquer
    if (t.positional === true) controls.push({ kind: CONTROL.position, id: CONTROL_ID.position, label: text("Position", "Position"), settable: true });
    return { id, label: text(t.label, en.label), icon: iconOf(id, t.icon), controls };
  });
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const kinds = kindsOf(JSON.parse(readFileSync(SRC, "utf8")));
  writeFileSync(OUT, `// ENGENDRE par scripts/gen-kinds.ts depuis firmware/main/web/profiles.json : ne pas editer.
import type { KindDecl } from "../opencloudapp/server/src/family-contract.js";
export const KINDS = ${JSON.stringify(kinds, null, 2)} satisfies readonly KindDecl[];
`);
  console.log(`${kinds.length} type(s) -> ${OUT}`);
}
