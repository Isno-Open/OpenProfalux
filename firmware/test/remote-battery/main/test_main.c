/* Banc hote de l'alerte pile des telecommandes (remote_battery.c). Pur, un simple gcc
 * suffit, rend 1 au premier echec.
 *
 * Mutation : alerter des le premier appui (BATT_LOW_PRESSES a 1) doit faire echouer
 * « un seul appui VLOW : pas de changement ». */
#include "remote_battery.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char *name, int got, int want) {
    if (got != want) { printf("FAIL %s : rendu %d, attendu %d\n", name, got, want); fails++; }
    else             printf("ok   %s\n", name);
}

int main(void) {
    batt_state_t s; memset(&s, 0, sizeof(s));
    check("etat initial inconnu",          s.known, 0);
    check("1re trame VLOW=0 : changement", batt_on_press(&s, false), 1);
    check("  -> connu, pas d'alerte",      s.known && !s.low, 1);
    check("VLOW=0 encore : pas de changement", batt_on_press(&s, false), 0);

    check("un seul appui VLOW : pas de changement", batt_on_press(&s, true), 0);
    check("  -> pas d'alerte (bit faux possible)",  s.low, 0);
    check("2e appui VLOW d'affilee : changement",   batt_on_press(&s, true), 1);
    check("  -> alerte",                            s.low, 1);
    check("3e appui VLOW : deja en alerte",         batt_on_press(&s, true), 0);

    check("VLOW=0 (pile changee) : changement",     batt_on_press(&s, false), 1);
    check("  -> alerte levee",                      s.low, 0);

    /* VLOW intermittent : jamais deux appuis d'affilee -> jamais d'alerte. */
    for (int i = 0; i < 6; i++) batt_on_press(&s, i % 2 == 0);
    check("VLOW intermittent : pas d'alerte",       s.low, 0);

    /* Premiere trame vue avec VLOW=1 : connu (changement), mais pas encore d'alerte. */
    batt_state_t t; memset(&t, 0, sizeof(t));
    check("1re trame VLOW=1 : changement (connu)",  batt_on_press(&t, true), 1);
    check("  -> pas encore d'alerte",               t.low, 0);

    /* Le compteur ne deborde pas. */
    for (int i = 0; i < 400; i++) batt_on_press(&t, true);
    check("compteur borne, toujours en alerte",     t.low && t.hits == 255, 1);

    if (fails) { printf("\n%d echec(s)\n", fails); return 1; }
    printf("\ntous les cas passent\n");
    return 0;
}
