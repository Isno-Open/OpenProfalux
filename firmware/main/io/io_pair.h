#pragma once
/* Appairage io-homecontrol, mode 1W uniquement. La radio doit deja etre en config io
 * (l'appelant orchestre via radio.c). Voir io_pair.cpp. */
#include <stdint.h>
#include "io_store.h"   /* io_profile_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Apprend le profil d'emission (cible + signature de type fp1/fp2/data) depuis une trame io
 * captee (commande 0x00) de la vraie telecommande du volet. Rend 0 si exploitable, -1 sinon. */
int io_profile_from_capture(const uint8_t *frame, int n, io_profile_t *out);

/* Appairage 1W : la carte se presente comme une nouvelle telecommande et POUSSE sa cle vers un
 * moteur mis en mode PROG (bouton physique). CC1101 seul, ni reception ni saut de frequence. Vise
 * le meme broadcast que la vraie telecommande (profile->target) et memorise le profil pour
 * l'emission.
 *
 * Identite d'emetteur : fixed_src est toujours NULL cote firmware (on cree une identite neuve de
 * type telecommande 0xD au hasard). Le parametre reste pour les essais, mais l'UI ne l'utilise pas :
 * emettre sous une identite donnee n'a pas d'interet pour l'enrolement par parrainage. Dans
 * les deux cas la cle AES est tiree au hasard et poussee avec la source. L'identite retenue est
 * memorisee et rendue dans out_src (3 o). Rend 0 si emis, -2 sur erreur. A valider au banc sur un
 * vrai moteur io. */
int io_hc_pair_1w(const io_profile_t *profile, const uint8_t *fixed_src,
                  uint8_t out_src[3], uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
