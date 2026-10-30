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
 * Identite et cle d'emetteur, deux modes :
 *   - fixed_src = NULL, fixed_key = NULL : on cree une identite de type telecommande 0xD au hasard
 *     avec une cle neuve a nous (ajout par parrainage d'une telecommande vierge) ;
 *   - fixed_src + fixed_key fournis : on emet sous une identite DEJA CONNUE du moteur et sa cle
 *     systeme (p.ex. une identite la box de copie que le moteur fait deja confiance), ce qui reproduit ce
 *     que fait une box la box de copie a l'enrolement. La cle est fournie a l'EXECUTION par l'appelant,
 *     jamais ecrite dans les sources (depot public).
 * man_id : octet fabricant du 0x30 ; < 0 => defaut Somfy (0x02), sinon la valeur captee sur
 * l'enrolement la box de copie reel (0x01 observe pour certaines identites). L'identite retenue est
 * memorisee et rendue dans out_src (3 o). Rend 0 si emis, -2 sur erreur. */
int io_hc_pair_1w(const io_profile_t *profile, const uint8_t *fixed_src,
                  const uint8_t *fixed_key, int man_id,
                  uint8_t out_src[3], uint32_t timeout_ms);

/* Identite DETERMINISTE derivee du chip ID + index (survit au formatage ; recalculable pour un
 * retrait ulterieur). Remplit out_src (type telecommande 0xD) + out_key (16 o). */
void io_derive_identity(uint32_t index, uint8_t out_src[3], uint8_t out_key[16]);

/* Decode une trame 0x30 captee -> identite (3 o), cle systeme en clair (16 o), compteur (out_seq).
 * Rend true si exploitable. Sert au profil 2 (fermeture en tant qu'origine). */
bool io_decode_0x30(const uint8_t *frame, int n, uint8_t out_src[3], uint8_t out_key[16], int *out_seq);

/* Enrolement par parrainage, 3 modes. mode : 1 = SIMPLE (notre 0x30 seul, temoin) ; 2 = PARRAINAGE
 * complet SIGNE AVEC LA CLE DE L'ORIGINE (0x39 -> notre 0x30 -> rafale 0x20 -> 0x2e -> fin 0x20) puis
 * commandes en tant que NOUS ; 3 = CLONE (meme ceremonie signee origine, le 0x30 re-ajoute l'origine,
 * commandes en tant qu'elle). Modes 2 et 3 : origin_src/origin_key decodes d'un 0x30 capte, compteur
 * amorce a origin_seq (le moteur rejette un compteur <= dernier vu). gap_ms/confirm_repeats inutilises
 * (le rythme vient des constantes io). Rend l'identite retenue dans out_src. 0 si OK, -3 si origine absente. */
int io_hc_enroll(const io_profile_t *profile,
                 const uint8_t our_src[3], const uint8_t our_key[16], int man_id,
                 int mode, const uint8_t *origin_src, const uint8_t *origin_key,
                 int origin_seq, int gap_ms, int confirm_repeats, uint8_t out_src[3]);
#ifdef __cplusplus
}
#endif
