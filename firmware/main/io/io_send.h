#pragma once
/* Emission io-homecontrol 1W : construit une trame (protocole iown-home, iohome_frame),
 * l'encapsule (io_framer) et l'emet par le CC1101 (io_radio). API C pour shutters.c.
 * La radio doit deja etre en config io (l'appelant orchestre via radio.c). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emet un appui bouton de volet Somfy io en 1W : source et destination (3 octets chacun),
 * cle AES du reseau (16 o, obtenue a l'appairage), code roulant, et code bouton (sens :
 * voir iohome_constants.h : BTN_UP/BTN_DOWN/BTN_STOP). Construit la trame commande 0x00 +
 * charge utile Somfy courte. Rend 0 si emis, -1 sinon. */
int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t dst[3],
                          const uint8_t key[16], uint16_t rolling, uint8_t button);

/* Traduit une action de volet ("up" | "down" | "stop") en code bouton io (BTN_*, declare
 * une seule fois dans iohome_constants.h). Rend -1 si l'action est inconnue, sinon 0..255. */
int io_button_for_action(const char *action);

#ifdef __cplusplus
}
#endif
