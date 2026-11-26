/*
 * Banc node de la lecture des profils et de leur detection (firmware/main/web/pyramid-detect.js),
 * sur le VRAI catalogue profiles.json (source unique). On prouve le contrat : on PROPOSE,
 * l'utilisateur choisit.
 *   - un profil qui herite est complete par sa base, sans perdre ce qu'il change ;
 *   - seule la bande est un trait DETECTE ; la modulation est certaine mais pas detectable ;
 *   - 433 seul reste ambigu (CAME portail OU Somfy volet) : les deux proposes ;
 *   - un trait certain CONNU qui contredit ecarte le profil (jamais impose) ;
 *   - rien de connu : toute la pyramide (choix manuel complet).
 *
 * Lancer : node --test firmware/test/pyramid-detect/detect.test.js
 * Mutations :
 *   - ne plus ecarter un profil contredit (retirer `excluded = true`) casse « FSK n'impose aucun
 *     OOK » et « 433 + protocole came : Somfy ecarte » ;
 *   - ne plus fusionner les objets herites (retirer la boucle MERGED) casse « Eveno garde la bande
 *     de sa base ».
 */
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');

const { resolveProfiles, rankProfiles, traits } = require('../../main/web/pyramid-detect.js');
const DOC = JSON.parse(fs.readFileSync(path.join(__dirname, '../../main/web/profiles.json'), 'utf8'));

const brands = (r) => r.proposed.map((c) => c.profile.brand).sort();
const isRanked = (r, brand) => r.ranked.some((c) => c.profile.brand === brand);
const byId = (id) => resolveProfiles(DOC).find((p) => p.id === id);

test('heritage : Eveno garde la bande et le protocole de sa base, et change sa porteuse', () => {
  const eveno = byId('eveno-enrol');
  assert.equal(eveno.radio.band, '868');
  assert.equal(eveno.radio.modulation, 'OOK');
  assert.equal(eveno.radio.freq_khz, 868330);
  assert.equal(eveno.protocol, 'profalux');
  assert.equal(eveno.inherits, undefined);
  assert.deepEqual(eveno.models, []);
});

test('heritage : une base absente ou une boucle est refusee', () => {
  assert.throws(() => resolveProfiles({ profiles: [{ id: 'a', inherits: 'z' }] }), /introuvable/);
  assert.throws(() => resolveProfiles({ profiles: [{ id: 'a', inherits: 'b' }, { id: 'b', inherits: 'a' }] }), /boucle/);
});

test('traits : certains filtrants, mais seule la bande est detectable', () => {
  const t = traits(DOC);
  assert.deepEqual(t.certain.sort(), ['add', 'protocol', 'radio.band', 'radio.modulation']);
  assert.deepEqual(t.detect, ['radio.band']);
});

test('bande 433 seule : CAME et Somfy proposes, utilisateur choisit', () => {
  const r = rankProfiles({ 'radio.band': '433' }, DOC);
  assert.equal(r.ambiguous, true);
  assert.deepEqual(brands(r), ['CAME', 'Somfy RTS']);
});

test('bande 433 + protocole came : CAME seul, Somfy ecarte', () => {
  const r = rankProfiles({ 'radio.band': '433', protocol: 'came' }, DOC);
  assert.equal(r.confident, true);
  assert.equal(r.proposed[0].profile.brand, 'CAME');
  assert.equal(isRanked(r, 'Somfy RTS'), false);
});

test('bande 868 : toute la famille KeeLoq proposee', () => {
  const r = rankProfiles({ 'radio.band': '868' }, DOC);
  const keeloq = resolveProfiles(DOC).filter((p) => p.protocol === 'profalux');
  assert.equal(r.proposed.length, keeloq.length);
  assert.ok(r.proposed.every((c) => c.profile.radio.band === '868'));
});

test('bande inconnue : aucune proposition forcee', () => {
  const r = rankProfiles({ 'radio.band': '915' }, DOC);
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
