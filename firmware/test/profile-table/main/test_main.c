/*
 * Banc hote de profile_table.c (issue #134, etape 2), contre la table ENGENDREE depuis le VRAI
 * profiles.json par tools/gen_profiles.py (voir run.sh). Ce qu'on prouve :
 *   - chaque profil du catalogue a un moteur et une methode que ce firmware reconnait ;
 *   - une fiche anterieure retrouve le bon profil d'apres sa marque et sa methode (migration) ;
 *   - un profil donne explicitement est garde tant qu'il reste coherent, et suivi s'il ne l'est plus.
 *   - (lot 2b) chaque profil est complet pour son moteur, et ses valeurs sont celles que le firmware
 *     emettait avant (comportement inchange).
 * Mutation : faire rendre PROFILE_ENGINE_UNKNOWN a profile_engine_of casse T1 et T3..T9 ; retirer un
 * code de bouton d'un profil d'enrolement dans profiles.json casse T15 et T16.
 */
#include <stdio.h>
#include <string.h>
#include "profile_table.h"
#include "dev_type.h"

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

/* Ce que le firmware emettait AVANT le lot 2b, quand ces valeurs etaient des constantes du C (PRESS_REPEATS,
 * PFX_BTN_*, BTN_LEARN, PFX_BURST, PFX_LEARN_ROUNDS, compteur de depart, REJEU_REPEATS, REJEU_GAP_US) : les profils
 * livres doivent les reproduire, sinon le comportement change. */
enum { OLD_PRESS = 10, OLD_UP = 0x2, OLD_STOP = 0x4, OLD_DOWN = 0x8, OLD_LEARN = 0x5, OLD_BURST = 9, OLD_ROUNDS = 5,
       OLD_COUNTER = 3, OLD_REJEU_REPEATS = 8, OLD_REJEU_GAP_US = 12000 };

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

    /* Lot 2b : complet pour son moteur, et comportement inchange. */
    int ready = 1;
    for (int i = 0; i < PROFILE_TABLE_COUNT; i++) {
        const char *why = NULL;
        if (!profile_engine_ready(&PROFILE_TABLE[i], &why)) { printf("       profil %s : %s\n", PROFILE_TABLE[i].id, why); ready = 0; }
    }
    check(ready, "T15 chaque profil est complet pour son moteur");
    int same = 1;
    for (int i = 0; i < PROFILE_TABLE_COUNT; i++) {
        const profile_def_t *p = &PROFILE_TABLE[i];
        if (profile_engine_of(p) == PROFILE_ENGINE_KEELOQ) {
            same &= p->frames_per_press == OLD_PRESS;
            if (profile_add_of(p) == PROFILE_ADD_ENROL)
                same &= p->code_up == OLD_UP && p->code_stop == OLD_STOP && p->code_down == OLD_DOWN && p->enrol_code == OLD_LEARN
                     && p->enrol_frames_per_burst == OLD_BURST && p->enrol_bursts == OLD_ROUNDS && p->enrol_counter_start == OLD_COUNTER;
        }
        if (profile_engine_of(p) == PROFILE_ENGINE_REPLAY) same &= p->repeats == OLD_REJEU_REPEATS && p->gap_us == OLD_REJEU_GAP_US;
        if (!same) { printf("       profil %s : valeurs differentes des anciennes constantes\n", p->id); break; }
    }
    check(same, "T16 les profils livres reproduisent les anciennes constantes (comportement inchange)");
    const profile_def_t *enrol = profile_find(ID_PFX_ENROL);
    check(profile_button_code(enrol, PROFILE_CMD_UP) == OLD_UP && profile_button_code(enrol, PROFILE_CMD_DOWN) == OLD_DOWN
          && profile_button_code(profile_find(ID_PFX_COPY), PROFILE_CMD_UP) == -1 && profile_button_code(NULL, PROFILE_CMD_STOP) == -1,
          "T17 code de bouton : celui du profil, -1 s'il n'en declare pas");

    /* Lot 4 : les types que le firmware connait (dev_type_t) sont declares, avec leur classe Home Assistant. */
    const type_def_t *shutter = type_find(TYPE_VOLET), *gate = type_find(TYPE_PORTAIL);
    check(shutter && gate && shutter->ha_device_class[0] && gate->ha_device_class[0] && !type_find(200),
          "T18 chaque type du firmware est declare, avec sa classe Home Assistant");
    check(shutter && shutter->positional && gate && !gate->positional, "T19 le volet a une position, le portail n'en a pas");

    printf(g_fail ? "ECHEC : %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
