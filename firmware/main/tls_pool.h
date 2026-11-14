/*
 * Reserve de memoire du TLS (mbedTLS), prise au tout debut du demarrage.
 *
 * Pourquoi : avec les tampons dynamiques de mbedTLS, une connexion etablie ne garde presque rien
 * et reprend sa memoire a chaque ecriture, puis a chaque poignee de main. Ce qui a ete consomme
 * ailleurs entre-temps la fait tomber. Mesure le 2026-11-13 sur l'ATOM (0.2.16-cloudtest2) :
 * memoire libre de 27 a 8 Ko, ecriture TLS du cloud en echec, puis poignee de main impossible
 * (alloc(4437) failed) en boucle. Ici, le TLS a SA zone, que rien d'autre ne peut prendre.
 *
 * Sa taille est le besoin MESURE de la configuration du boitier (voir tls_pool_need dans tls_budget.c). Une demande qui ne tient plus dans la reserve deborde sur la memoire generale : le TLS ne
 * casse pas, mais chaque debordement est compte (tls_pool_stats) pour redimensionner la reserve.
 */
#ifndef TLS_POOL_H
#define TLS_POOL_H

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>
#include "tls_budget.h"   /* tls_pool_need : la taille selon la configuration */
/* A appeler des la configuration lue, avant toute connexion et avant que le demarrage ne
 * morcelle la memoire. bytes = tls_pool_need(...) ; 0 = pas de reserve. */
void tls_pool_init(size_t bytes);

typedef struct {
    uint32_t size;        /* taille de la reserve (0 = pas de reserve) */
    uint32_t free_now;    /* libre a l'instant */
    uint32_t free_min;    /* libre au plus bas depuis le demarrage : ce qu'il faut mesurer */
    uint32_t spills;      /* demandes servies hors reserve, faute de place */
} tls_pool_stats_t;

void tls_pool_stats(tls_pool_stats_t *out);

/* ── Tour de poignee de main ──
 * Une seule poignee de main TLS a la fois (broker local, cloud, GitHub). La reserve tient une
 * poignee de main (~14 Ko) plus les sessions au repos ; deux poignees de main simultanees
 * (local et cloud au demarrage) la debordaient de ~14 Ko, sur un tas deja pris par le Wi-Fi,
 * le web et la radio. Mesure le 2026-11-14 sur l'ATOM, HA en mqtts + cloud mTLS + ecoute
 * permanente : minimum du tas a 216 o des le demarrage, web muet, cloud jamais connecte.
 * tls_turn_take attend son tour au plus wait_ms (false = pas eu le tour : la poignee de main
 * se fait quand meme, hors tour, et c'est compte) ; tls_turn_give rend le tour. */
#include <stdbool.h>
/* Attente d'un tour : au-dela d'une poignee de main normale (delai reseau de 10 s par essai,
 * esp-mqtt et esp_http_client), avec la marge d'une deuxieme en attente. */
#define TLS_TURN_WAIT_MS 30000
bool tls_turn_take(uint32_t wait_ms);
void tls_turn_give(void);
uint32_t tls_turn_misses(void);   /* poignees de main faites hors tour (attente depassee) */

#endif
