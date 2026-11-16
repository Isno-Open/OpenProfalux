/* Garde-fou reseau : voir net_watch.h. */
#include "net_watch.h"

static void nw_clear(net_watch_t *w) { w->fails = 0; w->wifi_restarted = false; }

void nw_init(net_watch_t *w) { w->armed = false; nw_clear(w); }

net_watch_action_t nw_probe(net_watch_t *w, bool associated, bool gateway_ok) {
    if (gateway_ok) w->armed = true;
    if (!associated || gateway_ok || !w->armed) { nw_clear(w); return NW_ACT_NONE; }
    w->fails++;
    if (w->fails >= NW_FAILS_REBOOT) return NW_ACT_REBOOT;
    if (w->fails >= NW_FAILS_RESTART_WIFI && !w->wifi_restarted) { w->wifi_restarted = true; return NW_ACT_RESTART_WIFI; }
    return NW_ACT_NONE;
}
