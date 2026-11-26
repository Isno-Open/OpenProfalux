// Controle le catalogue (firmware/main/web/profiles.json) et engendre ses types TypeScript
// (src/api/catalog.generated.ts, jamais commis). Lance par « npm run gen », donc avant chaque
// verification de types et chaque construction de l'interface.
//
//   node scripts/gen-catalog.ts [catalogue] [sortie]
//
// Deux controles, contre le MEME schema que celui que verra un profil utilisateur :
//   1. la forme : profiles.schema.json (ajv) ;
//   2. les regles qui relient les parties (docs/PROFILES.md, section 7) : checkCatalog.
// Une seule erreur suffit a refuser : rien n'est engendre, et le code de sortie vaut 1.
import { createRequire } from 'node:module';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Ajv2020 } from 'ajv/dist/2020.js';
import { compile, type JSONSchema } from 'json-schema-to-typescript';

const here = dirname(fileURLToPath(import.meta.url));
const WEB = join(here, '../../firmware/main/web');
const BOARDS = join(here, '../../boards');
const SPRITE = join(here, '../src/assets/icons.svg.txt');
export const CATALOG_PATH = join(WEB, 'profiles.json');
export const SCHEMA_PATH = join(WEB, 'profiles.schema.json');
const OUT_PATH = join(here, '../src/api/catalog.generated.ts');

/* La seule resolution des heritages du projet : celle que l'interface et la detection utilisent. */
const require = createRequire(import.meta.url);
const detect: { resolveProfiles: (doc: unknown) => unknown[] } = require(join(WEB, 'pyramid-detect.js'));
const { resolveProfiles } = detect;

type Json = Record<string, unknown>;
const isObj = (x: unknown): x is Json => x !== null && typeof x === 'object' && !Array.isArray(x);
const obj = (x: unknown): Json => (isObj(x) ? x : {});
const list = (x: unknown): unknown[] => (Array.isArray(x) ? x : []);
const entries = (x: unknown): [string, Json][] => Object.entries(obj(x)).filter(([k]) => !k.startsWith('_')).map(([k, v]) => [k, obj(v)]);

/* Valeurs du catalogue que les regles nomment (les memes que le schema enumere). */
const BUTTONS = { fixed: 'fixed', free: 'free' } as const;
const STEP_COMMAND = 'step';
const STEP_INSTRUCTION = 'instruction';

/** Bande autorisee d'une radio, telle qu'une carte la declare (boards/<carte>.json). */
export interface BandAllowed { radio: string; low: number; high: number }

/** Les bandes autorisees de toutes les cartes du depot. */
export function boardBands(dir = BOARDS): BandAllowed[] {
  return readdirSync(dir).filter(f => f.endsWith('.json')).flatMap(f => {
    const board = obj(JSON.parse(readFileSync(join(dir, f), 'utf8')));
    return list(board.bands_allowed_khz).map(b => {
      const band = obj(b);
      if (typeof band.radio !== 'string' || typeof band.low !== 'number' || typeof band.high !== 'number') throw new Error(`${f} : bands_allowed_khz {radio, low, high} attendu`);
      return { radio: band.radio, low: band.low, high: band.high };
    });
  });
}

/** Les dessins de la planche du projet (symboles i-<nom>), que les types et les roles nomment. */
export function spriteIcons(path = SPRITE): string[] {
  return [...readFileSync(path, 'utf8').matchAll(/<symbol\b[^>]*\bid="i-([^"]+)"/g)].map(m => m[1] ?? '');
}

/** La forme : le catalogue contre son schema. Rend les erreurs, vide si tout va bien. */
export function checkShape(doc: unknown, schema: object): string[] {
  const ajv = new Ajv2020({ allErrors: true, strict: true });
  const validate = ajv.compile(schema);
  if (validate(doc)) return [];
  return (validate.errors ?? []).map(e => `forme : ${e.instancePath || '/'} ${e.message ?? ''}`);
}

/** Les regles qui relient les parties du catalogue (docs/PROFILES.md, section 7). */
export function checkCatalog(doc: unknown, bands: readonly BandAllowed[], icons: readonly string[]): string[] {
  const errors: string[] = [];
  const fail = (msg: string): void => { errors.push(msg); };
  const root = obj(doc);
  const protocols = new Map(entries(root.protocols));
  const types = new Map(entries(root.types));

  // types : chaque commande exposee est portee par un bouton ; un bouton pas a pas a un cycle et un delai.
  // (secure et cycle ne citent que up, stop et down, que tout type expose : le schema suffit)
  const legacyCodes = new Map<number, string>();
  for (const [key, type] of types) {
    const commands = Object.keys(obj(type.commands));
    const carried = new Set<string>();
    const ids = new Set<string>();
    for (const b of list(type.buttons).map(obj)) {
      const id = String(b.id), command = String(b.command);
      if (ids.has(id)) fail(`type ${key} : bouton ${id} en double`);
      ids.add(id);
      if (command === STEP_COMMAND) {
        if (!Array.isArray(b.cycle) || typeof b.press_gap_ms !== 'number') fail(`type ${key} : le bouton pas a pas ${id} doit declarer cycle et press_gap_ms`);
        // le schema limite le cycle aux commandes que tout type expose (up, stop, down)
        for (const c of list(b.cycle)) carried.add(String(c));
      } else {
        if (b.cycle !== undefined || b.press_gap_ms !== undefined) fail(`type ${key} : cycle et press_gap_ms sont reserves a un bouton pas a pas (${id})`);
        if (!commands.includes(command) && b.label === undefined) fail(`type ${key} : le bouton ${id} emet ${command}, sans libelle ni commande du type qui en donne un`);
        carried.add(command);
      }
    }
    for (const c of commands) if (!carried.has(c)) fail(`type ${key} : la commande ${c} n'est portee par aucun bouton`);
    const travel = obj(type.travel_s);
    if (type.travel_s !== undefined && !(Number(travel.min) < Number(travel.max))) fail(`type ${key} : travel_s demande min < max`);
    const legacy = obj(type.legacy);
    if (typeof legacy.code === 'number') {
      const other = legacyCodes.get(legacy.code);
      if (other) fail(`types ${other} et ${key} : meme numero legacy ${legacy.code}`);
      legacyCodes.set(legacy.code, key);
    }
  }

  // icones : un dessin de la planche, pour chaque type et chaque role
  for (const [kind, set] of [['type', types], ['role', new Map(entries(root.roles))]] as const) {
    for (const [key, x] of set) if (!icons.includes(String(x.icon))) fail(`${kind} ${key} : l'icone ${String(x.icon)} n'est pas un dessin de la planche (ui/src/assets/icons.svg.txt)`);
  }

  // profils : resolus par la meme fonction que l'interface (une base absente ou une boucle leve ici)
  const raw = list(root.profiles).map(obj);
  const seen = new Set<string>();
  for (const p of raw) {
    const id = String(p.id);
    if (seen.has(id)) fail(`profil ${id} : identifiant en double`);
    seen.add(id);
  }
  let resolved: Json[];
  try { resolved = resolveProfiles(doc).map(obj); } catch (e) { fail(e instanceof Error ? e.message : String(e)); return errors; }
  const byId = new Map(resolved.map(p => [String(p.id), p]));

  const models = new Map<string, string>();
  for (const [i, p] of resolved.entries()) {
    const id = String(p.id), own = raw[i] ?? {};
    const radio = obj(p.radio);
    for (const field of ['types', 'protocol', 'add', 'test'] as const) if (p[field] === undefined) fail(`profil ${id} : ${field} manquant (ni declare ni herite)`);
    for (const field of ['band', 'freq_khz', 'modulation'] as const) if (radio[field] === undefined) fail(`profil ${id} : radio.${field} manquant (ni declare ni herite)`);

    // une base ne change ni l'encodeur (le protocole) ni la bande ; elle precede ce qui en herite (le firmware
    // lit les profils dans l'ordre, catalog.c)
    if (typeof own.inherits === 'string' && !raw.slice(0, i).some(q => q.id === own.inherits)) fail(`profil ${id} : sa base ${own.inherits} doit le preceder`);
    if (typeof own.inherits === 'string') {
      const base = byId.get(own.inherits);
      if (base && own.protocol !== undefined && own.protocol !== base.protocol) fail(`profil ${id} : change le protocole de sa base ${own.inherits}`);
      if (base && obj(own.radio).band !== undefined && obj(own.radio).band !== obj(base.radio).band) fail(`profil ${id} : change la bande de sa base ${own.inherits}`);
    }

    const protocol = protocols.get(String(p.protocol));
    if (!protocol) { fail(`profil ${id} : protocole ${String(p.protocol)} inconnu`); continue; }
    const procedure = obj(protocol.procedures)[String(p.add)];
    if (!Array.isArray(procedure)) fail(`profil ${id} : le protocole ${String(p.protocol)} n'a pas de procedure ${String(p.add)}`);
    for (const step of list(procedure).map(obj)) {
      if (step.step !== STEP_INSTRUCTION || typeof step.from !== 'string' || step.optional === true) continue;
      if (obj(p.instructions)[step.from] === undefined) fail(`profil ${id} : la procedure ${String(p.add)} montre instructions.${step.from}, que le profil n'a pas`);
    }

    // boutons des types : un protocole a codes ne sait emettre que ses commandes
    const codes = Object.keys(obj(protocol.commands));
    for (const t of list(p.types)) {
      const type = types.get(String(t));
      if (!type) { fail(`profil ${id} : type ${String(t)} inconnu`); continue; }
      if (protocol.buttons !== BUTTONS.fixed) continue;
      for (const b of list(type.buttons).map(obj)) {
        if (!codes.includes(String(b.command))) fail(`profil ${id} : le protocole ${String(p.protocol)} n'a pas de code pour la commande ${String(b.command)} du type ${String(t)}`);
      }
    }
    if (protocol.buttons === BUTTONS.fixed && codes.length === 0) fail(`protocole ${String(p.protocol)} : boutons imposes sans commandes`);

    // la porteuse dans une bande qu'une carte autorise pour cette radio
    const f = Number(radio.freq_khz);
    if (radio.band !== undefined && !bands.some(b => b.radio === radio.band && b.low <= f && f <= b.high)) {
      fail(`profil ${id} : ${f} kHz hors de toute bande ${String(radio.band)} autorisee par une carte (boards/*.json)`);
    }

    // un modele reconnu designe un seul profil
    for (const m of list(p.models)) {
      const other = models.get(String(m));
      if (other) fail(`profils ${other} et ${id} : meme modele ${String(m)}`);
      models.set(String(m), id);
    }
  }
  return errors;
}

/* Le schema sans ses notes : une cle '_' est un commentaire pour qui lit le fichier, jamais une donnee que le
   code lit. La garder ferait de chaque objet un dictionnaire ouvert, et le compilateur ne verifierait plus rien.
   On retire la regle des notes d'une copie, en place : la copie garde le type du schema. */
const NOTE_KEYS = '^_';
function dropNotes(node: unknown): void {
  if (Array.isArray(node)) { node.forEach(dropNotes); return; }
  if (!isObj(node)) return;
  const pattern = node.patternProperties;
  if (isObj(pattern)) {
    delete pattern[NOTE_KEYS];
    if (Object.keys(pattern).length === 0) delete node.patternProperties;
  }
  Object.values(node).forEach(dropNotes);
}
function withoutNotes(schema: JSONSchema): JSONSchema {
  const copy = structuredClone(schema);
  dropNotes(copy);
  return copy;
}

/** Les types TypeScript du catalogue, engendres depuis son schema. */
export async function typesOf(schema: JSONSchema): Promise<string> {
  const banner = '// ENGENDRE par scripts/gen-catalog.ts depuis firmware/main/web/profiles.schema.json : ne pas editer.\n';
  return compile(withoutNotes(schema), 'Catalog', { bannerComment: banner, unreachableDefinitions: true, additionalProperties: false, style: { singleQuote: true } });
}

async function main(): Promise<void> {
  const [catalogPath = CATALOG_PATH, outPath = OUT_PATH] = process.argv.slice(2);
  const doc: unknown = JSON.parse(readFileSync(catalogPath, 'utf8'));
  const schema: JSONSchema = JSON.parse(readFileSync(SCHEMA_PATH, 'utf8'));
  const shape = checkShape(doc, schema);
  const errors = shape.length ? shape : checkCatalog(doc, boardBands(), spriteIcons());
  if (errors.length) {
    for (const e of errors) console.error(`${catalogPath} : ${e}`);
    process.exit(1);
  }
  writeFileSync(outPath, await typesOf(schema));
  console.log(`${catalogPath} : conforme, types engendres dans ${outPath}`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) await main();
