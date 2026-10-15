/* Banc de test de rx_confirm (confirmation d'une trame par une seconde identique), sur PC.
 * Voir README.md. Rend 0 si tout passe, 1 sinon. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "rx_confirm.h"

/* Trame de reference : 64 bits (code tournant + serial + bouton), puis VLOW et Repeat. */
#define BASE "1010100010111010011110011001010011110100010100110010101000000001"

static int g_fail = 0;

static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

/* Copie de BASE avec le bit i inverse. */
static const char *flip(int i) {
    static char b[67];
    strcpy(b, BASE "00");
    b[i] = b[i] == '1' ? '0' : '1';
    return b;
}

int main(void) {
    rx_confirm_t c;
    printf("Banc rx_confirm\n");

    /* T0 : une trame seule ne suffit pas, la seconde identique confirme. */
    rx_confirm_reset(&c);
    check(!rx_confirm_push(&c, BASE "00", 66, 0), "T0 1re trame : non confirmee");
    check(rx_confirm_push(&c, BASE "01", 66, 125000), "T0 2e trame (Repeat a 1) : confirmee");
    check(strcmp(c.full, BASE "01") == 0, "T0 trame complete de 66 bits disponible");
    check(rx_confirm_push(&c, BASE "01", 66, 250000), "T0 3e trame : confirmee aussi");

    /* T1 : un bit faux dans les 64 premiers -> pas de confirmation (fantome). */
    rx_confirm_reset(&c);
    rx_confirm_push(&c, BASE "00", 66, 0);
    check(!rx_confirm_push(&c, flip(40), 66, 125000), "T1 bit faux dans le serial : refusee");
    check(!rx_confirm_push(&c, BASE "01", 66, 250000), "T1 la trame suivante ne confirme pas le fantome");
    check(rx_confirm_push(&c, BASE "01", 66, 375000), "T1 puis deux bonnes trames : confirmee");

    /* T2 : VLOW et Repeat (bits 64, 65) ignores. */
    rx_confirm_reset(&c);
    rx_confirm_push(&c, BASE "10", 66, 0);
    check(rx_confirm_push(&c, BASE "01", 66, 125000), "T2 VLOW et Repeat differents : confirmee");

    /* T3 : hors fenetre -> c'est un nouvel appui, pas une confirmation. */
    rx_confirm_reset(&c);
    rx_confirm_push(&c, BASE "00", 66, 0);
    check(!rx_confirm_push(&c, BASE "01", 66, RXC_WINDOW_US + 1), "T3 2e trame trop tardive : non confirmee");
    check(rx_confirm_push(&c, BASE "01", 66, RXC_WINDOW_US + 125001), "T3 la suivante, dans la fenetre : confirmee");

    /* T4 : trames coupees. Moins de 64 bits : ignoree. 64 bits : confirme, mais pas de
     * trame complete tant qu'aucune des deux n'a 66 bits. */
    rx_confirm_reset(&c);
    check(!rx_confirm_push(&c, BASE, 60, 0), "T4 60 bits : ignoree");
    check(c.last[0] == 0, "T4 60 bits : etat inchange");
    rx_confirm_push(&c, BASE, 64, 0);
    check(rx_confirm_push(&c, BASE, 64, 125000), "T4 deux trames de 64 bits : confirmee");
    check(c.full[0] == 0, "T4 aucune trame de 66 bits : full vide");
    check(rx_confirm_push(&c, BASE "01", 66, 250000) && strcmp(c.full, BASE "01") == 0,
          "T4 une 3e trame complete remplit full");

    /* T5 : la 1re trame est complete, la 2e coupee : full garde la 1re. */
    rx_confirm_reset(&c);
    rx_confirm_push(&c, BASE "00", 66, 0);
    check(rx_confirm_push(&c, BASE, 64, 125000) && strcmp(c.full, BASE "00") == 0,
          "T5 1re complete, 2e coupee : full = la 1re");

    /* T6 : un autre bouton (autre code tournant) remet la serie a zero. */
    rx_confirm_reset(&c);
    rx_confirm_push(&c, BASE "00", 66, 0);
    rx_confirm_push(&c, BASE "01", 66, 125000);
    check(!rx_confirm_push(&c, flip(3), 66, 250000), "T6 autre code tournant : non confirmee");
    check(c.full[0] == 0, "T6 full remis a zero");

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
