#pragma once
/* Appairage io-homecontrol. La radio doit deja etre en config io (l'appelant orchestre via
 * radio.c). Deux modes, voir io_pair.cpp. */
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
 * l'emission. Cree une identite d'emetteur neuve pour ce volet, la memorise, et la rend dans
 * out_src (3 o). Rend 0 si emis, -2 sur erreur. A valider au banc sur un vrai moteur io. */
int io_hc_pair_1w(const io_profile_t *profile, uint8_t out_src[3], uint32_t timeout_ms);

/* Appairage 2W : la carte, en controleur, decouvre un moteur en appairage puis lui transfere la
 * cle du reseau (protocole mode2w de iown-home).
 * EXPERIMENTAL : la sequence vive (saut de canal, aller-retour defi-reponse, acquittement du
 * moteur) reste a valider au banc ; l'emission est prouvee, pas le dialogue complet. Rend 0 et
 * remplit out_dst (node du moteur, 3 o) si le transfert est emis, -1 si rien trouve, -2 sur erreur. */
int io_hc_pair(uint8_t out_dst[3], uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
