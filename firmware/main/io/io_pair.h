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
/* Identite deterministe de la carte : MAC STA + index. */
bool io_derive_identity(uint32_t index, uint8_t out_src[3], uint8_t out_key[16]);
/* Emet NOTRE 0x30 d'enrolement vers la dest CAPTEE du profil ; identite deja persistee ; l'appelant
 * tient la radio en mode io. Dest, basse conso, fabricant et statut viennent tous du profil capte. */
int io_hc_pair_1w(const io_profile_t *profile, const uint8_t src[3], const uint8_t key[16]);
#ifdef __cplusplus
}
#endif
