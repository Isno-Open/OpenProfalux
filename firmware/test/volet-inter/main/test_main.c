/* Banc hote de la position intermediaire (volet_inter.c). Pur, un simple gcc suffit, rend 1
 * au premier echec.
 *
 * Mutations verifiees : accepter 0xB (P + Montee + Descente, la programmation d'un emetteur)
 * comme enregistrement doit faire echouer « 0xB n'est pas l'enregistrement » ; oublier le
 * test des repetitions (target == pct) doit faire echouer « repetition : rien ». */
#include "volet_inter.h"
#include <stdio.h>

static int fails = 0;
static void check(const char *name, int got, int want) {
    if (got != want) { printf("FAIL %s : rendu %d, attendu %d\n", name, got, want); fails++; }
    else             printf("ok   %s\n", name);
}

int main(void) {
    check("0x6 (Montee + Stop) : y aller",     inter_btn_goto(0x6), 1);
    check("0xC (Stop + Descente) : y aller",   inter_btn_goto(0xC), 1);
    check("0x2 (Montee) n'est pas y aller",    inter_btn_goto(0x2), 0);
    check("0xA (Montee + Descente) : enregistrer", inter_btn_save(0xA), 1);
    check("0xB n'est pas l'enregistrement",    inter_btn_save(0xB), 0);
    check("0xA n'est pas y aller",             inter_btn_goto(0xA), 0);

    int d;
    /* position = % de FERMETURE : 0 ouvert (haut), 100 ferme (bas) */
    check("ouvert, cible 60 : suivre",         inter_plan(0, -1, 0, 60, &d), INTER_MOVE);
    check("  -> en descendant",                d, -1);
    check("ferme, cible 60 : suivre",          inter_plan(0, -1, 100, 60, &d), INTER_MOVE);
    check("  -> en montant",                   d, +1);
    check("repetition : rien",                 inter_plan(-1, 60, 10, 60, &d), INTER_KEEP);
    check("en montee vers la butee : suivre", inter_plan(+1, 0, 80, 60, &d), INTER_MOVE);
    check("  -> en montant, cible changee",    d, +1);
    check("deja a 60,5 et arrete : rien",      inter_plan(0, -1, 60.5f, 60, &d), INTER_KEEP);
    check("deja a 60,5 en mouvement : figer",  inter_plan(-1, 100, 60.5f, 60, &d), INTER_FREEZE);
    check("% inconnu, arrete : rien",          inter_plan(0, -1, 30, -1, &d), INTER_KEEP);
    check("% inconnu, en mouvement : figer",   inter_plan(+1, 0, 30, -1, &d), INTER_FREEZE);
    check("% hors bornes : comme inconnu",     inter_plan(+1, 0, 30, 101, &d), INTER_FREEZE);

    printf(fails ? "\n%d ECHEC(S)\n" : "\nTout passe\n", fails);
    return fails ? 1 : 0;
}
