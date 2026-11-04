#pragma once
#include <stdint.h>
#include "io_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Profil complet d'une commande 0x00 captee, CRC et longueur verifies. */
int io_profile_from_capture(const uint8_t *frame, int n, io_profile_t *out);
/* Profil de commande d'un volet io SYNTHETISE depuis la marque (man_id -> fabricant), sans capture :
 * broadcast volet 00 00 3F + payload [origin, fabricant, bouton, 0, 0, 0]. -1 si marque io inconnue. */
int io_profile_synth(uint8_t man_id, io_profile_t *out);
/* Compatible avec feat/io-pair-known-identity : MAC STA + index. */
bool io_derive_identity(uint32_t index, uint8_t out_src[3], uint8_t out_key[16]);
/* Une rafale 0x30, identite deja persistee ; l'appelant tient la radio en mode io. */
int io_hc_pair_1w(const io_profile_t *profile, const uint8_t src[3],
                  const uint8_t key[16], uint8_t man_id);
#ifdef __cplusplus
}
#endif
