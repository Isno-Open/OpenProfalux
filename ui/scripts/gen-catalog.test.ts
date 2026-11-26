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
import { CATALOG_PATH, SCHEMA_PATH, boardBands, checkCatalog, checkShape, spriteIcons } from './gen-catalog.ts';

type Json = Record<string, any>;   // un catalogue qu'on casse expres : sa forme n'est plus garantie
const real = (): Json => JSON.parse(readFileSync(CATALOG_PATH, 'utf8'));
const schema: object = JSON.parse(readFileSync(SCHEMA_PATH, 'utf8'));
const bands = boardBands();
const icons = spriteIcons();
const profile = (doc: Json, id: string): Json => doc.profiles.find((p: Json) => p.id === id);

/* La regle refuse ce catalogue, et son message le dit. */
function refuses(doc: Json, pattern: RegExp): void {
  const errors = [...checkShape(doc, schema), ...checkCatalog(doc, bands, icons)];
  assert.ok(errors.some(e => pattern.test(e)), `attendu ${pattern}, obtenu :\n${errors.join('\n') || '(rien)'}`);
}

test('le vrai catalogue passe la forme et les regles', () => {
  assert.deepEqual(checkShape(real(), schema), []);
  assert.deepEqual(checkCatalog(real(), bands, icons), []);
});

test('forme : une cle inconnue est refusee', () => {
  const doc = real(); profile(doc, 'came-replay').colour = 'bleu';
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
  const doc = real(); profile(doc, 'came-replay').protocol = 'nice';
  refuses(doc, /profil came-replay : protocole nice inconnu/);
});

test('profil : un type inconnu est refuse', () => {
  const doc = real(); profile(doc, 'came-replay').types = ['garage'];
  refuses(doc, /profil came-replay : type garage inconnu/);
});

test("profil : une methode d'ajout sans procedure dans le protocole est refusee", () => {
  const doc = real(); profile(doc, 'came-replay').add = 'enrol';
  refuses(doc, /profil came-replay : le protocole came n'a pas de procedure enrol/);
});

test('profil : un protocole a codes ne porte pas un bouton dont il ne connait pas la commande', () => {
  const doc = real(); profile(doc, 'somfy-rts-enrol').types = ['portail'];
  refuses(doc, /profil somfy-rts-enrol : le protocole rts n'a pas de code pour la commande step du type portail/);
});

test('profil : une instruction exigee par la procedure doit exister dans le profil', () => {
  const doc = real(); delete profile(doc, 'profalux-enrol').instructions;
  refuses(doc, /profil profalux-enrol : la procedure enrol montre instructions\.gestures/);
});

test("profil : une porteuse hors de la bande qu'une carte autorise est refusee", () => {
  const doc = real(); profile(doc, 'eveno-enrol').radio.freq_khz = 871000;
  refuses(doc, /profil eveno-enrol : 871000 kHz hors de toute bande 868 autorisee/);
});

test('profil : un champ ni declare ni herite est refuse', () => {
  const doc = real(); delete profile(doc, 'somfy-rts-enrol').radio.modulation;
  refuses(doc, /profil somfy-rts-enrol : radio\.modulation manquant/);
});

test('heritage : une base absente est refusee', () => {
  const doc = real(); profile(doc, 'eveno-enrol').inherits = 'nulle-part';
  refuses(doc, /profil eveno-enrol : base nulle-part introuvable/);
});

test('heritage : une boucle est refusee', () => {
  const doc = real(); profile(doc, 'profalux-enrol').inherits = 'eveno-enrol';
  refuses(doc, /heritage en boucle/);
});

test('heritage : changer la bande de sa base est refuse', () => {
  const doc = real(); profile(doc, 'eveno-enrol').radio.band = '433';
  refuses(doc, /profil eveno-enrol : change la bande de sa base profalux-enrol/);
});

test('heritage : changer le protocole de sa base est refuse', () => {
  const doc = real(); profile(doc, 'eveno-enrol').protocol = 'rts';
  refuses(doc, /profil eveno-enrol : change le protocole de sa base profalux-enrol/);
});

test('profils : un identifiant en double est refuse', () => {
  const doc = real(); profile(doc, 'eveno-enrol').id = 'neosol-enrol';
  refuses(doc, /profil neosol-enrol : identifiant en double/);
});

test('profils : un modele reconnu par deux profils est refuse', () => {
  const doc = real(); profile(doc, 'neosol-enrol').models = ['FranciaFlex M4G'];
  refuses(doc, /profils profalux-enrol et neosol-enrol : meme modele FranciaFlex M4G/);
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
function run(doc: Json): { code: number; out: string; generated: boolean } {
  const dir = mkdtempSync(join(tmpdir(), 'catalog-'));
  const input = join(dir, 'profiles.json'), output = join(dir, 'catalog.generated.ts');
  writeFileSync(input, JSON.stringify(doc));
  const r = spawnSync(process.execPath, [script, input, output], { encoding: 'utf8' });
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
  const doc = real(); profile(doc, 'eveno-enrol').radio.freq_khz = 871000;
  const r = run(doc);
  assert.equal(r.code, 1);
  assert.match(r.out, /hors de toute bande/);
  assert.equal(r.generated, false);
});
