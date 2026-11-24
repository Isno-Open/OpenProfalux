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

const BAND = 'radio.band';
const brands = (r) => r.proposed.map((c) => c.profile.brand).sort();
const inRanked = (r, brand) => r.ranked.some((c) => c.profile.brand === brand);

test('traits certains filtrants, mais seule la bande est detectable', () => {
  const t = traits(DOC);
  assert.deepEqual(t.certain.sort(), ['add', 'code', 'engine', 'radio.band', 'radio.modulation']);
  assert.deepEqual(t.detect, [BAND]); /* modulation, code, methode, moteur : certains mais non detectables */
});

test('chaque trait declare existe dans au moins un profil (un trait mal ecrit ne filtrerait rien)', () => {
  for (const k of traits(DOC).certain) {
    assert.ok(DOC.profiles.some((p) => valueAt(p, k) != null), `trait ${k} absent de tous les profils`);
  }
});

test('bande 433 seule : CAME et Somfy proposes, utilisateur choisit', () => {
  const r = rankProfiles({ [BAND]: '433' }, DOC);
  assert.equal(r.ambiguous, true);
  assert.deepEqual(brands(r), ['CAME', 'Somfy RTS']);
});

test('bande 433 + code fixe connu : CAME seul, Somfy ecarte (code incompatible)', () => {
  const r = rankProfiles({ [BAND]: '433', code: 'fixed' }, DOC);
  assert.equal(r.confident, true);
  assert.equal(r.proposed[0].profile.brand, 'CAME');
  assert.equal(inRanked(r, 'Somfy RTS'), false);
});

test('bande 868 : toute la famille KeeLoq 868 proposee (Profalux copie et nouvelle, Franciaflex, Eveno, NeoSol)', () => {
  const r = rankProfiles({ [BAND]: '868' }, DOC);
  assert.equal(r.proposed.length, 5);
  assert.ok(r.proposed.every((c) => c.profile.radio.band === '868'));
});

test('bande 868 + enrolement : la copie Profalux est ecartee', () => {
  const r = rankProfiles({ [BAND]: '868', add: 'enrol' }, DOC);
  assert.ok(r.proposed.every((c) => c.profile.add === 'enrol'));
  assert.equal(r.ranked.some((c) => c.profile.id === 'profalux-copy'), false);
});

test('bande inconnue : aucune proposition forcee', () => {
  const r = rankProfiles({ [BAND]: '915' }, DOC);
  assert.equal(r.proposed.length, 0);
  assert.equal(r.confident, false);
});

test('rien de connu : toute la pyramide est proposee (choix manuel complet)', () => {
  const r = rankProfiles({}, DOC);
  assert.equal(r.proposed.length, DOC.profiles.length);
});

test('modulation FSK connue : aucun profil OOK impose', () => {
  const r = rankProfiles({ 'radio.modulation': 'FSK' }, DOC);
  assert.equal(r.proposed.length, 0);
});
