/*
 * Banc hote de profile_table.c (issue #134, etape 2), contre la table ENGENDREE depuis le VRAI
 * profiles.json par tools/gen_profiles.py (voir run.sh). Ce qu'on prouve :
 *   - chaque profil du catalogue a un moteur et une methode que ce firmware reconnait ;
 *   - une fiche anterieure retrouve le bon profil d'apres sa marque et sa methode (migration) ;
 *   - un profil donne explicitement est garde tant qu'il reste coherent, et suivi s'il ne l'est plus.
 *   - (lot 2b) chaque profil est complet pour son moteur, et ses valeurs sont celles que le firmware
 *     emettait avant (comportement inchange).
 * (lot 5) le bouton pas a pas : le cycle du portail CAME, et le nombre d'appuis vers chaque etat (T20..T28).
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
/* Noms de fil des commandes d'un bouton (profiles.json types[].buttons[].cmd). */
static const char CMD_STEP[] = "step", CMD_UP[] = "up", CMD_UNKNOWN[] = "x";
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
        if (profile_engine_of(p) == PROFILE_ENGINE_REPLAY)
            for (int c = 0; c < PROFILE_CMD_COUNT; c++)
                if (profile_replay_repeats(p, (profile_cmd_t)c))
                    same &= profile_replay_repeats(p, (profile_cmd_t)c) == OLD_REJEU_REPEATS && profile_replay_gap_us(p, (profile_cmd_t)c) == OLD_REJEU_GAP_US;
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

    /* Bouton pas a pas (#134, section 4 bis). Le profil CAME declare le cycle ouvre-stop-ferme-stop des cartes CAME
     * (DIP 2 sur ON, notice de la serie Z) ; le rejeu de son bouton garde les anciennes valeurs. */
    const profile_def_t *came = profile_find(ID_CAME);
    const step_cycle_t osfs = { .len = 4, .act = { STEP_ACT_OPEN, STEP_ACT_STOP, STEP_ACT_CLOSE, STEP_ACT_STOP } };
    check(came && profile_has_step(came) && !memcmp(&came->step, &osfs, sizeof osfs) && came->step_press_gap_ms > 0
          && profile_replay_repeats(came, PROFILE_CMD_STEP) == OLD_REJEU_REPEATS && !profile_has_step(enrol),
          "T20 le portail CAME declare son cycle ouvre-stop-ferme-stop et le rejeu de son bouton");
    check(profile_cmd_of(CMD_STEP) == PROFILE_CMD_STEP && profile_cmd_of(CMD_UP) == PROFILE_CMD_UP && profile_cmd_of(CMD_UNKNOWN) == -1
          && !strcmp(profile_cmd_name(PROFILE_CMD_STEP), CMD_STEP), "T21 commandes : nom du fil <-> profile_cmd_t");

    /* Le cycle, appui par appui, sur ouvre-stop-ferme-stop : ferme -> ouvre -> stop -> ferme -> stop -> ouvre. */
    uint8_t n = step_next_at_rest(&osfs, true);
    step_act_t seq[5];
    for (int i = 0; i < 5; i++) seq[i] = step_press(&osfs, &n);
    check(seq[0] == STEP_ACT_OPEN && seq[1] == STEP_ACT_STOP && seq[2] == STEP_ACT_CLOSE && seq[3] == STEP_ACT_STOP && seq[4] == STEP_ACT_OPEN,
          "T22 appuis successifs : ouvre, stop, ferme, stop, ouvre");
    /* Fin de course : en butee, la carte saute le stop, l'appui suivant repart dans l'autre sens. */
    n = step_next_at_rest(&osfs, true); step_press(&osfs, &n);   /* ouverture en cours, prochain = stop */
    step_travel_end(&osfs, &n);
    check(step_press(&osfs, &n) == STEP_ACT_CLOSE, "T23 ouvert en butee : l'appui suivant ferme");

    /* Nombre d'appuis vers l'etat voulu. */
    const step_state_t opening = { .dir = +1 }, closing = { .dir = -1 }, closed = { .at_closed = true }, open_ = { .at_open = true }, halted = { 0 };
    uint8_t after_open = 1, after_close = 3, at_rest_closed = 0, at_rest_open = 2;   /* positions du cycle osfs */
    check(step_presses(&osfs, at_rest_closed, closed, STEP_ACT_OPEN) == 1 && step_presses(&osfs, at_rest_closed, closed, STEP_ACT_CLOSE) == 0
          && step_presses(&osfs, at_rest_open, open_, STEP_ACT_CLOSE) == 1 && step_presses(&osfs, at_rest_open, open_, STEP_ACT_OPEN) == 0,
          "T24 au repos en butee : un appui vers l'autre butee, aucun vers la sienne");
    check(step_presses(&osfs, after_open, opening, STEP_ACT_CLOSE) == 2 && step_presses(&osfs, after_open, opening, STEP_ACT_STOP) == 1
          && step_presses(&osfs, after_open, opening, STEP_ACT_OPEN) == 0 && step_presses(&osfs, after_close, closing, STEP_ACT_OPEN) == 2,
          "T25 en mouvement : stop puis sens inverse (2 appuis), stop (1), deja dans le sens voulu (0)");
    check(step_presses(&osfs, at_rest_open, halted, STEP_ACT_STOP) == 0 && step_presses(&osfs, at_rest_open, halted, STEP_ACT_OPEN) == 3,
          "T26 arrete en chemin apres ouverture : rouvrir demande ferme, stop, ouvre");
    /* Les autres logiques de carte : ouvre-ferme (pas d'arret possible), ouverture seule (pas de fermeture). */
    const step_cycle_t oc = { .len = 2, .act = { STEP_ACT_OPEN, STEP_ACT_CLOSE } }, oo = { .len = 1, .act = { STEP_ACT_OPEN } };
    check(step_presses(&oc, 1, opening, STEP_ACT_STOP) == -1 && step_presses(&oc, 1, opening, STEP_ACT_CLOSE) == 1
          && step_presses(&oo, 0, open_, STEP_ACT_CLOSE) == -1 && step_presses(&oo, 0, closed, STEP_ACT_OPEN) == 1,
          "T27 ouvre-ferme : stop impossible ; ouverture seule : fermer impossible");
    check(step_presses(NULL, 0, closed, STEP_ACT_OPEN) == -1 && step_press(NULL, &n) == STEP_ACT_NONE,
          "T28 sans cycle : rien a faire");

    printf(g_fail ? "ECHEC : %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
