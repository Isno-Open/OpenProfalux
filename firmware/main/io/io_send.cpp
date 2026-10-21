/* io_send : voir io_send.h. Emission 1W = protocole iown-home -> framer UART -> io_radio. */
#include "io_send.h"
#include "io_radio.h"
#include "io_framer.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include <cstring>

using namespace iohome::frame;

extern "C" int io_hc_send_1w(const uint8_t src[3], const uint8_t dst[3],
                             const uint8_t key[16], uint16_t rolling, uint8_t cmd) {
    if (!src || !dst || !key) return -1;
    IoFrame f;
    init_frame(&f, true);                 /* 1W */
    set_source(&f, src);
    set_destination(&f, dst);
    if (!set_command(&f, cmd, nullptr, 0)) return -1;
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

extern "C" int io_cmd_for_action(const char *action) {
    if (!action) return -1;
    if (!strcmp(action, "up"))   return iohome::CMD_OPEN;    /* ouvrir = monter */
    if (!strcmp(action, "down")) return iohome::CMD_CLOSE;   /* fermer = descendre */
    if (!strcmp(action, "stop")) return iohome::CMD_STOP;
    return -1;
}
