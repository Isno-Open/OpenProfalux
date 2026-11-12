#pragma once
#include <stdint.h>
#include "io_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apprend UNE action (monter/stop/descendre) depuis une trame 0x00 captee de la vraie telecommande.
 * Remplit cmd_target, payload[action] et low_power de *out (mis a zero par l'appelant, appele une fois
 * par action). CRC et longueur verifies. Rend 0 si exploitable. */
int io_profile_from_capture(const uint8_t *frame, int n, io_profile_t *out, io_action_t action);
/* Apprend l'enrolement depuis le 0x30 de la vraie telecommande capte pendant sa fenetre PROG :
 * enroll_target, low_power, man_id et pair_status de *out. Rien n'est suppose. Rend 0 si 0x30 valide. */
int io_enroll_from_capture(const uint8_t *frame, int n, io_profile_t *out);
/* Nom de marque d'apres l'octet fabricant d'une commande 0x00 (payload[1]) : "Somfy" (0x43) /
 * "Velux" (0x61) / NULL si inconnu. */
const char *io_marque_name(uint8_t fabricant);
/* Nom de marque d'apres le man_id capte du 0x30 (0x02 Somfy / 0x01 Velux), pour le panneau avance. */
const char *io_man_name(uint8_t man_id);
/* Octet de commande d'une trame io (offset fixe), ou 0xFF si la trame est trop courte. Pour que les
 * sondes de diagnostic (radio.c, C) nomment une trame captee sans connaitre la disposition io. */
uint8_t io_frame_cmd(const uint8_t *frame, int n);
/* Libelle francais court d'un octet de commande io (0x30 cle, 0x20 private-write, 0x2e confirmation,
 * 0x00 commande...), ou NULL si inconnu. Sert au journal auto-explicatif. Pas de valeur en dur chez
 * l'appelant : les octets vivent dans iohome_constants.h. */
const char *io_cmd_label(uint8_t cmd);
/* Vrai si l'octet est la confirmation de fin de ceremonie (0x2e), pour poser une banniere de journal. */
bool io_cmd_is_pair_confirm(uint8_t cmd);
/* Identite deterministe de la carte : MAC STA + index. */
bool io_derive_identity(uint32_t index, uint8_t out_src[3], uint8_t out_key[16]);
/* Identite deterministe LIBRE : plus petit index dont la source n'est pas deja enrolee (une par volet io). */
bool io_derive_free_identity(uint8_t out_src[3], uint8_t out_key[16]);
/* Mode diagnostic prive : reproduit la fabrique de reference pour une des 11 identites du pool.
 * seed_us=0 prend gettimeofday ; une valeur imposee rend le calcul testable. out_x4 optionnel. */
bool io_derive_compat_pool_identity(uint8_t pool_index, uint64_t seed_us,
                                    uint8_t out_src[3], uint8_t out_key[16], uint8_t out_x4[4]);
/* Pool produit fixe de 11 identites : meme fabrique X4/T que le pool de compatibilite. */
bool io_derive_local_pool_identity(uint8_t pool_index, uint64_t seed_us,
                                   uint8_t out_src[3], uint8_t out_key[16], uint8_t out_x4[4]);
/* Emet NOTRE 0x30 d'enrolement vers la dest CAPTEE du profil ; identite deja persistee ; l'appelant
 * tient la radio en mode io. Dest, basse conso, fabricant et statut viennent tous du profil capte.
 * out_frame (>= 31 o) et out_len, s'ils sont fournis, recoivent la trame reellement emise pour le
 * journal de diagnostic ; passer NULL/NULL si on n'en veut pas. */
int io_hc_pair_1w(const io_profile_t *profile, const uint8_t src[3], const uint8_t key[16],
                  uint8_t *out_frame, int *out_len);
/* Ceremonie d'ajout complete (recette iohc-flipper, Somfy et Velux) : 0x39 signe (notre adresse),
 * 0x30 vers 4 destinations (man_id CAPTE), puis STOP et DESCENTE signes. out_frame recoit le 0x30. */
int io_hc_pair_1w_ceremony(const io_profile_t *profile, const uint8_t src[3], const uint8_t key[16],
                           uint8_t *out_frame, int *out_len);
#ifdef __cplusplus
}
#endif
