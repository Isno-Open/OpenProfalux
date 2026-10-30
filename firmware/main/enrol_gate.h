#pragma once
/* enrol_gate — quand l'enrolement peut emettre la trame d'apprentissage (issue #53).
 *
 * Une capture ratee (aucune trame recue) laissait l'identite de la capture PRECEDENTE
 * selectionnee, et l'apprentissage partait avec elle : une meme identite virtuelle finissait
 * apprise par plusieurs moteurs, jamais celui qu'on visait. Regle : l'apprentissage n'est
 * permis qu'apres une capture REUSSIE dans la tentative en cours. Lancer une capture, la
 * rater, oublier l'identite, l'enregistrer en volet ou redemarrer ferme la porte.
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/enrol-gate. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EG_IDLE = 0,     /* aucune capture dans cette tentative (demarrage, oubli, volet enregistre) */
    EG_CAPTURING,    /* capture en cours */
    EG_CAPTURED,     /* capture reussie : l'identite chargee est celle de cette telecommande */
    EG_FAILED,       /* capture ratee ou refusee : aucune identite utilisable */
} enrol_gate_state_t;

typedef struct {
    enrol_gate_state_t state;
    uint32_t remote;     /* serial de la telecommande captee (EG_CAPTURED) */
} enrol_gate_t;

void eg_init(enrol_gate_t *g);                               /* -> EG_IDLE */
void eg_capture_start(enrol_gate_t *g);                      /* -> EG_CAPTURING */
/* Resultat de la capture : got_frame = une trame a ete recue ; capture_rc = retour de
 * pfx_enrol_capture (0 = identite chargee). -> EG_CAPTURED seulement si les deux sont bons. */
void eg_capture_result(enrol_gate_t *g, bool got_frame, int capture_rc, uint32_t remote);
void eg_reset(enrol_gate_t *g);                              /* oubli, volet enregistre -> EG_IDLE */

bool eg_learn_allowed(const enrol_gate_t *g);
/* Message en clair si l'apprentissage est refuse (NULL s'il est permis). */
const char *eg_refusal(const enrol_gate_t *g);
