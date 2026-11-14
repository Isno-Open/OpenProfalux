/* Banc de test de la taille de la reserve TLS (main/tls_budget.c), sur PC.
 * Les planchers sont les PICS MESURES sur l'ATOM (tls_pool - tls_min publies avec le battement) :
 * la reserve doit les contenir, sinon le TLS deborde sur le tas, qui manquait justement.
 * cc -std=gnu11 -Wall -Wextra -fsanitize=address -g -I ../../main main/test_main.c ../../main/tls_budget.c */
#include <stdbool.h>
#include <stdio.h>
#include "tls_budget.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

/* Pics mesures (octets) : reserve de 24 576 o, 4 364 libres au plus bas (cloud seul, GitHub
 * compris, 2026-11-13) ; reserve de 22 528 o, 832 libres au plus bas (HA mqtts + cloud, GitHub
 * compris, 2026-11-14). */
#define PEAK_CLOUD          (24576 - 4364)
#define PEAK_LOCAL_CLOUD    (22528 - 832)
/* Reserve de 22 016 o, 6 292 libres au plus bas : verification GitHub seule (MQTT local en clair),
 * 2026-11-14. Avec une reserve de 15 872 o, juste au-dessus de ce pic, le morcellement faisait
 * deborder 4 blocs par verification : d'ou la marge exigee au-dessus de chaque pic. */
#define PEAK_GITHUB         (22016 - 6292)
#define FRAG_MARGIN         1024

int main(void) {
    printf("Banc tls_budget\n");
    size_t none   = tls_pool_need(false, false, false, false);
    size_t gh     = tls_pool_need(false, false, false, true);
    size_t loc    = tls_pool_need(true,  false, false, true);
    size_t locm   = tls_pool_need(true,  true,  false, true);
    size_t cloud  = tls_pool_need(false, false, true,  true);
    size_t both   = tls_pool_need(true,  false, true,  true);
    check(none == 0, "T0 aucun TLS attendu : pas de reserve");
    check(gh >= PEAK_GITHUB + FRAG_MARGIN, "T1 verification GitHub seule : couvre le pic mesure (15 724 o) et 1 Ko de morcellement");
    check(tls_pool_need(true, false, false, false) == loc, "T2 HA mqtts sans GitHub auto : meme besoin (une poignee de main de toute facon)");
    check(loc > gh && locm > loc, "T3 une session HA s'ajoute, plus grosse avec certificat client");
    check(cloud >= PEAK_CLOUD + FRAG_MARGIN, "T4 cloud seul : couvre le pic mesure (20 212 o)");
    check(both >= PEAK_LOCAL_CLOUD + FRAG_MARGIN, "T5 HA mqtts + cloud : couvre le pic mesure (21 696 o)");
    check(both - cloud == loc - gh, "T6 les sessions s'additionnent (une poignee de main a la fois)");
    printf(g_fail ? "ECHEC : %d\n" : "TOUT OK\n", g_fail);
    return g_fail ? 1 : 0;
}
