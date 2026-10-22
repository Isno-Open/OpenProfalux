/* io_send : voir io_send.h. Emission 1W = protocole iown-home -> framer UART -> io_radio. */
#include "io_send.h"
#include "io_radio.h"
#include "io_framer.h"
#include "io_store.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include <cstring>

using namespace iohome;
using namespace iohome::frame;

/* Construit une trame 1W signee (cmd + charge utile + code roulant + HMAC) depuis la source src,
 * diffuse vers le broadcast Somfy, et l'emet. Le code roulant vient du compteur par-source de
 * io_store (croissant, anti-rejeu). Rend true si emise. */
static bool send_signed_1w(const uint8_t src[3], const uint8_t key[16],
                           uint8_t cmd, const uint8_t *payload, size_t n) {
    IoFrame f;
    init_frame(&f, true);                 /* 1W */
    set_source(&f, src);
    set_destination(&f, SOMFY_BROADCAST_ADDR);   /* le moteur filtre par la source */
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

/* Emet un appui bouton de volet Somfy io en 1W : deux trames calquees sur une capture Situo
 * reelle (danielpetrovic/somfy-io-bridge) qu'une vraie telecommande envoie a chaque ordre :
 *  - la trame primaire 0x00 (p0x00_14) : origin 0x01, fabricant Somfy 0x43, parametre principal
 *    (sens : BTN_UP/BTN_DOWN/BTN_STOP), fp1/fp2 a 0 ;
 *  - une trame compagnon 0x20 (p0x20_16) : fp1 = 0x00 (monter/descendre) ou 0x02 (stop).
 * Diffuse vers le broadcast Somfy ; le moteur filtre par la source. A valider au banc. */
extern "C" int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t key[16], uint8_t button) {
    if (!src || !key) return -1;

    /* Trame primaire 0x00 : origin, fabricant, parametre principal (2 o), fp1, fp2. */
    uint8_t primary[6] = { BUTTON_ORIGIN, MANUFACTURER_SOMFY, button, 0x00, 0x00, 0x00 };
    if (!send_signed_1w(src, key, CMD_EXECUTE, primary, sizeof primary)) return -1;

    /* Trame compagnon 0x20 : origin 0x02, acei 0xFF, main 0x01 0x43, fp1 (mouvement/stop),
     * fp2 0x0C, data 0x00 0x00. Valeurs reproduites de la capture Situo. */
    uint8_t fp1 = (button == BTN_STOP) ? 0x02 : 0x00;
    uint8_t companion[8] = { 0x02, 0xFF, 0x01, MANUFACTURER_SOMFY, fp1, 0x0C, 0x00, 0x00 };
    send_signed_1w(src, key, CMD_PRIVATE_WRITE, companion, sizeof companion);   /* au mieux */

    return 0;
}

extern "C" int io_button_for_action(const char *action) {
    if (!action) return -1;
    if (!strcmp(action, "up"))   return BTN_UP;     /* ouvrir = monter */
    if (!strcmp(action, "down")) return BTN_DOWN;   /* fermer = descendre */
    if (!strcmp(action, "stop")) return BTN_STOP;
    return -1;
}
