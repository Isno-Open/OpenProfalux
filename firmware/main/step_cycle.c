/*
 * step_cycle.c : voir step_cycle.h.
 */
#include "step_cycle.h"

step_act_t step_press(const step_cycle_t *c, uint8_t *next) {
    if (!c || !c->len || !next) return STEP_ACT_NONE;
    if (*next >= c->len) *next = 0;
    step_act_t a = (step_act_t)c->act[*next];
    *next = (uint8_t)((*next + 1) % c->len);
    return a;
}

void step_travel_end(const step_cycle_t *c, uint8_t *next) {
    if (!c || !c->len || !next) return;
    if (*next >= c->len) *next = 0;
    if (c->act[*next] == STEP_ACT_STOP) *next = (uint8_t)((*next + 1) % c->len);
}

int step_presses(const step_cycle_t *c, uint8_t next, step_state_t s, step_act_t want) {
    if (!c || !c->len) return -1;
    switch (want) {
    case STEP_ACT_OPEN:  if (s.dir > 0 || (s.dir == 0 && s.at_open))   return 0; break;
    case STEP_ACT_CLOSE: if (s.dir < 0 || (s.dir == 0 && s.at_closed)) return 0; break;
    case STEP_ACT_STOP:  if (s.dir == 0) return 0; break;
    default: return -1;
    }
    if (next >= c->len) next = 0;
    for (int k = 1; k <= c->len; k++)
        if (c->act[(next + k - 1) % c->len] == want) return k;
    return -1;
}

uint8_t step_next_at_rest(const step_cycle_t *c, bool closed) {
    if (!c || !c->len) return 0;
    step_act_t want = closed ? STEP_ACT_OPEN : STEP_ACT_CLOSE;
    for (uint8_t i = 0; i < c->len; i++) if (c->act[i] == want) return i;
    for (uint8_t i = 0; i < c->len; i++) if (c->act[i] == STEP_ACT_OPEN) return i;
    return 0;
}
