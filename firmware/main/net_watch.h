#pragma once
/* net_watch : le reseau repond-il VRAIMENT ? Decision du garde-fou reseau (wifi_bridge.c).
 *
 * Pourquoi : le 2026-11-16, l'ATOM est reste ~11 min injoignable (ni ping, ni interface, ni cloud)
 * alors que le Wi-Fi se disait associe. Le garde-fou Wi-Fi ne regardait que l'association : il n'a
 * rien fait, et il a fallu debrancher. Ici, on sonde la PASSERELLE (ping) a intervalle regulier :
 * une passerelle muette alors que le Wi-Fi est associe, c'est la pile reseau du boitier qui est
 * bloquee. On relance alors le Wi-Fi, puis on redemarre en dernier recours.
 *
 * La passerelle et pas le broker : un Home Assistant arrete ou un cloud coupe ne sont pas une
 * panne du boitier, et ne doivent jamais le faire redemarrer. Et le garde-fou ne s'arme qu'apres
 * une PREMIERE reponse de la passerelle : un reseau dont la passerelle ignore le ping ferait sinon
 * redemarrer le boitier toutes les quelques minutes.
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/net-watch. */
#include <stdbool.h>

/* Sondes consecutives sans reponse de la passerelle (une sonde = quelques pings) avant d'agir.
 * Avec une sonde toutes les NW_PROBE_PERIOD_S : relance du Wi-Fi a 2 min, redemarrage a 4 min. */
#define NW_PROBE_PERIOD_S     30
#define NW_FAILS_RESTART_WIFI 4
#define NW_FAILS_REBOOT       8

typedef enum {
    NW_ACT_NONE = 0,       /* rien a faire */
    NW_ACT_RESTART_WIFI,   /* relancer la pile Wi-Fi (une fois par panne) */
    NW_ACT_REBOOT,         /* redemarrer le boitier (dernier recours) */
} net_watch_action_t;

typedef struct {
    bool armed;            /* la passerelle a deja repondu une fois : son silence devient une panne */
    int fails;             /* sondes ratees d'affilee, Wi-Fi associe */
    bool wifi_restarted;   /* la relance du Wi-Fi a deja ete tentee pour cette panne */
} net_watch_t;

void nw_init(net_watch_t *w);
/* Resultat d'une sonde. associated : le Wi-Fi se dit associe (sinon c'est l'affaire du garde-fou
 * Wi-Fi, qui relance et redemarre de son cote : on remet a zero). gateway_ok : la passerelle a
 * repondu a au moins un ping. */
net_watch_action_t nw_probe(net_watch_t *w, bool associated, bool gateway_ok);
