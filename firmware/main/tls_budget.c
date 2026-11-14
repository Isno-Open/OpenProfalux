/* Taille de la reserve TLS selon la configuration : voir tls_pool.h. Unite PURE, testee sur PC
 * (firmware/test/tls-budget). */
#include "tls_budget.h"

/* Taille de la reserve : le BESOIN de la configuration du boitier, pas une propriete de la carte.
 * Les poignees de main passant une par une (tour TLS, voir tls_pool.h), elle tient UNE poignee de main
 * plus une session au repos par connexion TLS permanente. Mesures du 2026-11-13 et 14 sur l'ATOM
 * (tls_free et tls_min publies avec le battement), reserve de 22 528 o :
 *   cloud seul        : 5 980 o au repos, 2 248 o libres au plus bas -> poignee de main ~14,3 Ko ;
 *   HA mqtts + cloud  : 7 404 o au repos (session HA sans certificat client ~1,4 Ko),
 *                       832 o libres au plus bas, verification GitHub comprise, 0 debordement.
 *   GitHub seul       : reserve de 22 016 o, 6 292 o libres au plus bas -> pic de 15 724 o, etat
 *                       PSA de repos compris (~1,1 Ko tenus sans aucune session). La chaine de
 *                       certificats de GitHub est plus lourde que celle d'un broker : c'est elle qui
 *                       fixe la poignee de main. Avec 15 872 o (ancienne valeur), 148 o de jeu : le
 *                       morcellement faisait deborder 4 blocs par verification (2026-11-14).
 * Le banc exige 1 Ko au-dessus de chaque pic mesure, pour ce morcellement. */
#define TLS_HANDSHAKE_BYTES     (16384)   /* une poignee de main a la fois, celle de GitHub (15 724 o) */
#define TLS_SESSION_MTLS_BYTES  (6144)    /* session au repos avec certificat client (cloud) */
#define TLS_SESSION_BYTES       (1536)    /* session au repos sans certificat client (HA mqtts) */
#define TLS_POOL_MARGIN_BYTES   (1024)

size_t tls_pool_need(bool local_tls, bool local_mtls, bool cloud, bool github_auto) {
    if (!local_tls && !cloud && !github_auto) return 0;   /* aucun TLS attendu : pas de reserve */
    size_t n = TLS_HANDSHAKE_BYTES + TLS_POOL_MARGIN_BYTES;
    if (local_tls) n += local_mtls ? TLS_SESSION_MTLS_BYTES : TLS_SESSION_BYTES;
    if (cloud)     n += TLS_SESSION_MTLS_BYTES;
    return n;
}
