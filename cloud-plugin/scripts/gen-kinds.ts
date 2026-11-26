// Engendre src/kinds.generated.ts depuis firmware/main/web/profiles.json : les types d'appareils de
// la famille OpenProfalux sont ceux que le FIRMWARE declare, une seule declaration, et leurs icones sont celles de la planche du boitier (docs/FAMILLES.md
// d'OpenCloudApp). Lance a la construction et par les tests ; le fichier engendre n'est pas commis.
//   node scripts/gen-kinds.ts [profiles.json] [sortie]
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { CONTROL } from "../opencloudapp/server/src/family-contract.ts";
import { CONTROL_ID, ACTION_IDS, SETTING_ID, ORIENTATION_MAX, ROLE_SIMPLE, roleKindId } from "../src/ids.ts";
import { DEVICE_SETTING } from "../opencloudapp/server/src/family-contract.ts";
import { SETTING_KIND } from "../opencloudapp/server/src/plugin-contract.ts";

const here = dirname(fileURLToPath(import.meta.url));
const SRC = process.argv[2] ?? join(here, "../../firmware/main/web/profiles.json");
const OUT = process.argv[3] ?? join(here, "../src/kinds.generated.ts");
/** La planche d'icones du projet : celle que l'interface du boitier affiche. */
const SPRITE = join(here, "../../ui/src/assets/icons.svg.txt");
/** Icone des telecommandes (elles ne sont pas un type de profiles.json : c'est le plugin qui les declare). */
const REMOTE_ICON = "remote";

type Text = { fr: string; en: string };
type Ctl = { kind: string; id: string; label: Text; icon?: unknown; settable?: boolean; secure?: true };
/** Un type simple tel que le generateur le construit (positional : pour choisir ceux d'un role). */
type Simple = { id: string; label: Text; icon: unknown; controls: Ctl[]; settings: unknown[]; positional: boolean };
type Dpad = { up: string; stop: string; down: string };
const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);
const isDpad = (v: unknown): v is Dpad => isObj(v) && typeof v.up === "string" && typeof v.stop === "string" && typeof v.down === "string";

/** Icone declaree par le firmware -> icone du portail. profiles.json nomme un dessin de la planche du projet
 *  (symbole i-<nom>) ; le portail le recoit en image (data: URL), jamais en balisage : c'est donc LE MEME
 *  dessin que sur le boitier. Un nom absent de la planche : erreur, plutot qu'une icone fausse. */
export function iconOf(owner: string, icon: unknown, sprite: string): { src: string } {
  if (typeof icon !== "string" || !/^[a-z][a-z-]*$/.test(icon)) throw new Error(`${owner} : icone attendue, le nom d'un dessin de la planche`);
  const symbol = new RegExp(`<symbol id="i-${icon}" viewBox="([^"]+)">([\\s\\S]*?)</symbol>`).exec(sprite);
  if (!symbol) throw new Error(`${owner} : icone « ${icon} » absente de la planche`);
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${symbol[1]!}">${symbol[2]!.replace(/\s*\n\s*/g, "")}</svg>`;
  return { src: `data:image/svg+xml;base64,${Buffer.from(svg, "utf8").toString("base64")}` };
}
/** L'icone des telecommandes, prise sur la meme planche. */
export const remoteIconOf = (sprite: string): { src: string } => iconOf("telecommande", REMOTE_ICON, sprite);

/** Plus long nom accepte par le firmware (profiles.json name_max). */
export function nameMaxOf(profiles: unknown): number {
  const n = isObj(profiles) ? profiles.name_max : undefined;
  if (typeof n !== "number" || !Number.isInteger(n) || n < 1) throw new Error("profiles.json : name_max (longueur maximale d'un nom) requis");
  return n;
}

/** Les types d'appareils du firmware, dans le vocabulaire du portail. */
export function kindsOf(profiles: unknown, sprite: string): unknown[] {
  if (!isObj(profiles) || !isObj(profiles.types)) throw new Error("profiles.json : objet types attendu");
  const devicesMax = profiles.devices_max;
  if (typeof devicesMax !== "number" || devicesMax < 1) throw new Error("profiles.json : devices_max (nombre maximal d'appareils) requis");
  const simple: Simple[] = Object.entries(profiles.types).map(([id, t]) => {
    if (!isObj(t) || typeof t.label !== "string" || !isDpad(t.dpad)) throw new Error(`type ${id} : label et dpad requis`);
    const en = t.en;
    if (!isObj(en) || typeof en.label !== "string" || !en.label || !isDpad(en.dpad)) throw new Error(`type ${id} : libelles anglais requis (en.label, en.dpad)`);
    const dpad = t.dpad, enDpad = en.dpad;
    const text = (fr: string, e: string): Text => ({ fr, en: e });
    const glyphs: Dpad = { up: "▲", stop: "■", down: "▼" };
    // commandes qui donnent acces aux lieux (profiles.json "secure") : le portail exige un code avant
    // de les envoyer pour un tiers. Declare par le firmware, qui seul sait ce que la commande emet.
    const secure = t.secure ?? [];
    if (!Array.isArray(secure) || secure.some((k) => !ACTION_IDS.some((a) => a === k)))
      throw new Error(`type ${id} : secure liste des commandes du pave (${ACTION_IDS.join(", ")})`);
    const controls: Ctl[] = ACTION_IDS.map((k) => ({
      kind: CONTROL.action, id: k, label: text(dpad[k], enDpad[k]), icon: { glyph: glyphs[k] }, ...(secure.includes(k) ? { secure: true as const } : {}),
    }));
    // un type positionnel a une position, reglable (decide le 2026-11-14) ; chaque appareil peut la masquer
    if (t.positional === true) controls.push({ kind: CONTROL.position, id: CONTROL_ID.position, label: text("Position", "Position"), settable: true });
    // reglages : le rang dans la liste pour tous ; l'orientation de la facade pour un type positionnel
    // (comme l'interface locale) ; l'etalonnage pour un type qui declare ses bornes (travel_s)
    const settings: unknown[] = [{ kind: SETTING_KIND.number, key: SETTING_ID.rank, label: text("Rang dans la liste", "Position in the list"), min: 1, max: devicesMax,
      hint: text("1 = en tête. Les autres appareils se décalent.", "1 = first. The other devices shift.") }];
    if (t.positional === true) settings.push({ kind: SETTING_KIND.number, key: SETTING_ID.orientation, label: text("Orientation de la façade (°)", "Facade orientation (°)"),
      min: 0, max: ORIENTATION_MAX, hint: text("Azimut : 0 nord, 90 est, 180 sud, 270 ouest.", "Azimuth: 0 north, 90 east, 180 south, 270 west.") });
    const travel = t.travel_s;
    if (travel !== undefined) {
      if (!isObj(travel) || typeof travel.min !== "number" || typeof travel.max !== "number" || travel.min >= travel.max)
        throw new Error(`type ${id} : travel_s {min, max} en secondes, min < max`);
      const field = (key: string, fr: string, e: string): unknown => ({ kind: SETTING_KIND.number, key, label: text(fr, e), min: travel.min, max: travel.max,
        hint: text("Chronométré sur le volet, en secondes.", "Timed on the shutter, in seconds.") });
      settings.push(field(SETTING_ID.travelUp, "Temps de montée (s)", "Up travel time (s)"), field(SETTING_ID.travelDown, "Temps de descente (s)", "Down travel time (s)"));
    }
    return { id, label: text(t.label, en.label), icon: iconOf(`type ${id}`, t.icon, sprite), controls, settings, positional: t.positional === true };
  });
  // ordre des types = ordre des cartes sans rang : les roles « list_first » (centrales) d'abord
  const roles = roleKindsOf(profiles, simple, sprite);
  const out = (r: (typeof roles)[number]): unknown => { const { listFirst: _, ...k } = r; return k; };
  return [...roles.filter((r) => r.listFirst).map(out), ...simple.map(({ positional: _, ...k }) => k), ...roles.filter((r) => !r.listFirst).map(out)];
}

type Role = { id: string; icon: unknown; label: Text; membersLabel: Text; members: readonly string[]; positionalOnly: boolean; listFirst: boolean };
/** Roles du firmware (profiles.json "roles") : centrale, zone. */
function rolesOf(profiles: Record<string, unknown>): Role[] {
  const roles = profiles.roles;
  if (!isObj(roles)) throw new Error("profiles.json : objet roles attendu");
  return Object.entries(roles).filter(([k]) => !k.startsWith("_")).map(([id, r]) => {
    if (!isObj(r) || typeof r.label !== "string" || typeof r.members_label !== "string" || !Array.isArray(r.members)
      || !isObj(r.en) || typeof r.en.label !== "string" || typeof r.en.members_label !== "string")
      throw new Error(`role ${id} : label, members_label, members et libelles anglais requis`);
    const members = r.members.map((m) => {
      if (typeof m !== "string" || (m !== ROLE_SIMPLE && !(m in roles))) throw new Error(`role ${id} : membre « ${String(m)} » ni simple ni un role`);
      return m;
    });
    return { id, icon: r.icon, label: { fr: r.label, en: r.en.label }, membersLabel: { fr: r.members_label, en: r.en.members_label }, members, positionalOnly: r.positional_only === true, listFirst: r.list_first === true };
  });
}

/** Une centrale ou une zone de chaque type permis : les commandes du type, sans position (elle ne serait
 *  celle d'aucun volet), le rang, et ses membres choisis parmi les appareils du boitier des roles permis. */
function roleKindsOf(profiles: Record<string, unknown>, simple: readonly Simple[], sprite: string): ({ listFirst?: boolean } & Record<string, unknown>)[] {
  const roles = rolesOf(profiles);
  const typesOf = (r: Role): readonly Simple[] => simple.filter((k) => !r.positionalOnly || k.positional);
  const kindsFor = (r: Role, member: string): string[] => {
    if (member === ROLE_SIMPLE) return typesOf(r).map((k) => k.id);
    const mr = roles.find((x) => x.id === member)!;
    return typesOf(r).filter((k) => typesOf(mr).includes(k)).map((k) => roleKindId(k.id, mr.id));
  };
  return roles.flatMap((r) => typesOf(r).map((k) => ({
    id: roleKindId(k.id, r.id), label: r.label, icon: iconOf(`role ${r.id}`, r.icon, sprite), listFirst: r.listFirst,
    controls: k.controls.filter((c) => c.kind === CONTROL.action),
    settings: [k.settings[0], { kind: DEVICE_SETTING.devices, key: SETTING_ID.members, label: r.membersLabel, kinds: r.members.flatMap((m) => kindsFor(r, m)) }],
  })));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const profiles: unknown = JSON.parse(readFileSync(SRC, "utf8"));
  const sprite = readFileSync(SPRITE, "utf8");
  const kinds = kindsOf(profiles, sprite);
  writeFileSync(OUT, `// ENGENDRE par scripts/gen-kinds.ts depuis firmware/main/web/profiles.json et la planche d'icones : ne pas editer.
import type { KindDecl } from "../opencloudapp/server/src/family-contract.js";
export const KINDS = ${JSON.stringify(kinds, null, 2)} satisfies readonly KindDecl[];
/** Plus long nom d'un appareil ou d'une telecommande (profiles.json name_max, COVER_NAME_MAX). */
export const NAME_MAX = ${nameMaxOf(profiles)};
/** Icone des telecommandes : le dessin de la planche du projet, comme sur le boitier. */
export const REMOTE_ICON = ${JSON.stringify(remoteIconOf(sprite))};
`);
  console.log(`${kinds.length} type(s) -> ${OUT}`);
}
