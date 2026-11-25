/*
 * step_cycle.h : le suivi d'un bouton PAS A PAS (comportement « step » de #134, section 4 bis).
 *
 * Un portail a bouton unique (CAME et sa famille) n'a qu'une trame : c'est la carte du moteur qui decide ce que
 * fait chaque appui, selon sa logique, reglee par interrupteurs. La notice CAME de la serie Z en donne trois :
 * ouvre-stop-ferme-stop, ouvre-ferme, ouverture seule. Le PROFIL declare la sienne (frames.<bouton>.cycle), et
 * le boitier suit le cycle comme la telecommande : il sait quelle action fera le prochain appui.
 *
 * Ce module ne sait rien de la radio : il dit COMBIEN d'appuis menent a l'etat voulu, et fait avancer le cycle.
 * Module PUR (aucune dependance ESP-IDF), teste sur l'hote (firmware/test/profile-table).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Ce que fait un appui, a une position du cycle. */
typedef enum {
    STEP_ACT_NONE = 0,
    STEP_ACT_OPEN,    /* "open"  : le portail part en ouverture */
    STEP_ACT_STOP,    /* "stop"  : il s'arrete ou il est */
    STEP_ACT_CLOSE,   /* "close" : il part en fermeture */
} step_act_t;

/* Plus long cycle declarable (ouvre-stop-ferme-stop). */
#define STEP_CYCLE_MAX 4

typedef struct {
    uint8_t len;                     /* 0 = pas de bouton pas a pas */
    uint8_t act[STEP_CYCLE_MAX];     /* step_act_t, dans l'ordre des appuis */
} step_cycle_t;

/* Ce que le boitier croit du portail : sens du mouvement en cours (+1 ouverture, -1 fermeture, 0 immobile) et,
 * immobile, s'il repose en butee ouverte ou fermee (ni l'une ni l'autre : arrete en chemin). */
typedef struct {
    int  dir;
    bool at_open, at_closed;
} step_state_t;

/* Un appui : rend l'action faite a la position `*next` du cycle, et avance `*next`. STEP_ACT_NONE sans cycle. */
step_act_t step_press(const step_cycle_t *c, uint8_t *next);

/* Fin de course : la carte du moteur saute le « stop » qui suivait le mouvement (en butee, l'appui suivant repart
 * dans l'autre sens). Avance `*next` si l'action qu'il designe est un stop. */
void step_travel_end(const step_cycle_t *c, uint8_t *next);

/* Nombre d'appuis qui menent a `want` depuis la position `next` du cycle et l'etat `s` :
 *   0  : deja dans cet etat (ouvert ou en ouverture pour STEP_ACT_OPEN, immobile pour STEP_ACT_STOP) ;
 *   >0 : le nombre d'appuis, au plus la longueur du cycle ;
 *   -1 : impossible avec ce cycle (« stop » dans un cycle ouvre-ferme, « close » en ouverture seule). */
int step_presses(const step_cycle_t *c, uint8_t next, step_state_t s, step_act_t want);

/* Position du cycle d'un portail au repos dont on ne connait pas le cycle (fiche anterieure au suivi) : ferme,
 * le prochain appui ouvre ; sinon il ferme (ou ouvre, si le cycle ne ferme jamais). */
uint8_t step_next_at_rest(const step_cycle_t *c, bool closed);
