/* Banc du garde-fou reseau (net_watch.c) : quand relancer le Wi-Fi, quand redemarrer. */
#include <stdio.h>
#include <stdlib.h>
#include "net_watch.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("ECHEC : %s\n", msg); failures++; } else printf("ok   : %s\n", msg); } while (0)

static net_watch_action_t run(net_watch_t *w, int n, bool assoc, bool gw) {
    net_watch_action_t a = NW_ACT_NONE;
    for (int i = 0; i < n; i++) a = nw_probe(w, assoc, gw);
    return a;
}

int main(void) {
    net_watch_t w;

    nw_init(&w);
    CHECK(run(&w, 50, true, true) == NW_ACT_NONE, "passerelle qui repond : jamais d'action");

    nw_init(&w);
    CHECK(run(&w, 3 * NW_FAILS_REBOOT, true, false) == NW_ACT_NONE, "passerelle qui n'a jamais repondu (ping filtre) : jamais d'action");

    nw_init(&w);
    nw_probe(&w, true, true);   /* la passerelle repond une premiere fois : le garde-fou s'arme */
    CHECK(run(&w, NW_FAILS_RESTART_WIFI - 1, true, false) == NW_ACT_NONE, "pas d'action avant le seuil de relance");
    CHECK(nw_probe(&w, true, false) == NW_ACT_RESTART_WIFI, "seuil atteint : relance du Wi-Fi");
    CHECK(nw_probe(&w, true, false) == NW_ACT_NONE, "relance tentee une seule fois par panne");
    CHECK(run(&w, NW_FAILS_REBOOT - NW_FAILS_RESTART_WIFI - 2, true, false) == NW_ACT_NONE, "pas de redemarrage avant son seuil");
    CHECK(nw_probe(&w, true, false) == NW_ACT_REBOOT, "panne qui dure : redemarrage");

    nw_init(&w);
    nw_probe(&w, true, true);
    run(&w, NW_FAILS_RESTART_WIFI + 1, true, false);
    CHECK(nw_probe(&w, true, true) == NW_ACT_NONE && w.fails == 0 && !w.wifi_restarted, "passerelle revenue : tout repart a zero");
    CHECK(run(&w, NW_FAILS_RESTART_WIFI, true, false) == NW_ACT_RESTART_WIFI, "nouvelle panne : nouvelle relance possible");

    nw_init(&w);
    nw_probe(&w, true, true);
    CHECK(run(&w, 3 * NW_FAILS_REBOOT, false, false) == NW_ACT_NONE, "Wi-Fi non associe : affaire du garde-fou Wi-Fi, rien ici");
    run(&w, NW_FAILS_REBOOT - 1, true, false);
    CHECK(nw_probe(&w, false, false) == NW_ACT_NONE && w.fails == 0, "association perdue en cours de panne : compteur remis a zero");

    printf("%s (%d echec(s))\n", failures ? "BANC EN ECHEC" : "BANC OK", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
