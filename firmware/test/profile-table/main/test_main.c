/*
 * Banc hote de profile_table.c (issue #134, etape 2), contre la table ENGENDREE depuis le VRAI
 * profiles.json par tools/gen_profiles.py (voir run.sh). Ce qu'on prouve :
 *   - chaque profil du catalogue a un moteur et une methode que ce firmware reconnait ;
 *   - une fiche anterieure retrouve le bon profil d'apres sa marque et sa methode (migration) ;
 *   - un profil donne explicitement est garde tant qu'il reste coherent, et suivi s'il ne l'est plus.
 * Mutation : faire rendre PROFILE_ENGINE_UNKNOWN a profile_engine_of casse T1 et T3..T9.
 */
#include <stdio.h>
#include <string.h>
#include "profile_table.h"

static int g_fail = 0;
static void check(int ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}
static int is(const profile_def_t *p, const char *id) { return p && !strcmp(p->id, id); }

/* Les valeurs du domaine que ce banc donne ou attend, declarees une fois : ce sont celles de profiles.json. */
static const char ID_PFX_ENROL[] = "profalux-enrol", ID_PFX_COPY[] = "profalux-copy", ID_EVENO[] = "eveno-enrol",
                  ID_FRANCIAFLEX[] = "franciaflex-enrol", ID_SOMFY[] = "somfy-rts-enrol", ID_CAME[] = "came-replay",
                  ID_UNKNOWN[] = "inconnu", ID_REMOVED[] = "disparu";
static const char BRAND_PFX[] = "Profalux", BRAND_EVENO[] = "Eveno", BRAND_FRANCIAFLEX[] = "Franciaflex", NO_BRAND[] = "";

int main(void) {
    int known = PROFILE_TABLE_COUNT > 0;
    for (int i = 0; i < PROFILE_TABLE_COUNT; i++)
        if (profile_engine_of(&PROFILE_TABLE[i]) == PROFILE_ENGINE_UNKNOWN || profile_add_of(&PROFILE_TABLE[i]) == PROFILE_ADD_UNKNOWN) {
            printf("       profil %s : moteur %s ou methode %s inconnus du firmware\n", PROFILE_TABLE[i].id, PROFILE_TABLE[i].engine, PROFILE_TABLE[i].add);
            known = 0;
        }
    check(known, "T1 chaque profil du catalogue a un moteur et une methode connus du firmware");
    check(profile_find(ID_PFX_ENROL) && !profile_find(ID_UNKNOWN) && !profile_find(NO_BRAND) && !profile_find(NULL), "T2 profile_find : par id, NULL sinon");

    /* Migration : fiche sans id de profil (marque, virtuel, Somfy, rejeu). */
    check(is(profile_resolve(BRAND_PFX, true, false, false), ID_PFX_ENROL), "T3 virtuel Profalux -> profalux-enrol");
    check(is(profile_resolve(NO_BRAND, true, false, false), ID_PFX_ENROL), "T4 virtuel sans marque -> profalux-enrol");
    check(is(profile_resolve(BRAND_EVENO, true, false, false), ID_EVENO), "T5 virtuel Eveno -> eveno-enrol");
    check(is(profile_resolve(NO_BRAND, true, true, false), ID_SOMFY), "T6 virtuel Somfy -> somfy-rts-enrol");
    check(is(profile_resolve(NO_BRAND, false, false, true), ID_CAME), "T7 rejeu sans marque -> came-replay");
    check(is(profile_resolve(NO_BRAND, false, false, false), ID_PFX_COPY), "T8 clone sans marque -> profalux-copy");
    check(is(profile_resolve(BRAND_EVENO, false, false, false), ID_PFX_COPY), "T9 clone d'une marque sans profil de copie -> le premier de la methode");

    /* Coherence d'un profil grave. */
    check(is(profile_for_device(ID_FRANCIAFLEX, BRAND_FRANCIAFLEX, true, false, false), ID_FRANCIAFLEX), "T10 profil explicite et coherent : garde");
    check(is(profile_for_device(ID_FRANCIAFLEX, BRAND_EVENO, true, false, false), ID_EVENO), "T11 marque changee : le profil suit");
    check(is(profile_for_device(ID_PFX_ENROL, BRAND_PFX, false, false, false), ID_PFX_COPY), "T12 methode changee (enrole -> clone) : le profil suit");
    check(is(profile_for_device(ID_REMOVED, BRAND_PFX, true, false, false), ID_PFX_ENROL), "T13 profil inconnu (retire du catalogue) : retrouve");
    check(is(profile_for_device(ID_PFX_ENROL, NO_BRAND, true, false, false), ID_PFX_ENROL), "T14 fiche sans marque : le profil grave est garde");

    printf(g_fail ? "ECHEC : %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
