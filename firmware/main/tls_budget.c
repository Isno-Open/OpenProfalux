/* Taille de la reserve TLS selon la configuration : voir tls_pool.h. Unite PURE, testee sur PC
 * (firmware/test/tls-budget). */
#include "tls_budget.h"

/* Taille de la reserve : le BESOIN de la configuration du boitier, pas une propriete de la carte.
 * Les poignees de main passant une par une (tour TLS, voir tls_pool.h), elle tient UNE poignee de main
 * plus une session au repos par connexion TLS permanente. Mesures du 2026-11-13 et 14 sur l'ATOM
 * (tls_free et tls_min publies avec le battement), reserve de 22 528 o :
 *   cloud seul        : 5 980 o au repos, 2 248 o libres au plus bas -> poignee de main ~14,3 Ko ;
 *   HA mqtts + cloud  : 7 404 o au repos (session HA sans certificat client ~1,4 Ko),
 *                       832 o libres au plus bas, verification GitHub comprise, 0 debordement. */
#define TLS_HANDSHAKE_BYTES     (14848)   /* une poignee de main a la fois : ~14,3 Ko + 512 o */
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
