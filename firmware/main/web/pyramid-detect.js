/*
 * pyramid-detect.js : classement de profils pour la pyramide « Ajouter un appareil ».
 *
 * A partir des traits CONNUS (observes sur une capture, ou indiques par l'utilisateur), propose
 * le ou les profils de profiles.json qui collent. On PROPOSE, on n'impose pas : l'utilisateur
 * choisit toujours. Ambigu (plusieurs candidats a egalite) = on les montre tous.
 *
 * Deux notions distinctes, tirees de 'traits' dans profiles.json (source unique) :
 *   - statut CERTAIN : un parametre qui se declare. En matching, un trait certain CONNU est un
 *     FILTRE DUR : s'il contredit un profil, ce profil est ecarte (jamais impose a tort).
 *   - drapeau 'detected' : trait OBSERVABLE depuis une capture (aujourd'hui la bande = la radio qui
 *     a entendu). Il ne change PAS la durete du filtre ; il dit seulement ce que le flux de
 *     detection automatique peut remplir tout seul. La modulation est certaine mais NON detectable
 *     a l'aveugle (le CC1101 n'est pas un SDR), donc pas 'detected' : on la declare, on ne la lit pas.
 *   - un parametre INCERTAIN (datarate, frequence) ne sert JAMAIS a filtrer : il se releve ensuite,
 *     pour le profil choisi.
 *
 * Rien n'est code en dur ici : les traits et leur statut sont lus de profiles.json. Un trait est un
 * CHEMIN dans le profil ('radio.band', 'add') : ajouter un trait de detection = le marquer
 * 'detected' dans profiles.json.
 *
 * Module PUR, sans DOM : teste au node (firmware/test/pyramid-detect).
 */
(function (root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PyramidDetect = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const STATUS_CERTAIN = 'certain';

  /* Depuis 'traits' de profiles.json : la liste des traits CERTAINS (filtres de matching) et, parmi
   * eux, ceux marques 'detected' (ce que la detection auto peut observer). Tolere un document sans
   * 'traits' (rend des listes vides). */
  function traits(doc) {
    const all = (doc && doc.traits) || {};
    const certain = Object.keys(all).filter(
      (k) => all[k] && typeof all[k] === 'object' && all[k].status === STATUS_CERTAIN
    );
    return { certain, detect: certain.filter((k) => all[k].detected === true) };
  }

  /* Valeur d'un trait dans un profil : le trait est un chemin pointe ('radio.band'). */
  function valueAt(profile, path) {
    let v = profile;
    for (const part of path.split('.')) {
      if (v == null || typeof v !== 'object') return undefined;
      v = v[part];
    }
    return v;
  }

  /* Classe les profils de `doc` (profiles.json parse) selon les traits CONNUS `observed`, indexes
   * par chemin ({ 'radio.band': '868' }).
   * Un trait certain connu qui CONTREDIT un profil l'ecarte ; un accord remonte son score ;
   * un trait non connu, ou absent du profil (filet de securite), est neutre.
   * Rend { ranked, proposed, ambiguous, confident } :
   *   - ranked   : les profils non ecartes, meilleur score d'abord (ordre stable) ;
   *   - proposed : ceux a egalite au meilleur score (ce qu'on met en avant) ;
   *   - ambiguous: plus d'un propose (on affiche le choix) ;
   *   - confident: exactement un propose (on le pre-selectionne, l'utilisateur garde la main). */
  function rankProfiles(observed, doc) {
    const obs = observed || {};
    const profiles = (doc && doc.profiles) || [];
    const { certain } = traits(doc);

    const ranked = [];
    for (let i = 0; i < profiles.length; i++) {
      const p = profiles[i];
      let score = 0;
      let excluded = false;
      for (const k of certain) {
        const v = valueAt(p, k);
        if (obs[k] == null || v == null) continue; /* trait non connu, ou profil sans ce trait */
        if (obs[k] === v) score++;
        else { excluded = true; break; } /* trait certain connu qui contredit : ecarte */
      }
      if (excluded) continue;
      ranked.push({ index: i, profile: p, score });
    }

    ranked.sort((a, b) => b.score - a.score || a.index - b.index);
    const top = ranked.length ? ranked[0].score : -1;
    const proposed = ranked.filter((c) => c.score === top);
    return {
      ranked,
      proposed,
      ambiguous: proposed.length > 1,
      confident: proposed.length === 1,
    };
  }

  return { rankProfiles, traits, valueAt };
});
