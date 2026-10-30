/* Banc de test de enrol_gate (apprentissage permis seulement apres une capture reussie dans
 * la tentative en cours, issue #53), sur PC. Voir README.md. Rend 0 si tout passe, 1 sinon. */
#include <stdbool.h>
#include <stdio.h>
#include "enrol_gate.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

int main(void) {
    enrol_gate_t g;
    printf("Banc enrol_gate\n");

    /* T0 : demarrage (identite eventuellement restee en NVS) : pas d'apprentissage. */
    eg_init(&g);
    check(!eg_learn_allowed(&g) && eg_refusal(&g), "T0 demarrage : apprentissage refuse");

    /* T1 : capture reussie -> apprentissage permis, avec la telecommande captee. */
    eg_capture_start(&g);
    check(!eg_learn_allowed(&g), "T1 capture en cours : refuse");
    eg_capture_result(&g, true, 0, 0x054CA93);
    check(eg_learn_allowed(&g) && !eg_refusal(&g) && g.remote == 0x054CA93, "T1 capture reussie : permis");

    /* T2 : LE cas de #53. Une capture reussie, puis une nouvelle capture RATEE : l'identite
     * d'avant ne doit pas rester utilisable. */
    eg_capture_start(&g);
    eg_capture_result(&g, false, -3, 0);
    check(!eg_learn_allowed(&g) && g.remote == 0, "T2 capture ratee apres une reussie : refuse");
    check(g.state == EG_FAILED, "T2 message d'echec (rapprocher et recommencer)");

    /* T3 : trame recue mais identite refusee (table pleine) : refuse aussi. */
    eg_capture_start(&g);
    eg_capture_result(&g, true, -1, 0x0BEC235);
    check(!eg_learn_allowed(&g), "T3 trame recue, identite refusee : refuse");

    /* T4 : oubli ou volet enregistre apres une reussite : la porte se referme. */
    eg_capture_start(&g); eg_capture_result(&g, true, 0, 0x054CA93);
    eg_reset(&g);
    check(!eg_learn_allowed(&g), "T4 oubli / volet enregistre : refuse");

    /* T5 : un resultat qui arrive hors capture (perime) ne rouvre pas la porte. */
    eg_init(&g);
    eg_capture_result(&g, true, 0, 0x054CA93);
    check(!eg_learn_allowed(&g), "T5 resultat hors capture : ignore");

    /* T6 : relancer une capture apres une reussite referme la porte jusqu'au resultat. */
    eg_capture_start(&g); eg_capture_result(&g, true, 0, 0x054CA93);
    eg_capture_start(&g);
    check(!eg_learn_allowed(&g), "T6 nouvelle capture lancee : refuse jusqu'au resultat");

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
