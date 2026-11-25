#pragma once
/* Emission io-homecontrol 1W : construit une trame (protocole iown-home, iohome_frame),
 * l'encapsule (io_framer) et l'emet par le CC1101 (io_radio). API C pour shutters.c.
 * La radio doit deja etre en config io (l'appelant orchestre via radio.c). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emet un appui bouton de volet Somfy io en 1W : source du volet (3 o), cle AES de cette
 * source (16 o), et code bouton (sens : voir iohome_constants.h : BTN_UP/BTN_DOWN/BTN_STOP).
 * Une rafale de quatre trames au meme compteur (bit LPM sur la 1re seulement), compteur reserve dans io_store.
 * Cible et charge utile viennent du profil capte. Aucun nouvel essai automatique.
 * Rend 0 si au moins une trame est emise, -1 sinon. */
int io_hc_send_shutter_1w(const uint8_t src[3], const uint8_t key[16], uint8_t button);

/* Les actions d'un volet io, et leur code bouton io (BTN_*, iohome_constants.h). Rend -1 pour une action inconnue. */
typedef enum { IO_ACTION_UP, IO_ACTION_DOWN, IO_ACTION_STOP } io_action_t;
int io_button_of(io_action_t action);

/* Traduit une action de volet ("up" | "down" | "stop") en code bouton io (BTN_*, declare
 * une seule fois dans iohome_constants.h). Rend -1 si l'action est inconnue, sinon 0..255. */
int io_button_for_action(const char *action);

#ifdef __cplusplus
}
#endif
