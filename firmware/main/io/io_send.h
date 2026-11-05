#pragma once
/* Emission io-homecontrol 1W : construit une trame (protocole iown-home, iohome_frame),
 * l'encapsule (io_framer) et l'emet par le CC1101 (io_radio). API C pour shutters.c.
 * La radio doit deja etre en config io (l'appelant orchestre via radio.c). */
#include <stdint.h>
#include "io_store.h"   /* io_action_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Emet UNE action de volet io en 1W (monter/stop/descendre) : source du volet (3 o), cle AES de cette
 * source (16 o), et l'action. La trame rejouee est la trame 0x00 REELLE de cette action, captee de la
 * vraie telecommande (profil io_store), rejouee verbatim ; dest, charge utile et basse conso en viennent.
 * Une rafale de quatre trames au meme compteur, reserve dans io_store. Aucun nouvel essai automatique.
 * Rend 0 si au moins une trame est emise, -1 sinon. */
int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t key[16], io_action_t action);

#ifdef __cplusplus
}
#endif
