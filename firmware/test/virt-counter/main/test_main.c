/* Banc de test de virt_counter (compteur des telecommandes virtuelles a la restauration), sur PC.
 * Voir README.md. */
#include <stdbool.h>
#include <stdio.h>
#include "virt_counter.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

int main(void) {
    printf("Banc virt_counter\n");
    check(vc_ahead(501, 500), "T0 un pas d'avance");
    check(!vc_ahead(500, 501), "T1 un pas de retard");
    check(!vc_ahead(500, 500), "T2 egal : pas en avance");
    check(vc_ahead(3, 65530), "T3 apres le retour a 0 : 3 est en avance sur 65530");
    check(!vc_ahead(65530, 3), "T4 avant le retour a 0 : 65530 est en retard sur 3");
    check(vc_ahead(32767, 0), "T5 demi-tour moins un pas : en avance");
    check(!vc_ahead(32768, 0), "T6 demi-tour exact : pas en avance (meme regle que cnt_ahead)");

    /* La restauration : on garde le plus avance du compteur restaure et de celui en place. */
    check(vc_keep(100, 500) == 500, "T7 sauvegarde plus ancienne : le compteur en place est garde");
    check(vc_keep(900, 500) == 900, "T8 sauvegarde plus recente : le compteur restaure est pris");
    check(vc_keep(500, 500) == 500, "T9 egaux : inchange");
    check(vc_keep(65530, 3) == 3, "T10 le compteur en place a repasse 0 : il est garde");
    check(vc_keep(3, 65530) == 3, "T11 la sauvegarde a repasse 0 : elle est prise");

    printf(g_fail ? "\n%d echec(s)\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
