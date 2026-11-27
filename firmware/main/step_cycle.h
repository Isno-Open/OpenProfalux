/*
 * step_cycle.h : le suivi d'un bouton PAS A PAS (docs/PROFILES.md, section 5.1 ; #134, etape 5).
 *
 * Un portail a bouton unique (CAME et sa famille) n'a qu'une trame : c'est la carte du moteur qui decide ce que
 * fait chaque appui, selon sa logique. Le TYPE declare son cycle (types[].buttons[].cycle, catalog_cycle_t), et le
 * boitier le suit comme la telecommande : il sait ce que fera le prochain appui. Actions : CAT_CMD_UP ouvre,
 * CAT_CMD_STOP arrete, CAT_CMD_DOWN ferme.
 *
 * Module PUR : il ne sait rien de la radio. Il dit COMBIEN d'appuis menent a l'etat voulu, et fait avancer le cycle.
 * Teste sur l'hote (firmware/test/cfg_store, T18). Repris de la PR #146.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "catalog.h"

/* Ce que le boitier croit du portail : sens du mouvement (+1 ouverture, -1 fermeture, 0 immobile) et, immobile,
 * s'il repose en butee ouverte ou fermee (ni l'une ni l'autre : arrete en chemin). */
typedef struct {
    int  dir;
    bool at_open, at_closed;
} step_state_t;

/* Un appui : rend l'action faite a la position `*next` du cycle, et avance `*next`. -1 sans cycle. */
int step_press(const catalog_cycle_t *c, uint8_t *next);

/* Fin de course : la carte du moteur saute le stop qui suivait le mouvement (en butee, l'appui suivant repart dans
 * l'autre sens). Avance `*next` si l'action qu'il designe est un stop. */
void step_travel_end(const catalog_cycle_t *c, uint8_t *next);

/* Nombre d'appuis qui menent a `want` (CAT_CMD_UP, _STOP ou _DOWN) depuis la position `next` et l'etat `s` :
 *   0  : deja dans cet etat (ouvert ou en ouverture, ferme ou en fermeture, immobile pour un stop) ;
 *   >0 : le nombre d'appuis, au plus la longueur du cycle ;
 *   -1 : impossible avec ce cycle (un stop dans un cycle ouvre-ferme, fermer en ouverture seule). */
int step_presses(const catalog_cycle_t *c, uint8_t next, step_state_t s, catalog_cmd_t want);

/* Position du cycle d'un portail au repos dont on ne connait pas le cycle (fiche anterieure au suivi) : ferme, le
 * prochain appui ouvre ; sinon il ferme (ou ouvre, si le cycle ne ferme jamais). */
uint8_t step_next_at_rest(const catalog_cycle_t *c, bool closed);
