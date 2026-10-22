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
                           uint8_t cmd, const uint8_t *payload, size_t n) {
    IoFrame f;
    init_frame(&f, true);                 /* 1W */
    set_source(&f, src);
    set_destination(&f, dst);
    if (!set_command(&f, cmd, payload, n)) return false;
    set_rolling_code(&f, io_store_next_rolling(src));
    if (!finalize_frame(&f, key, nullptr)) return false;   /* ajoute HMAC + CRC */

    uint8_t io_bytes[32];
    size_t nb = serialize_frame(&f, io_bytes, sizeof io_bytes);
    if (nb == 0 || nb > sizeof io_bytes) return false;

    uint8_t fifo[64];
    size_t m = io_frame_to_fifo(io_bytes, (uint8_t)nb, fifo, sizeof fifo);
    if (m == 0 || m > 60) return false;

    return io_radio_tx_packet(fifo, m) == 0;
}

/* Emet un appui bouton de volet Somfy io en 1W, calque sur une VRAIE trame de la telecommande du
 * volet (profil appris a l'appairage, voir io_store/io_pair). Commande 0x00, charge utile longue
 * "p0x00_16" (8 octets) : origin 0x01, acei, main[2] (main[0] = bouton BTN_UP/DOWN/STOP), fp1, fp2,
 * data[2]. acei, fp1, fp2, data et la destination sont la signature du type d'appareil, rejouee
 * telle quelle ; seul le bouton change. Repete comme une vraie telecommande (repeat = 4). Rend 0
 * si au moins une trame est emise, -1 si pas de profil pour ce volet ou echec radio. */
extern "C" int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t key[16], uint8_t button) {
    if (!src || !key) return -1;

    io_profile_t p;
    if (!io_store_get_profile(src, &p)) return -1;   /* ce volet n'a pas de profil appris */

    uint8_t payload[8] = { BUTTON_ORIGIN, p.acei, button, 0x00, p.fp1, p.fp2, p.data[0], p.data[1] };
    int ok = -1;
    for (int i = 0; i < 4; i++) {    /* repetitions, comme la vraie telecommande */
        if (send_signed_1w(src, key, p.target, CMD_EXECUTE, payload, sizeof payload)) ok = 0;
        esp_rom_delay_us(40000);     /* ~40 ms entre trames */
    }
    return ok;
}

extern "C" int io_button_for_action(const char *action) {
    if (!action) return -1;
    if (!strcmp(action, "up"))   return BTN_UP;     /* ouvrir = monter */
    if (!strcmp(action, "down")) return BTN_DOWN;   /* fermer = descendre */
    if (!strcmp(action, "stop")) return BTN_STOP;
    return -1;
}
