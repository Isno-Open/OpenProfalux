/* io_send : voir io_send.h. Emission 1W = protocole iown-home -> framer UART -> io_radio. */
#include "io_send.h"
#include "io_radio.h"
#include "io_framer.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include <cstring>

using namespace iohome;
using namespace iohome::frame;

/* Charge utile d'un appui bouton de volet Somfy io (6 octets), forme courte "p0x00_14"
 * mesuree et re-emise par samr037/iohc-flipper (docs/phase2_updown_capture.csv, trames de
 * 25 octets d'une telecommande Somfy ; src/state/tx_runner.c build_button_frame) :
 *   [0] origin (0x01), [1] fabricant Somfy (0x43), [2] code bouton, [3..5] 0x00.
 * Le code bouton porte le sens : BTN_UP / BTN_DOWN / BTN_STOP. Diffuse vers le broadcast
 * Somfy (SOMFY_BROADCAST_ADDR) ; le moteur filtre par la source. A valider au banc. */
static void build_somfy_shutter_payload(uint8_t button, uint8_t out[6]) {
    out[0] = BUTTON_ORIGIN;        /* 0x01 */
    out[1] = MANUFACTURER_SOMFY;   /* 0x43 */
    out[2] = button;               /* BTN_UP / BTN_DOWN / BTN_STOP */
    out[3] = 0x00;
    out[4] = 0x00;
    out[5] = 0x00;
}

extern "C" int io_hc_send_shutter_1w(const uint8_t src[3],
                                     const uint8_t key[16], uint16_t rolling, uint8_t button) {
    if (!src || !key) return -1;
    uint8_t payload[6];
    build_somfy_shutter_payload(button, payload);

    IoFrame f;
    init_frame(&f, true);                 /* 1W */
    set_source(&f, src);
    set_destination(&f, SOMFY_BROADCAST_ADDR);   /* diffusion Somfy ; le moteur filtre par la source */
    if (!set_command(&f, CMD_EXECUTE, payload, sizeof payload)) return -1;   /* cmd 0x00 + charge utile */
    set_rolling_code(&f, rolling);
    if (!finalize_frame(&f, key, nullptr)) return -1;   /* ajoute HMAC + CRC */

    uint8_t io_bytes[32];
    size_t n = serialize_frame(&f, io_bytes, sizeof io_bytes);
    if (n == 0 || n > sizeof io_bytes) return -1;

    uint8_t fifo[64];
    size_t m = io_frame_to_fifo(io_bytes, (uint8_t)n, fifo, sizeof fifo);
    if (m == 0 || m > 60) return -1;

    return io_radio_tx_packet(fifo, m);
}

extern "C" int io_button_for_action(const char *action) {
    if (!action) return -1;
    if (!strcmp(action, "up"))   return BTN_UP;     /* ouvrir = monter */
    if (!strcmp(action, "down")) return BTN_DOWN;   /* fermer = descendre */
    if (!strcmp(action, "stop")) return BTN_STOP;
    return -1;
}
