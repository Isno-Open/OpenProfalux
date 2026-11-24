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

int main(void) {
    int known = PROFILE_TABLE_COUNT > 0;
    for (int i = 0; i < PROFILE_TABLE_COUNT; i++)
        if (profile_engine_of(&PROFILE_TABLE[i]) == PROFILE_ENGINE_UNKNOWN || profile_add_of(&PROFILE_TABLE[i]) == PROFILE_ADD_UNKNOWN) {
            printf("       profil %s : moteur %s ou methode %s inconnus du firmware\n", PROFILE_TABLE[i].id, PROFILE_TABLE[i].engine, PROFILE_TABLE[i].add);
            known = 0;
        }
    check(known, "T1 chaque profil du catalogue a un moteur et une methode connus du firmware");
    check(profile_find("profalux-enrol") && !profile_find("inconnu") && !profile_find("") && !profile_find(NULL), "T2 profile_find : par id, NULL sinon");

    /* Migration : fiche sans id de profil (marque, virtuel, Somfy, rejeu). */
    check(is(profile_resolve("Profalux", true, false, false), "profalux-enrol"), "T3 virtuel Profalux -> profalux-enrol");
    check(is(profile_resolve("", true, false, false), "profalux-enrol"), "T4 virtuel sans marque -> profalux-enrol");
    check(is(profile_resolve("Eveno", true, false, false), "eveno-enrol"), "T5 virtuel Eveno -> eveno-enrol");
    check(is(profile_resolve("", true, true, false), "somfy-rts-enrol"), "T6 virtuel Somfy -> somfy-rts-enrol");
    check(is(profile_resolve("", false, false, true), "came-replay"), "T7 rejeu sans marque -> came-replay");
    check(is(profile_resolve("", false, false, false), "profalux-copy"), "T8 clone sans marque -> profalux-copy");
    check(is(profile_resolve("Eveno", false, false, false), "profalux-copy"), "T9 clone d'une marque sans profil de copie -> le premier de la methode");

    /* Coherence d'un profil grave. */
    check(is(profile_for_device("franciaflex-enrol", "Franciaflex", true, false, false), "franciaflex-enrol"), "T10 profil explicite et coherent : garde");
    check(is(profile_for_device("franciaflex-enrol", "Eveno", true, false, false), "eveno-enrol"), "T11 marque changee : le profil suit");
    check(is(profile_for_device("profalux-enrol", "Profalux", false, false, false), "profalux-copy"), "T12 methode changee (enrole -> clone) : le profil suit");
    check(is(profile_for_device("disparu", "Profalux", true, false, false), "profalux-enrol"), "T13 profil inconnu (retire du catalogue) : retrouve");
    check(is(profile_for_device("profalux-enrol", "", true, false, false), "profalux-enrol"), "T14 fiche sans marque : le profil grave est garde");

    printf(g_fail ? "ECHEC : %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
