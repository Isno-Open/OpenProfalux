#pragma once
/* Emission io-homecontrol 1W : construit une trame (protocole iown-home, iohome_frame),
 * l'encapsule (io_framer) et l'emet par le CC1101 (io_radio). API C pour shutters.c.
 * La radio doit deja etre en config io (l'appelant orchestre via radio.c). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emet une commande 1W : source et destination (3 octets chacun), cle AES du reseau (16 o,
 * obtenue a l'appairage), code roulant, et code de commande io. Rend 0 si emis, -1 sinon. */
int io_hc_send_1w(const uint8_t src[3], const uint8_t dst[3],
                  const uint8_t key[16], uint16_t rolling, uint8_t cmd);

#ifdef __cplusplus
}
#endif
