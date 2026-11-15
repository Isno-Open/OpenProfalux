/* Appairage cloud « a la Netflix » (autorisation d'appareil, RFC 8628), cote boitier.
 *
 * Le boitier demande un code au portail (POST <serveur>/api/device/start), l'interface locale affiche
 * le QR code et le code court ; l'utilisateur scanne avec son telephone (ou saisit le code sur
 * <serveur>/lier) et confirme depuis son compte. Pendant ce temps une tache interroge le portail
 * (POST /api/device/poll) ; une fois confirme, le portail rend le certificat UNE fois : le boitier
 * l'enregistre et demarre le cloud, comme l'appairage par code (/api/cloud/pair).
 * Cote portail : OpenCloudApp, web/src/routes/api/device/{start,poll,approve}. */
#ifndef CLOUD_GRANT_H
#define CLOUD_GRANT_H
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

/* Famille de ce firmware pour le portail (champ firmware des boitiers) : le plugin qui sait le lire. */
#define CLOUD_FAMILY "openprofalux"

/* Etat d'un appairage en cours, lu par l'interface (GET /api/cloud/grant). Vocabulaire sur le fil. */
typedef enum { GRANT_IDLE, GRANT_PENDING, GRANT_READY, GRANT_EXPIRED, GRANT_DENIED, GRANT_ERROR } grant_state_t;

/* Commence un appairage (remplace celui en cours) : rend 0 et remplit le JSON a renvoyer a
 * l'interface ({user_code, uri, expires_in}), ou un code HTTP du portail / -1 en cas d'echec. */
int cloud_grant_start(const char *server, char *out, size_t len);

/* Etat courant en JSON : {"state":"pending","user_code":"...","uri":"...","device_id":"..."} */
void cloud_grant_status(char *out, size_t len);

/* Enregistre le paquet d'enrolement rendu par le portail (device_id, broker_url, ca, cert, key),
 * pose la configuration cloud et demarre le client. Partage avec /api/cloud/pair. Rend le
 * device_id (dans dev) ou false si le paquet est incomplet. */
bool cloud_apply_bundle(cJSON *bundle, char *dev, size_t dev_len);

/* POST JSON vers url (TLS par le bundle de certificats) : rend le statut HTTP et la reponse
 * (*resp, a liberer, NULL si rien), ou 0 si la connexion a echoue. */
int cloud_http_post(const char *url, const char *payload, char **resp);

#endif
