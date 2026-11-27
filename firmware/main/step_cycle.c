/* step_cycle.c : voir step_cycle.h. */
#include "step_cycle.h"

int step_press(const catalog_cycle_t *c, uint8_t *next) {
    if (!c || !c->len || !next) return -1;
    if (*next >= c->len) *next = 0;
    const int a = c->act[*next];
    *next = (uint8_t)((*next + 1) % c->len);
    return a;
}

void step_travel_end(const catalog_cycle_t *c, uint8_t *next) {
    if (!c || !c->len || !next) return;
    if (*next >= c->len) *next = 0;
    if (c->act[*next] == CAT_CMD_STOP) *next = (uint8_t)((*next + 1) % c->len);
}

int step_presses(const catalog_cycle_t *c, uint8_t next, step_state_t s, catalog_cmd_t want) {
    if (!c || !c->len) return -1;
    switch (want) {
    case CAT_CMD_UP:   if (s.dir > 0 || (s.dir == 0 && s.at_open))   return 0; break;
    case CAT_CMD_DOWN: if (s.dir < 0 || (s.dir == 0 && s.at_closed)) return 0; break;
    case CAT_CMD_STOP: if (s.dir == 0) return 0; break;
    default: return -1;
    }
    if (next >= c->len) next = 0;
    for (int k = 1; k <= c->len; k++)
        if (c->act[(next + k - 1) % c->len] == want) return k;
    return -1;
}

uint8_t step_next_at_rest(const catalog_cycle_t *c, bool closed) {
    if (!c || !c->len) return 0;
    const int want = closed ? CAT_CMD_UP : CAT_CMD_DOWN;
    for (uint8_t i = 0; i < c->len; i++) if (c->act[i] == want) return i;
    for (uint8_t i = 0; i < c->len; i++) if (c->act[i] == CAT_CMD_UP) return i;
    return 0;
}
