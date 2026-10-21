#pragma once
/* Identite io-homecontrol de CETTE carte, vue comme un controleur du reseau io.
 * Partagee par tous les volets io : un node source (3 o), la cle AES du reseau (16 o,
 * obtenue a l'appairage 2W), et un compteur roulant commun a la source. Persistee en NVS
 * (namespace dedie, comme l'enrolement Profalux). Le node du MOTEUR (destination) vit, lui,
 * dans virt_serial du volet (24 bits). */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rend true et remplit src/key si une identite io est enregistree. */
bool io_store_get(uint8_t src[3], uint8_t key[16]);
/* true si la carte a deja rejoint un reseau io (une cle est posee). */
bool io_store_has(void);
/* Enregistre l'identite io (source + cle reseau). Initialise le compteur roulant si absent. */
void io_store_set(const uint8_t src[3], const uint8_t key[16]);
/* Rend le compteur roulant courant puis l'incremente et le persiste (anti-rejeu). */
uint16_t io_store_next_rolling(void);

#ifdef __cplusplus
}
#endif
