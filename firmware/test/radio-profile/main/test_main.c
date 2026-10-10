/* Banc hote des profils radio (radio_profile.c). Pur, un simple gcc suffit, rend 1 au
 * premier echec. Les mots de frequence attendus sont ceux ecrits a la main dans cc1101.c
 * avant les profils (Profalux 21 66 A5) et mesures sur une installation Eveno (21 65 B6).
 *
 * Mutation : retirer l'arrondi (+13000) de radio_cc1101_freq_word doit faire echouer
 * « eveno -> 0x2165B6 » (la troncature donne 0x2165B5, soit 868,3296 MHz). */
#include "radio_profile.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check_u(const char *name, unsigned long got, unsigned long want) {
    if (got != want) { printf("FAIL %s : rendu 0x%lX, attendu 0x%lX\n", name, got, want); fails++; }
    else             printf("ok   %s\n", name);
}
static void check_s(const char *name, const char *got, const char *want) {
    if (strcmp(got, want)) { printf("FAIL %s : rendu \"%s\", attendu \"%s\"\n", name, got, want); fails++; }
    else                   printf("ok   %s\n", name);
}

int main(void) {
    /* Frequences des profils, et du profil personnalise. */
    check_u("profalux en kHz", radio_profile_freq_khz(RADIO_PROFILE_PROFALUX, 0), 868425);
    check_u("eveno en kHz",    radio_profile_freq_khz(RADIO_PROFILE_EVENO, 0),    868330);
    check_u("custom = saisie", radio_profile_freq_khz(RADIO_PROFILE_CUSTOM, 868100), 868100);
    check_u("hors enum -> defaut", radio_profile_freq_khz((radio_profile_t)99, 0), 868425);

    /* Mots de frequence CC1101 (quartz 26 MHz), arrondis au plus proche. */
    check_u("profalux -> 0x2166A5", radio_cc1101_freq_word(868425), 0x2166A5);
    check_u("eveno    -> 0x2165B6", radio_cc1101_freq_word(868330), 0x2165B6);
    check_u("868350   -> 0x2165E8", radio_cc1101_freq_word(868350), 0x2165E8);
    check_u("863000   -> 0x21313B", radio_cc1101_freq_word(863000), 0x21313B);
    check_u("870000   -> 0x217627", radio_cc1101_freq_word(870000), 0x217627);

    /* Noms <-> profils, aller-retour et inconnus. */
    for (int p = 0; p < RADIO_PROFILE_COUNT; p++) {
        char n[48]; snprintf(n, sizeof(n), "aller-retour %s", radio_profile_name((radio_profile_t)p));
        check_u(n, (unsigned long)radio_profile_from_name(radio_profile_name((radio_profile_t)p)), (unsigned long)p);
    }
    check_s("nom eveno", radio_profile_name(RADIO_PROFILE_EVENO), "eveno");
    check_u("nom inconnu -> -1", (unsigned long)(radio_profile_from_name("somfy") + 1), 0);
    check_u("nom NULL -> -1",    (unsigned long)(radio_profile_from_name(NULL) + 1), 0);
    check_u("casse stricte",     (unsigned long)(radio_profile_from_name("Eveno") + 1), 0);

    /* Plage autorisee (bornes incluses), celle des cartes EU : 863000..870000 kHz. */
    check_u("borne basse incluse", radio_freq_in_band(863000, 863000, 870000), 1);
    check_u("borne haute incluse", radio_freq_in_band(870000, 863000, 870000), 1);
    check_u("sous la plage",       radio_freq_in_band(862999, 863000, 870000), 0);
    check_u("au-dessus",           radio_freq_in_band(870001, 863000, 870000), 0);
    check_u("433 hors plage 868",  radio_freq_in_band(433920, 863000, 870000), 0);

    /* Resolution profil + saisie, controlee contre la plage. */
    uint32_t f = 1;
    check_u("resolve eveno",          radio_profile_resolve(RADIO_PROFILE_EVENO, 0, 863000, 870000, &f), 1);
    check_u("  -> 868330",            f, 868330);
    check_u("resolve custom ok",      radio_profile_resolve(RADIO_PROFILE_CUSTOM, 868100, 863000, 870000, &f), 1);
    check_u("  -> 868100",            f, 868100);
    f = 42;
    check_u("custom hors plage refuse", radio_profile_resolve(RADIO_PROFILE_CUSTOM, 433920, 863000, 870000, &f), 0);
    check_u("  -> frequence inchangee", f, 42);
    check_u("custom 0 refuse",        radio_profile_resolve(RADIO_PROFILE_CUSTOM, 0, 863000, 870000, &f), 0);
    check_u("profil -1 refuse",       radio_profile_resolve(-1, 0, 863000, 870000, &f), 0);
    check_u("profil 3 refuse",        radio_profile_resolve(RADIO_PROFILE_COUNT, 0, 863000, 870000, &f), 0);
    check_u("profil hors plage carte", radio_profile_resolve(RADIO_PROFILE_PROFALUX, 0, 433050, 434790, &f), 0);

    if (fails) { printf("\n%d echec(s)\n", fails); return 1; }
    printf("\ntous les cas passent\n");
    return 0;
}
