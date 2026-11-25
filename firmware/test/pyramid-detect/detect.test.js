/*
 * Banc node de la detection de profil (firmware/main/web/pyramid-detect.js), sur la VRAIE
 * table profiles.json (source unique). On prouve le contrat : on PROPOSE, l'utilisateur choisit.
 *   - seule la bande est un trait DETECTE (observable d'une capture) ; la modulation est certaine
 *     mais pas detectable a l'aveugle ;
 *   - un trait est un CHEMIN dans le profil ('radio.band') ;
 *   - 433 seul reste ambigu (CAME portail OU Somfy volet) : les deux proposes ;
 *   - un trait certain CONNU qui contredit ecarte le profil (jamais impose) ;
 *   - rien de connu => toute la pyramide (choix manuel complet).
 *
 * Lancer : node --test firmware/test/pyramid-detect/detect.test.js
 * Mutation : ne plus ecarter un profil contredit (retirer le `continue`) casse « FSK n'impose
 * aucun OOK » et « 433 + code fixe : Somfy ecarte ».
 */
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const { rankProfiles, traits, valueAt } = require('../../main/web/pyramid-detect.js');
const DOC = JSON.parse(
  fs.readFileSync(path.join(__dirname, '../../main/web/profiles.json'), 'utf8')
);

/* Les valeurs du domaine que ce banc observe ou attend, declarees une fois (AGENTS.md : pas de texte
 * compare a la main). Ce sont des valeurs du fil, celles de profiles.json. */
const TRAIT = { band: 'radio.band', modulation: 'radio.modulation', code: 'code', add: 'add', engine: 'engine' };
const BAND = { mhz433: '433', mhz868: '868', unknown: '915' };
const CODE = { fixed: 'fixed' };
const MODULATION = { fsk: 'FSK' };
const ADD = { enrol: 'enrol' };
const BRAND = { came: 'CAME', somfy: 'Somfy RTS', velux: 'Velux' };
const ENGINE = { io: 'io-homecontrol' };
const PROFILE_ID = { profaluxCopy: 'profalux-copy' };
const brands = (r) => r.proposed.map((c) => c.profile.brand).sort();
const inRanked = (r, brand) => r.ranked.some((c) => c.profile.brand === brand);
const inProposed = (r, brand) => r.proposed.some((c) => c.profile.brand === brand);

test('traits certains filtrants, mais seule la bande est detectable', () => {
  const t = traits(DOC);
  assert.deepEqual(t.certain.sort(), [TRAIT.add, TRAIT.code, TRAIT.engine, TRAIT.band, TRAIT.modulation].sort());
  assert.deepEqual(t.detect, [TRAIT.band]); /* modulation, code, methode, moteur : certains mais non detectables */
});

test('chaque trait declare existe dans au moins un profil (un trait mal ecrit ne filtrerait rien)', () => {
  for (const k of traits(DOC).certain) {
    assert.ok(DOC.profiles.some((p) => valueAt(p, k) != null), `trait ${k} absent de tous les profils`);
  }
});

test('bande 433 seule : CAME et Somfy proposes, utilisateur choisit', () => {
  const r = rankProfiles({ [TRAIT.band]: BAND.mhz433 }, DOC);
  assert.equal(r.ambiguous, true);
  assert.deepEqual(brands(r), [BRAND.came, BRAND.somfy].sort());
});

test('bande 433 + code fixe connu : CAME seul, Somfy ecarte (code incompatible)', () => {
  const r = rankProfiles({ [TRAIT.band]: BAND.mhz433, [TRAIT.code]: CODE.fixed }, DOC);
  assert.equal(r.confident, true);
  assert.equal(r.proposed[0].profile.brand, BRAND.came);
  assert.equal(inRanked(r, BRAND.somfy), false);
});

test('bande 868 : tous les profils 868 proposes (famille KeeLoq et Velux io), et eux seuls', () => {
  const r = rankProfiles({ [TRAIT.band]: BAND.mhz868 }, DOC);
  assert.equal(r.proposed.length, DOC.profiles.filter((p) => p.radio.band === BAND.mhz868).length);
  assert.ok(r.proposed.every((c) => c.profile.radio.band === BAND.mhz868));
  assert.ok(inProposed(r, BRAND.velux), 'Velux io est un profil 868');
});

test('bande 868 + enrolement : la copie Profalux est ecartee', () => {
  const r = rankProfiles({ [TRAIT.band]: BAND.mhz868, [TRAIT.add]: ADD.enrol }, DOC);
  assert.ok(r.proposed.every((c) => c.profile.add === ADD.enrol));
  assert.equal(r.ranked.some((c) => c.profile.id === PROFILE_ID.profaluxCopy), false);
});

test('bande inconnue : aucune proposition forcee', () => {
  const r = rankProfiles({ [TRAIT.band]: BAND.unknown }, DOC);
  assert.equal(r.proposed.length, 0);
  assert.equal(r.confident, false);
});

test('rien de connu : toute la pyramide est proposee (choix manuel complet)', () => {
  const r = rankProfiles({}, DOC);
  assert.equal(r.proposed.length, DOC.profiles.length);
});

test('modulation FSK connue : aucun profil OOK impose, seul l\'io est propose', () => {
  const r = rankProfiles({ [TRAIT.modulation]: MODULATION.fsk }, DOC);
  assert.ok(r.proposed.length > 0);
  assert.ok(r.proposed.every((c) => c.profile.radio.modulation === MODULATION.fsk && c.profile.engine === ENGINE.io));
});
