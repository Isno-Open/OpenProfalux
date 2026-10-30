/* enrol_gate — voir enrol_gate.h. */
#include "enrol_gate.h"

void eg_init(enrol_gate_t *g)          { g->state = EG_IDLE; g->remote = 0; }
void eg_reset(enrol_gate_t *g)         { eg_init(g); }
void eg_capture_start(enrol_gate_t *g) { g->state = EG_CAPTURING; g->remote = 0; }

void eg_capture_result(enrol_gate_t *g, bool got_frame, int capture_rc, uint32_t remote) {
    if (g->state != EG_CAPTURING) return;   /* resultat d'une capture qui n'est plus la courante */
    if (got_frame && capture_rc == 0) { g->state = EG_CAPTURED; g->remote = remote; }
    else                              { g->state = EG_FAILED;   g->remote = 0; }
}

bool eg_learn_allowed(const enrol_gate_t *g) { return g->state == EG_CAPTURED; }

const char *eg_refusal(const enrol_gate_t *g) {
    switch (g->state) {
    case EG_CAPTURED:  return 0;
    case EG_CAPTURING: return "Capture en cours : appuie sur la télécommande de ce volet.";
    case EG_FAILED:    return "La capture a échoué : rapproche la télécommande du boîtier et recommence.";
    case EG_IDLE:
    default:           return "Capte d'abord la télécommande de ce volet.";
    }
}
