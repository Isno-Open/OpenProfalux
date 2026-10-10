/*
 * remote_battery.c — alerte pile faible des telecommandes (voir remote_battery.h). Pur.
 */
#include "remote_battery.h"

bool batt_on_press(batt_state_t *s, bool vlow) {
    bool was_known = s->known, was_low = s->low;
    s->known = true;
    if (vlow) {
        if (s->hits < 255) s->hits++;
        if (s->hits >= BATT_LOW_PRESSES) s->low = true;
    } else {
        s->hits = 0;
        s->low = false;
    }
    return !was_known || s->low != was_low;
}
