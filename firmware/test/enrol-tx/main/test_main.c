/* Banc de test de enrol_tx (reglages d'emission de la session d'enrolement, issue #159), sur PC.
 * Voir README.md. Rend 0 si tout passe, 1 sinon. */
#include <stdbool.h>
#include <stdio.h>
#include "enrol_tx.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

/* Valeurs du rapport #159 : porteuse et TE mesures sur les telecommandes, a cote du profil Profalux. */
enum { PROFILE_TE_US = 400, MEASURED_KHZ = 868330, MEASURED_TE_US = 439 };

int main(void) {
    enrol_tx_t s;
    printf("Banc enrol_tx\n");

    /* T0 : pas de session : tout herite du profil, la radio garde ses reglages. */
    enrol_tx_clear(&s);
    tx_params_t p = enrol_tx_params(&s);
    check(p.freq_khz == 0 && p.te_us == 0 && p.rpt == -1 && p.rmt == -1, "T0 sans session : tout herite");

    /* T1 : la capture ouvre la session avec les reglages du panneau. */
    enrol_tx_capture(&s, &(enrol_tx_t){ .freq_khz = MEASURED_KHZ, .te_us = MEASURED_TE_US, .rpt = TXOVR_ON, .rmt = TXOVR_OFF });
    p = enrol_tx_params(&s);
    check(p.freq_khz == MEASURED_KHZ && p.te_us == MEASURED_TE_US, "T1 capture : porteuse et TE du panneau");
    check(p.rpt == 1 && p.rmt == 0, "T1 capture : options forcees (oui / non)");

    /* T2 : LE cas de #159. Capture avec les valeurs du profil (rien d'envoye), puis reglages saisis
     * avant d'emettre : l'apprentissage doit partir avec les valeurs saisies. */
    enrol_tx_capture(&s, &(enrol_tx_t){ .freq_khz = 0, .te_us = 0, .rpt = TXOVR_INHERIT, .rmt = TXOVR_INHERIT });
    enrol_tx_learn(&s, &(enrol_tx_t){ .freq_khz = MEASURED_KHZ, .te_us = MEASURED_TE_US, .rpt = TXOVR_INHERIT, .rmt = TXOVR_INHERIT }, true);
    p = enrol_tx_params(&s);
    check(p.freq_khz == MEASURED_KHZ && p.te_us == MEASURED_TE_US, "T2 reglages changes apres la capture : emis a l'apprentissage");

    /* T3 : un apprentissage sans reglages (ancien client) garde ceux de la capture. */
    enrol_tx_learn(&s, &(enrol_tx_t){ .freq_khz = 0, .te_us = 0, .rpt = TXOVR_INHERIT, .rmt = TXOVR_INHERIT }, false);
    p = enrol_tx_params(&s);
    check(p.freq_khz == MEASURED_KHZ && p.te_us == MEASURED_TE_US, "T3 apprentissage sans reglages : ceux de la session restent");

    /* T4 : un apprentissage qui remet « heriter » l'emporte aussi (retour aux valeurs du profil). */
    enrol_tx_learn(&s, &(enrol_tx_t){ .freq_khz = 0, .te_us = 0, .rpt = TXOVR_INHERIT, .rmt = TXOVR_INHERIT }, true);
    p = enrol_tx_params(&s);
    check(p.freq_khz == 0 && p.te_us == 0, "T4 apprentissage revenu au profil : la session herite");

    /* T5 : fin de session (identite deselectionnee ou enregistree) : le prochain enrolement repart du profil. */
    enrol_tx_capture(&s, &(enrol_tx_t){ .freq_khz = MEASURED_KHZ, .te_us = MEASURED_TE_US, .rpt = TXOVR_ON, .rmt = TXOVR_ON });
    enrol_tx_clear(&s);
    p = enrol_tx_params(&s);
    check(p.freq_khz == 0 && p.te_us == 0 && p.rpt == -1 && p.rmt == -1 && s.rpt == TXOVR_INHERIT, "T5 fin de session : tout herite");

    /* T6 : le journal affiche le TE emis, celui de la session, pas celui que la radio a restaure. */
    tx_params_t session = { .freq_khz = 0, .te_us = MEASURED_TE_US, .rpt = -1, .rmt = -1 };
    tx_params_t inherit = { .freq_khz = 0, .te_us = 0, .rpt = -1, .rmt = -1 };
    check(enrol_tx_emitted_te(&session, PROFILE_TE_US) == MEASURED_TE_US, "T6 journal : TE de la session");
    check(enrol_tx_emitted_te(&inherit, PROFILE_TE_US) == PROFILE_TE_US, "T6 journal : TE de la radio quand la session herite");
    check(enrol_tx_emitted_te(NULL, PROFILE_TE_US) == PROFILE_TE_US, "T6 journal : sans reglages, TE de la radio");

    printf(g_fail ? "ECHEC : %d test(s)\n" : "Tout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
