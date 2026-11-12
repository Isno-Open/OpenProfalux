/* io_send : voir io_send.h. Emission 1W = protocole iown-home -> framer UART -> io_radio. */
#include "io_send.h"
#include "io_radio.h"
#include "io_framer.h"
#include "io_store.h"
#include "iohome_frame.h"
#include "iohome_crypto.h"
#include "iohome_constants.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

using namespace iohome;
using namespace iohome::frame;

/* Construit une trame 1W signee (cmd + charge utile + code roulant + HMAC) depuis la source src,
 * vers la destination dst, et l'emet. Le code roulant vient du compteur par-source de io_store
 * (croissant, anti-rejeu). Rend true si emise. */
/* Emet io_bytes (trame io complete, CRC compris) IO_TX_REPEATS fois a l'identique, espacees de gap_ms.
 * Rend 0 si au moins une copie est partie. */
extern "C" int io_hc_send_frame_burst(const uint8_t *io_bytes, size_t nb, uint16_t gap_ms) {
    if (!io_bytes || nb == 0 || nb > FRAME_MAX_SIZE) return -1;
    bool ok = false;
    for (uint8_t i = 0; i < IO_TX_REPEATS; i++) {
        if (io_radio_tx_frame(io_bytes, (uint8_t)nb) == 0) ok = true;
        if (i + 1 < IO_TX_REPEATS) vTaskDelay(pdMS_TO_TICKS(gap_ms));
    }
    return ok ? 0 : -1;
}

/* Construit une trame 1W signee a ctrl1 donne (un code roulant pour toute la rafale) et l'emet en
 * IO_TX_REPEATS copies identiques. Sert a la ceremonie Situo (0x39 et 0x20 signes). */
extern "C" int io_hc_send_signed_1w(const uint8_t src[3], const uint8_t key[16], const uint8_t dst[3],
                                    uint8_t cmd, const uint8_t *payload, size_t n, uint8_t ctrl1,
                                    uint16_t gap_ms) {
    if (!src || !key || !dst) return -1;
    IoFrame f;
    init_frame(&f);
    f.ctrl_byte_1 = ctrl1;
    set_source(&f, src);
    set_destination(&f, dst);
    if (!set_command(&f, cmd, payload, n)) return -1;
    uint16_t seq;
    if (!io_store_next_rolling(src, &seq)) return -1;
    set_rolling_code(&f, seq);
    if (!finalize_frame(&f, key)) return -1;
    uint8_t io_bytes[32];
    size_t nb = serialize_frame(&f, io_bytes, sizeof io_bytes);
    if (nb == 0 || nb > sizeof io_bytes) return -1;
    return io_hc_send_frame_burst(io_bytes, nb, gap_ms);
}

static bool send_signed_1w(const uint8_t src[3], const uint8_t key[16], const uint8_t dst[3],
                           uint8_t cmd, const uint8_t *payload, size_t n, uint8_t low_power) {
    IoFrame f;
    init_frame(&f);
    f.ctrl_byte_1 = low_power;   /* CAPTE : la vraie telecommande met (ou non) le bit basse conso selon la destination */
    set_source(&f, src);
    set_destination(&f, dst);
    if (!set_command(&f, cmd, payload, n)) return false;
    uint16_t seq;
    if (!io_store_next_rolling(src, &seq)) return false;
    set_rolling_code(&f, seq);
    if (!finalize_frame(&f, key)) return false;   /* ajoute HMAC + CRC */

    uint8_t io_bytes[32];
    size_t nb = serialize_frame(&f, io_bytes, sizeof io_bytes);
    if (nb == 0 || nb > sizeof io_bytes) return false;

    bool ok = false;
    /* Un appui = une seule rafale, meme compteur ; seuls le bit LPM et le CRC changent apres la 1re. */
    for (uint8_t i = 0; i < IO_TX_REPEATS; i++) {
        if (i == 1) {
            io_bytes[1] &= (uint8_t)~CTRL1_LOW_POWER;
            const uint16_t crc = crypto::compute_crc16(io_bytes, nb - CRC_SIZE);
            io_bytes[nb - 2] = (uint8_t)crc;
            io_bytes[nb - 1] = (uint8_t)(crc >> 8);
        }
        if (io_radio_tx_frame(io_bytes, (uint8_t)nb) == 0) ok = true;
        if (i + 1 < IO_TX_REPEATS) vTaskDelay(pdMS_TO_TICKS(40));
    }
    return ok;
}

/* Emet UNE action de volet io en 1W (monter/stop/descendre), rejouee VERBATIM depuis la trame 0x00
 * REELLE de cette action, captee de la vraie telecommande (profil appris a l'appairage). On ne reecrit
 * RIEN : ni le bouton, ni le reste. La destination (cmd_target), la charge utile (payload[action]) et
 * la basse conso (low_power) viennent toutes du profil capte ; source, code roulant et HMAC sont les
 * notres. Repete comme une vraie telecommande (repeat = 4). Rend 0 si au moins une trame est emise,
 * -1 si l'action n'est pas captee ou en cas d'echec radio. */
extern "C" int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t key[16], io_action_t action) {
    if (!src || !key || action < 0 || action >= IO_ACTION_COUNT) return -1;

    io_profile_t p;
    if (!io_store_get_profile(src, &p)) return -1;   /* ce volet n'a pas de profil appris */
    uint8_t n = p.payload_len[action];
    if (n < 3 || n > FRAME_MAX_DATA_SIZE) return -1;  /* action non captee ou profil invalide */

    return send_signed_1w(src, key, p.cmd_target, CMD_EXECUTE, p.payload[action], n, p.low_power) ? 0 : -1;
}
