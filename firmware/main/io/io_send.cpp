/* io_send : voir io_send.h. Emission 1W = protocole iown-home -> framer UART -> io_radio. */
#include "io_send.h"
#include "io_radio.h"
#include "io_framer.h"
#include "io_store.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include "esp_rom_sys.h"
#include <cstring>

using namespace iohome;
using namespace iohome::frame;

/* Construit une trame 1W signee (cmd + charge utile + code roulant + HMAC) depuis la source src,
 * vers la destination dst, et l'emet. Le code roulant vient du compteur par-source de io_store
 * (croissant, anti-rejeu). Rend true si emise. */
static bool send_signed_1w(const uint8_t src[3], const uint8_t key[16], const uint8_t dst[3],
                           uint8_t cmd, const uint8_t *payload, size_t n, int seq) {
    IoFrame f;
    init_frame(&f);
    set_source(&f, src);
    set_destination(&f, dst);
    if (!set_command(&f, cmd, payload, n)) return false;
    /* seq >= 0 : compteur imposé (ex. fermeture au nom de l'origine, dont on suit le compteur) ;
     * seq < 0 : compteur par-source de io_store (anti-rejeu croissant, cas commun). */
    set_rolling_code(&f, seq >= 0 ? (uint16_t)seq : io_store_next_rolling(src));
    if (!finalize_frame(&f, key)) return false;   /* ajoute HMAC + CRC */

    uint8_t io_bytes[32];
    size_t nb = serialize_frame(&f, io_bytes, sizeof io_bytes);
    if (nb == 0 || nb > sizeof io_bytes) return false;

    return io_radio_tx_frame(io_bytes, (uint8_t)nb) == 0;
}

/* Emet un appui bouton de volet Somfy io en 1W, calque sur une VRAIE trame de la telecommande du
 * volet (profil appris a l'appairage, voir io_store/io_pair). Commande 0x00. On rejoue la charge
 * utile captee a l'identique, a sa vraie longueur (6 octets [origin, fabricant, bouton, 0, 0, 0]
 * pour une p0x00_14, 8 pour une p0x00_16), et on ne change que l'octet du bouton (position 2). La
 * destination et le reste de la charge utile sont la signature du type d'appareil, rejoues tels
 * quels ; source, code roulant et HMAC sont les notres. Repete comme une vraie telecommande
 * (repeat = 4). Rend 0 si au moins une trame est emise, -1 si pas de profil ou echec radio. */
extern "C" int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t key[16], uint8_t button) {
    if (!src || !key) return -1;

    io_profile_t p;
    if (!io_store_get_profile(src, &p)) return -1;   /* ce volet n'a pas de profil appris */
    if (p.payload_len < 3 || p.payload_len > FRAME_MAX_DATA_SIZE) return -1;  /* profil invalide */

    uint8_t payload[FRAME_MAX_DATA_SIZE];
    memcpy(payload, p.payload, p.payload_len);
    payload[2] = button;             /* seul l'octet du bouton change ; le reste = signature rejouee */

    int ok = -1;
    for (int i = 0; i < 4; i++) {    /* repetitions, comme la vraie telecommande */
        if (send_signed_1w(src, key, p.target, CMD_EXECUTE, payload, p.payload_len, -1)) ok = 0;
        esp_rom_delay_us(40000);     /* ~40 ms entre trames */
    }
    return ok;
}

/* Emet une trame de FERMETURE de pairage (0x2e, CMD_PAIR_CONFIRM) au nom de src, signee key.
 * Charge utile = [0x00] (observe sur l'air). seq : compteur IMPOSE (>=0, pour fermer au nom de
 * l'origine en AVANCANT son compteur capte) ou -1 (compteur par-source de io_store). Diffuse vers
 * le broadcast Somfy. Rend 0 si emise, -1 sinon. */
extern "C" int io_hc_send_confirm_1w(const uint8_t src[3], const uint8_t key[16], int seq) {
    if (!src || !key) return -1;
    static const uint8_t payload[1] = { 0x00 };
    return send_signed_1w(src, key, SOMFY_BROADCAST_ADDR, CMD_PAIR_CONFIRM, payload, 1, seq) ? 0 : -1;
}

extern "C" int io_button_for_action(const char *action) {
    if (!action) return -1;
    if (!strcmp(action, "up"))   return BTN_UP;     /* ouvrir = monter */
    if (!strcmp(action, "down")) return BTN_DOWN;   /* fermer = descendre */
    if (!strcmp(action, "stop")) return BTN_STOP;
    return -1;
}
