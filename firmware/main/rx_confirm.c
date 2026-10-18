/* rx_confirm : voir rx_confirm.h. */
#include "rx_confirm.h"
#include <string.h>

void rx_confirm_reset(rx_confirm_t *c) {
    c->last[0] = 0; c->last_us = 0; c->full[0] = 0;
}

bool rx_confirm_push(rx_confirm_t *c, const char *bits, int n, int64_t now_us) {
    if (!bits || n < RXC_CMP_BITS) return false;
    if (n > RXC_FULL_BITS) n = RXC_FULL_BITS;
    bool same = c->last[0] && now_us - c->last_us <= RXC_WINDOW_US
             && memcmp(c->last, bits, RXC_CMP_BITS) == 0;
    /* Trame de 66 bits de la serie : on la garde d'une trame a l'autre tant que la serie dure
     * (la premiere trame peut etre la complete, et la seconde coupee au 64e bit). */
    bool prev_full = same && strlen(c->last) == RXC_FULL_BITS;
    if (!same) c->full[0] = 0;
    else if (prev_full && !c->full[0]) memcpy(c->full, c->last, RXC_FULL_BITS + 1);
    memcpy(c->last, bits, (size_t)n); c->last[n] = 0;
    c->last_us = now_us;
    if (same && n == RXC_FULL_BITS) memcpy(c->full, c->last, RXC_FULL_BITS + 1);
    return same;
}
