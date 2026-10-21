#pragma once
/* req_guard : refuser les envois (POST /api/...) qu'une page d'un AUTRE site fait faire au navigateur (CSRF).
 *
 * Sans mot de passe (le defaut), une page piegee ouverte sur un PC de la maison pouvait piloter le
 * boitier : le navigateur execute un POST vers son adresse locale. Le navigateur dit lui-meme d'ou part
 * la requete :
 *   - Sec-Fetch-Site (navigateurs recents, une page ne peut pas le falsifier) : refus seulement s'il vaut
 *     "cross-site". same-origin, same-site et none passent : l'interface, une iframe dans Home Assistant
 *     (les requetes partent de la page du boitier elle-meme), un proxy ;
 *   - sinon Origin (navigateurs anciens) : son hote doit etre celui de Host (casse et port ignores).
 *     "null" (page isolee, fichier local) est refuse ;
 *   - ni l'un ni l'autre (Home Assistant en REST, curl, scripts) : la requete passe.
 * Les GET ne sont pas concernes : une autre page ne peut pas lire les reponses (aucun en-tete CORS).
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/req-guard. */
#include <stdbool.h>

/* Valeurs des en-tetes Sec-Fetch-Site, Origin et Host, NULL si absent. Vrai si l'envoi est accepte. */
bool rg_write_ok(const char *sec_fetch_site, const char *origin, const char *host);
