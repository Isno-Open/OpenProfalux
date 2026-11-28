// Banc du generateur du catalogue (scripts/gen-catalog.ts). Chaque regle de docs/PROFILES.md
// (section 7) a son temoin : on casse le VRAI catalogue d'une seule facon, et la regle doit le
// refuser avec son message. Puis les deux appels : le generateur lance sur un catalogue casse doit
// sortir en erreur, sans rien engendrer, que la faute soit de forme ou de regle.
//
//   node --test scripts/*.test.ts
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CATALOG_PATH, SCHEMA_PATH, boardBands, checkCatalog, checkFirmware, checkSchemaFirmware, checkShape, firmwareLimits, spriteIcons } from './gen-catalog.ts';

/* Les profils du vrai catalogue que ce banc casse, par leur identifiant. */
const PROFILE = { cameReplay: 'came-replay', eveno: 'eveno-enrol', neosol: 'neosol-enrol', profaluxEnrol: 'profalux-enrol', somfy: 'somfy-rts-enrol' } as const;
type Json = Record<string, any>;   // un catalogue qu'on casse expres : sa forme n'est plus garantie
const real = (): Json => JSON.parse(readFileSync(CATALOG_PATH, 'utf8'));
const schema: object = JSON.parse(readFileSync(SCHEMA_PATH, 'utf8'));
const bands = boardBands();
const icons = spriteIcons();
const limits = firmwareLimits();
const profile = (doc: Json, id: string): Json => doc.profiles.find((p: Json) => p.id === id);

/* La regle refuse ce catalogue, et son message le dit. */
function refuses(doc: Json, pattern: RegExp): void {
  const errors = [...checkShape(doc, schema), ...checkCatalog(doc, bands, icons), ...checkFirmware(doc, limits)];
  assert.ok(errors.some(e => pattern.test(e)), `attendu ${pattern}, obtenu :\n${errors.join('\n') || '(rien)'}`);
}

test('le vrai catalogue passe la forme et les regles', () => {
  assert.deepEqual(checkShape(real(), schema), []);
  assert.deepEqual(checkCatalog(real(), bands, icons), []);
  assert.deepEqual(checkFirmware(real(), limits), []);
});

test('forme : une cle inconnue est refusee', () => {
  const doc = real(); profile(doc, PROFILE.cameReplay).colour = 'bleu';
  refuses(doc, /forme : .*must NOT have additional properties/);
});

test('forme : une commande inconnue est refusee', () => {
  const doc = real(); doc.types.volet.buttons[0].command = 'ouvrir';
  refuses(doc, /forme : \/types\/volet\/buttons\/0\/command/);
});

test('type : une commande exposee sans bouton est refusee', () => {
  const doc = real(); doc.types.volet.buttons.pop();
  refuses(doc, /type volet : la commande down n'est portee par aucun bouton/);
});

test('type : un bouton pas a pas sans delai entre appuis est refuse', () => {
  const doc = real(); delete doc.types.portail.buttons[0].press_gap_ms;
  refuses(doc, /type portail : le bouton pas a pas main doit declarer cycle et press_gap_ms/);
});

test("forme : un cycle ne passe que par up, stop et down", () => {
  const doc = real(); doc.types.portail.buttons[0].cycle = ['up', 'prog'];
  refuses(doc, /forme : .*cycle/);
});

test('forme : un type expose toujours up, stop et down', () => {
  const doc = real(); delete doc.types.portail.commands.down;
  refuses(doc, /forme : .*must have required property 'down'/);
});

test("type et role : une icone hors de la planche est refusee", () => {
  const doc = real(); doc.types.portail.icon = 'portique'; doc.roles.zone.icon = 'piece';
  refuses(doc, /type portail : l'icone portique n'est pas un dessin de la planche/);
  refuses(doc, /role zone : l'icone piece n'est pas un dessin de la planche/);
});

test('type : deux types avec le meme numero legacy sont refuses', () => {
  const doc = real(); doc.types.portail.legacy.code = 0;
  refuses(doc, /types volet et portail : meme numero legacy 0/);
});

test('profil : un protocole inconnu est refuse', () => {
  const doc = real(); profile(doc, PROFILE.cameReplay).protocol = 'nice';
  refuses(doc, /profil came-replay : protocole nice inconnu/);
});

test('profil : un type inconnu est refuse', () => {
  const doc = real(); profile(doc, PROFILE.cameReplay).types = ['garage'];
  refuses(doc, /profil came-replay : type garage inconnu/);
});

test("profil : une methode d'ajout sans procedure dans le protocole est refusee", () => {
  const doc = real(); profile(doc, PROFILE.cameReplay).add = 'enrol';
  refuses(doc, /profil came-replay : le protocole came n'a pas de procedure enrol/);
});

test('profil : un protocole a codes ne porte pas un bouton dont il ne connait pas la commande', () => {
  const doc = real(); profile(doc, PROFILE.somfy).types = ['portail'];
  refuses(doc, /profil somfy-rts-enrol : le protocole rts n'a pas de code pour la commande step du type portail/);
});

test('profil : une instruction exigee par la procedure doit exister dans le profil', () => {
  const doc = real(); delete profile(doc, PROFILE.profaluxEnrol).instructions;
  refuses(doc, /profil profalux-enrol : la procedure enrol montre instructions\.gestures/);
});

test("profil : une porteuse hors de la bande qu'une carte autorise est refusee", () => {
  const doc = real(); profile(doc, PROFILE.eveno).radio.freq_khz = 871000;
  refuses(doc, /profil eveno-enrol : 871000 kHz hors de toute bande 868 autorisee/);
});

test('profil : un champ ni declare ni herite est refuse', () => {
  const doc = real(); delete profile(doc, PROFILE.somfy).radio.modulation;
  refuses(doc, /profil somfy-rts-enrol : radio\.modulation manquant/);
});

test('heritage : une base absente est refusee', () => {
  const doc = real(); profile(doc, PROFILE.eveno).inherits = 'nulle-part';
  refuses(doc, /profil eveno-enrol : base nulle-part introuvable/);
});

test('heritage : une boucle est refusee', () => {
  const doc = real(); profile(doc, PROFILE.profaluxEnrol).inherits = PROFILE.eveno;
  refuses(doc, /heritage en boucle/);
});

test('heritage : une base qui suit ce qui en herite est refusee (le firmware lit dans l\'ordre)', () => {
  const doc = real();
  const base = doc.profiles.findIndex((p: Json) => p.id === PROFILE.profaluxEnrol);
  const [moved] = doc.profiles.splice(base, 1);
  doc.profiles.push(moved);
  refuses(doc, /profil eveno-enrol : sa base profalux-enrol doit le preceder/);
});

test('heritage : changer la bande de sa base est refuse', () => {
  const doc = real(); profile(doc, PROFILE.eveno).radio.band = '433';
  refuses(doc, /profil eveno-enrol : change la bande de sa base profalux-enrol/);
});

test('heritage : changer le protocole de sa base est refuse', () => {
  const doc = real(); profile(doc, PROFILE.eveno).protocol = 'rts';
  refuses(doc, /profil eveno-enrol : change le protocole de sa base profalux-enrol/);
});

test('profils : un identifiant en double est refuse', () => {
  const doc = real(); profile(doc, PROFILE.eveno).id = PROFILE.neosol;
  refuses(doc, /profil neosol-enrol : identifiant en double/);
});

test('profils : un modele reconnu par deux profils est refuse', () => {
  const doc = real(); profile(doc, PROFILE.neosol).models = ['FranciaFlex M4G'];
  refuses(doc, /profils profalux-enrol et neosol-enrol : meme modele FranciaFlex M4G/);
});

test('firmware : un catalogue plus grand que son lecteur est refuse a la construction', () => {
  const doc = real();
  const base = profile(doc, PROFILE.somfy);
  for (let i = doc.profiles.length; i <= limits.profiles; i++) doc.profiles.push({ ...base, id: `copie-${i}` });
  refuses(doc, new RegExp(`firmware : ${limits.profiles + 1} profils, le lecteur en prend ${limits.profiles}`));
});

test('firmware : un identifiant de profil trop long pour le lecteur est refuse', () => {
  const doc = real(); profile(doc, PROFILE.somfy).id = 'p'.repeat(limits.id);
  refuses(doc, /firmware : identifiant de profil .* trop long/);
});

test('firmware : deux protocoles pour un meme encodeur sont refuses', () => {
  const doc = real(); doc.protocols.copie = { ...doc.protocols.rts };
  refuses(doc, /firmware : deux protocoles pour l'encodeur somfy-rts/);
});

/* L'ordre des modeles est un contrat : une identite d'enrolement garde en NVS le NUMERO de son modele
   (pfx_enrol.c), lu dans le catalogue (catalog.c) dans l'ordre des profils. Ces quatre-la etaient dans
   cet ordre avant le catalogue (v0.2.16) : on ajoute a la fin, on ne reordonne pas. */
test('modeles : les numeros gardes en NVS designent toujours le meme modele', () => {
  const catalog = real().profiles.flatMap((p: Json) => p.models ?? []);
  assert.deepEqual(catalog.slice(0, 4), ['Profalux MAI-EMPX / MUR / NOE', 'FranciaFlex M4G', 'Profalux NeoSol', 'Profalux MAI-RDPX / RDTELPX']);
});

/* Les appels : le generateur lui-meme refuse, sort en erreur et n'engendre rien. */
const script = fileURLToPath(new URL('./gen-catalog.ts', import.meta.url));
function run(doc: Json, schemaDoc?: Json): { code: number; out: string; generated: boolean } {
  const dir = mkdtempSync(join(tmpdir(), 'catalog-'));
  const input = join(dir, 'profiles.json'), output = join(dir, 'catalog.generated.ts'), schemaPath = join(dir, 'profiles.schema.json');
  writeFileSync(input, JSON.stringify(doc));
  writeFileSync(schemaPath, JSON.stringify(schemaDoc ?? schema));
  const r = spawnSync(process.execPath, [script, input, output, schemaPath], { encoding: 'utf8' });
  return { code: r.status ?? -1, out: r.stderr, generated: existsSync(output) };
}

test('appel : le generateur engendre les types du vrai catalogue', () => {
  const r = run(real());
  assert.equal(r.code, 0);
  assert.ok(r.generated);
});

test('appel : une faute de forme fait echouer le generateur, sans rien engendrer', () => {
  const doc = real(); doc.types.volet.positional = 'oui';
  const r = run(doc);
  assert.equal(r.code, 1);
  assert.match(r.out, /forme : \/types\/volet\/positional/);
  assert.equal(r.generated, false);
});

test('appel : une faute de regle fait echouer le generateur, sans rien engendrer', () => {
  const doc = real(); profile(doc, PROFILE.eveno).radio.freq_khz = 871000;
  const r = run(doc);
  assert.equal(r.code, 1);
  assert.match(r.out, /hors de toute bande/);
  assert.equal(r.generated, false);
});

test('appel : un catalogue que le firmware refuserait fait echouer le generateur, sans rien engendrer', () => {
  const doc = real(); doc.protocols.copie = { ...doc.protocols.rts };
  const r = run(doc);
  assert.equal(r.code, 1);
  assert.match(r.out, /deux protocoles pour l'encodeur/);
  assert.equal(r.generated, false);
});

test('firmware : un cycle plus long que le lecteur est refuse a la construction', () => {
  const doc = real(); const b = doc.types.portail.buttons[0];
  b.cycle = Array.from({ length: limits.cycle + 1 }, (_, i) => (i % 2 ? 'stop' : 'up'));
  refuses(doc, new RegExp(`firmware : type portail, cycle de ${limits.cycle + 1} appuis, le lecteur en prend ${limits.cycle}`));
});

test('firmware : un delai entre appuis plus long que le lecteur est refuse', () => {
  const doc = real(); doc.types.portail.buttons[0].press_gap_ms = limits.pressGapMs + 1;
  refuses(doc, /firmware : type portail, press_gap_ms .* au plus/);
});

test('firmware : deux boutons pas a pas sur un type sont refuses', () => {
  const doc = real(); const b = doc.types.portail.buttons[0];
  doc.types.portail.buttons.push({ ...b, id: 'second', position: { row: 1, col: 0 } });
  refuses(doc, /firmware : type portail, 2 boutons pas a pas, le lecteur en prend 1/);
});

/* Les entites que les essais nomment : la famille et la classe du portail, une classe que le firmware connait
 * hors du catalogue, et des valeurs qu'aucun lecteur ne connait. */
const ENTITY = { cover: 'cover', gate: 'gate', garage: 'garage', unknownClass: 'lumiere', unknownComponent: 'light' } as const;
/* Le schema tel qu'on le lit, a modifier dans un essai. */
const schemaCopy = (): Json => JSON.parse(readFileSync(SCHEMA_PATH, 'utf8'));
const entityOf = (sc: Json): Json => sc.$defs.DeviceType.properties.entity.properties;

test("entite : le schema enumere exactement les familles et les classes que le firmware connait", () => {
  assert.deepEqual(checkSchemaFirmware(schemaCopy(), limits), []);
  assert.ok(limits.coverClasses.includes(ENTITY.gate) && limits.components.includes(ENTITY.cover));
});

test("entite : une classe que le schema accepte et pas le firmware est refusee", () => {
  const sc = schemaCopy(); entityOf(sc).device_class.enum.push(ENTITY.unknownClass);
  assert.match(checkSchemaFirmware(sc, limits).join('\n'), new RegExp(`entity\\.device_class enumere .*${ENTITY.unknownClass}`));
});

test("entite : une classe que le firmware connait et que le schema oublie est refusee", () => {
  const sc = schemaCopy(); entityOf(sc).device_class.enum = entityOf(sc).device_class.enum.filter((c: string) => c !== ENTITY.garage);
  assert.match(checkSchemaFirmware(sc, limits).join('\n'), /entity\.device_class/);
});

test("entite : une famille que le firmware n'annonce pas est refusee", () => {
  const sc = schemaCopy(); entityOf(sc).component.enum.push(ENTITY.unknownComponent);
  assert.match(checkSchemaFirmware(sc, limits).join('\n'), /entity\.component/);
});

test("appel : un schema qui diverge du firmware fait echouer le generateur, sans rien engendrer", () => {
  const sc = schemaCopy(); entityOf(sc).device_class.enum.push(ENTITY.unknownClass);
  const r = run(real(), sc);
  assert.equal(r.code, 1);
  assert.match(r.out, /entity\.device_class/);
  assert.equal(r.generated, false);
});

test("forme : une classe d'entite inconnue est refusee", () => {
  const doc = real(); doc.types.portail.entity.device_class = ENTITY.unknownClass;
  refuses(doc, /forme : \/types\/portail\/entity\/device_class/);
});
