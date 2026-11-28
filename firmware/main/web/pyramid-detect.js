/*
 * pyramid-detect.js : lecture des profils du catalogue (profiles.json), et leur classement pour
 * l'ecran « Ajouter un appareil ».
 *
 * resolveProfiles : un profil qui herite d'un autre ('inherits') ne porte que ce qui change. On le
 * complete par sa base : les objets (radio, tx, instructions, test) se fusionnent sur un niveau, le
 * reste est remplace. C'est la SEULE resolution du projet : l'interface, la detection et le
 * generateur (ui/scripts/gen-catalog.ts) passent tous par elle.
 *
 * rankProfiles : a partir des traits CONNUS (observes sur une capture, ou indiques par
 * l'utilisateur), propose le ou les profils qui collent. On PROPOSE, on n'impose pas : l'utilisateur
 * choisit toujours. Ambigu (plusieurs candidats a egalite) = on les montre tous.
 *
 * Les traits viennent du bloc 'traits' du catalogue, un par chemin dans le profil resolu :
 *   - status 'certain' : un trait CONNU qui contredit un profil l'ecarte (jamais impose a tort) ;
 *     un accord remonte son score ;
 *   - 'detected' : observable sur une capture (aujourd'hui la bande, celle de la radio qui a
 *     entendu). Il ne change pas la durete du filtre ; il dit ce que la detection peut remplir seule ;
 *   - un trait qui n'est pas certain (frequence, debit) ne filtre jamais : il se releve ensuite.
 *
 * Rien n'est code en dur ici : ajouter un trait de detection = le declarer dans le catalogue.
 * Module PUR, sans DOM : teste au node (firmware/test/pyramid-detect).
 */
(function (root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PyramidDetect = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const STATUS_CERTAIN = 'certain';
  /* Objets d'un profil fusionnes avec ceux de sa base (un niveau). */
  const MERGED = ['radio', 'tx', 'instructions', 'test'];
  const isObject = (x) => x !== null && typeof x === 'object' && !Array.isArray(x);

  /* Les profils du catalogue, completes par leur base. Une base absente ou une boucle est une
   * erreur : le generateur la refuse avant toute construction, on ne devine rien ici. */
  function resolveProfiles(doc) {
    const list = (doc && doc.profiles) || [];
    const byId = new Map(list.map((p) => [p.id, p]));
    const done = new Map();
    function resolve(p, seen) {
      if (done.has(p.id)) return done.get(p.id);
      if (seen.includes(p.id)) throw new Error(`profil ${p.id} : heritage en boucle (${seen.concat(p.id).join(' > ')})`);
      let out = { ...p };
      if (p.inherits !== undefined) {
        const base = byId.get(p.inherits);
        if (!base) throw new Error(`profil ${p.id} : base ${p.inherits} introuvable`);
        const parent = resolve(base, seen.concat(p.id));
        out = { ...parent, ...p };
        for (const key of MERGED) {
          if (isObject(parent[key]) && isObject(p[key])) out[key] = { ...parent[key], ...p[key] };
        }
        delete out.inherits;
      }
      done.set(p.id, out);
      return out;
    }
    return list.map((p) => resolve(p, []));
  }

  /* Valeur d'un chemin pointe ('radio.band') dans un profil. */
  function valueAt(profile, path) {
    return path.split('.').reduce((x, k) => (isObject(x) ? x[k] : undefined), profile);
  }

  /* Les traits CERTAINS (filtres) et, parmi eux, ceux qu'une capture observe. Tolere un document
   * sans 'traits' (listes vides). */
  function traits(doc) {
    const all = (doc && doc.traits) || {};
    const certain = Object.keys(all).filter((k) => isObject(all[k]) && all[k].status === STATUS_CERTAIN);
    return { certain, detect: certain.filter((k) => all[k].detected === true) };
  }

  /* Classe les profils resolus de `doc` selon les traits CONNUS `observed` (cles = chemins).
   * Rend { ranked, proposed, ambiguous, confident } :
   *   - ranked    : les profils non ecartes, meilleur score d'abord (ordre stable) ;
   *   - proposed  : ceux a egalite au meilleur score (ce qu'on met en avant) ;
   *   - ambiguous : plus d'un propose (on affiche le choix) ;
   *   - confident : exactement un propose (on le preselectionne, l'utilisateur garde la main). */
  function rankProfiles(observed, doc) {
    const known = observed || {};
    const profiles = resolveProfiles(doc);
    const { certain } = traits(doc);

    const ranked = [];
    for (let i = 0; i < profiles.length; i++) {
      const profile = profiles[i];
      let score = 0;
      let excluded = false;
      for (const path of certain) {
        const value = valueAt(profile, path);
        if (known[path] == null || value == null) continue; /* trait non connu, ou absent du profil */
        if (known[path] === value) score++;
        else { excluded = true; break; } /* trait certain connu qui contredit : ecarte */
      }
      if (excluded) continue;
      ranked.push({ index: i, profile, score });
    }

    ranked.sort((a, b) => b.score - a.score || a.index - b.index);
    const top = ranked.length ? ranked[0].score : -1;
    const proposed = ranked.filter((c) => c.score === top);
    return { ranked, proposed, ambiguous: proposed.length > 1, confident: proposed.length === 1 };
  }

  return { resolveProfiles, rankProfiles, traits, valueAt };
});
