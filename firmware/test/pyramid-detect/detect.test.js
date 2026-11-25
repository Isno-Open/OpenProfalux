/*
 * Banc node de la detection de profil (firmware/main/web/pyramid-detect.js), sur la VRAIE
 * table profiles.json (source unique). On prouve le contrat : on PROPOSE, l'utilisateur choisit.
 *   - seule la bande est un trait DETECTE (observable d'une capture) ; la modulation est certaine
 *     mais pas detectable a l'aveugle ;
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

const { rankProfiles, traits } = require('../../main/web/pyramid-detect.js');
const DOC = JSON.parse(
  fs.readFileSync(path.join(__dirname, '../../main/web/profiles.json'), 'utf8')
);

const marques = (r) => r.proposed.map((c) => c.profil.marque).sort();
const dansRanked = (r, marque) => r.ranked.some((c) => c.profil.marque === marque);

test('schema : traits certains filtrants, mais seule la bande est detectable', () => {
  const t = traits(DOC);
  assert.deepEqual(t.certain.sort(), ['band', 'code', 'mecanisme', 'modulation']);
  assert.deepEqual(t.detect, ['band']); /* modulation/code/mecanisme : certains mais non detectables */
});

test('bande 433 seule : CAME et Somfy proposes, utilisateur choisit', () => {
  const r = rankProfiles({ band: '433' }, DOC);
  assert.equal(r.ambiguous, true);
  assert.deepEqual(marques(r), ['CAME', 'Somfy RTS']);
});

test('bande 433 + code fixe connu : CAME seul, Somfy ecarte (code incompatible)', () => {
  const r = rankProfiles({ band: '433', code: 'fixe' }, DOC);
  assert.equal(r.confident, true);
  assert.equal(r.proposed[0].profil.marque, 'CAME');
  assert.equal(dansRanked(r, 'Somfy RTS'), false);
});

test('bande 868 : toute la famille KeeLoq 868 proposee (Profalux cloner+nouvelle, Eveno, NeoSol)', () => {
  const r = rankProfiles({ band: '868' }, DOC);
  assert.equal(r.proposed.length, 4);
  assert.ok(r.proposed.every((c) => c.profil.band === '868'));
});

test('bande inconnue : aucune proposition forcee', () => {
  const r = rankProfiles({ band: '915' }, DOC);
  assert.equal(r.proposed.length, 0);
  assert.equal(r.confident, false);
});

test('rien de connu : toute la pyramide est proposee (choix manuel complet)', () => {
  const r = rankProfiles({}, DOC);
  assert.equal(r.proposed.length, DOC.profils.length);
});

test('modulation FSK connue : aucun profil OOK impose', () => {
  const r = rankProfiles({ modulation: 'FSK' }, DOC);
  assert.equal(r.proposed.length, 0);
});
