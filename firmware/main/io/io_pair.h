#pragma once
/* Appairage 2W io-homecontrol. La carte, en controleur, decouvre un moteur mis en appairage
 * puis lui transfere la cle du reseau (generee ou reutilisee, memorisee dans io_store). Protocole
 * repris de iown-home (classes mode2w). La radio doit deja etre en config io (l'appelant orchestre
 * via radio.c).
 *
 * EXPERIMENTAL : la sequence vive (saut de canal, aller-retour defi-reponse, acquittement du
 * moteur) reste a valider au banc avec un vrai appareil io ; l'emission est prouvee, pas le
 * dialogue complet. Rend 0 et remplit out_dst (node du moteur, 3 o) si le transfert est emis, -1
 * si aucun appareil n'est trouve avant le delai, -2 sur erreur. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int io_hc_pair(uint8_t out_dst[3], uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
