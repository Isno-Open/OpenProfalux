/*
 * Reserve de memoire du TLS (mbedTLS), prise au tout debut du demarrage.
 *
 * Pourquoi : avec les tampons dynamiques de mbedTLS, une connexion etablie ne garde presque rien
 * et reprend sa memoire a chaque ecriture, puis a chaque poignee de main. Ce qui a ete consomme
 * ailleurs entre-temps la fait tomber. Mesure le 2026-11-13 sur l'ATOM (0.2.16-cloudtest2) :
 * memoire libre de 27 a 8 Ko, ecriture TLS du cloud en echec, puis poignee de main impossible
 * (alloc(4437) failed) en boucle. Ici, le TLS a SA zone, que rien d'autre ne peut prendre.
 *
 * Sa taille est un besoin MESURE du firmware (voir TLS_POOL_BYTES dans tls_pool.c). Une demande qui ne tient plus dans la reserve deborde sur la memoire generale : le TLS ne
 * casse pas, mais chaque debordement est compte (tls_pool_stats) pour redimensionner la reserve.
 */
#ifndef TLS_POOL_H
#define TLS_POOL_H

#include <stddef.h>
#include <stdint.h>

/* A appeler en TOUT premier dans app_main, avant toute connexion. */
void tls_pool_init(void);

typedef struct {
    uint32_t size;        /* taille de la reserve (0 = pas de reserve) */
    uint32_t free_now;    /* libre a l'instant */
    uint32_t free_min;    /* libre au plus bas depuis le demarrage : ce qu'il faut mesurer */
    uint32_t spills;      /* demandes servies hors reserve, faute de place */
} tls_pool_stats_t;

void tls_pool_stats(tls_pool_stats_t *out);

#endif
