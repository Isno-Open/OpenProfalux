#pragma once
/* Identites io-homecontrol de CETTE carte, UNE PAR VOLET io (comme les canaux d'une
 * telecommande multi). Chaque identite = un node source (3 o) + une cle AES (16 o) + un
 * compteur roulant propre. En 1W, le volet diffuse vers le broadcast et c'est la source
 * qui l'identifie aupres du moteur ; la source du volet est rangee dans virt_serial (24 bits).
 * Persiste en NVS (namespace "iohc"), une entree cle/compteur par source. */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rend true et remplit key si une cle est enregistree pour cette source. */
bool io_store_get_key(const uint8_t src[3], uint8_t key[16]);
/* Enregistre (ou remplace) la cle de cette source et initialise son compteur roulant. */
void io_store_set(const uint8_t src[3], const uint8_t key[16]);
/* Rend le compteur roulant courant de cette source puis l'incremente et le persiste. */
uint16_t io_store_next_rolling(const uint8_t src[3]);

#ifdef __cplusplus
}
#endif
