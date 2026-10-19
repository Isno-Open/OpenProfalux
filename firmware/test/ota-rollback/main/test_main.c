/* Banc de test de ota_rollback (retour automatique apres une mise a jour), sur PC.
 * Voir README.md. Rend 0 si tout passe, 1 sinon. */
#include <stdbool.h>
#include <stdio.h>
#include "ota_rollback.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

int main(void) {
    static const unsigned autres[] = { ORB_VALID, ORB_INVALID, ORB_ABORTED, ORB_UNDEFINED };
    printf("Banc ota_rollback\n");

    /* T0 : etat illisible -> on garde ce qu'on savait. */
    check(orb_auto_observe(false, ORB_PENDING_VERIFY, -1) == -1 && orb_auto_observe(false, ORB_NEW, 1) == 1,
          "T0 etat inconnu : rien n'est conclu");

    /* T1 : en essai au demarrage -> seul un bootloader avec l'option l'a mise en essai. */
    check(orb_auto_observe(true, ORB_PENDING_VERIFY, -1) == 1 && orb_auto_observe(true, ORB_PENDING_VERIFY, 0) == 1,
          "T1 en essai : retour automatique actif (meme si vu inactif avant)");

    /* T2 : demarree mais restee NEW -> bootloader d'avant l'option. */
    check(orb_auto_observe(true, ORB_NEW, -1) == 0 && orb_auto_observe(true, ORB_NEW, 1) == 0,
          "T2 restee NEW : retour automatique inactif");

    /* T3 : pas de mise a jour recente -> on garde la derniere observation. */
    bool garde = true;
    for (unsigned i = 0; i < sizeof(autres) / sizeof(autres[0]); i++)
        garde &= orb_auto_observe(true, autres[i], 1) == 1 && orb_auto_observe(true, autres[i], 0) == 0
              && orb_auto_observe(true, autres[i], -1) == -1;
    check(garde, "T3 validee, ecartee, jamais mise a jour : observation gardee");

    /* T4 : redemarrage voulu ou nouvelle mise a jour -> valider SEULEMENT une version en essai. */
    check(orb_validate_now(true, ORB_PENDING_VERIFY), "T4 en essai : validee");
    check(!orb_validate_now(true, ORB_INVALID),
          "T5 apres le bouton Version precedente (INVALID) : jamais revalidee");
    bool rien = !orb_validate_now(false, ORB_PENDING_VERIFY) && !orb_validate_now(true, ORB_NEW)
             && !orb_validate_now(true, ORB_VALID) && !orb_validate_now(true, ORB_ABORTED)
             && !orb_validate_now(true, ORB_UNDEFINED);
    check(rien, "T6 inconnue, NEW, validee, abandonnee, jamais mise a jour : rien a valider");

    /* T7 : affichage "en essai" : la meme regle. */
    check(orb_on_trial(true, ORB_PENDING_VERIFY) && !orb_on_trial(true, ORB_NEW) && !orb_on_trial(true, ORB_VALID)
          && !orb_on_trial(false, ORB_PENDING_VERIFY), "T7 en essai seulement si PENDING_VERIFY");

    /* T8 : derniere image ecartee. */
    check(orb_discarded(true, ORB_ABORTED) == ORB_DISCARD_AUTO, "T8 abandonnee par le bootloader : automatique");
    check(orb_discarded(true, ORB_INVALID) == ORB_DISCARD_MANUAL, "T9 ecartee par le bouton : manuel");
    check(orb_discarded(false, ORB_ABORTED) == ORB_DISCARD_NONE && orb_discarded(true, ORB_VALID) == ORB_DISCARD_NONE
          && orb_discarded(true, ORB_NEW) == ORB_DISCARD_NONE, "T10 aucune image ecartee");

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
