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

/* Profil d'emission d'un volet io, appris d'une trame captee de sa VRAIE telecommande. Un moteur
 * io attend une trame "p0x00_16" dont la cible (broadcast encodant le type d'appareil) et la
 * signature fp1/fp2/data[2] dependent du type. On rejoue ces champs tels quels et on ne change
 * que le bouton, la source, le code roulant et le HMAC (signes avec NOTRE cle). */
typedef struct {
    uint8_t target[3];   /* destination de la vraie trame (broadcast type-dependant) */
    uint8_t acei;        /* 2e octet de charge utile (0x43 observe chez Somfy) */
    uint8_t fp1;         /* signature de type (ex. type 6 : 0x80) */
    uint8_t fp2;         /* signature de type (ex. type 6 : 0xD3) */
    uint8_t data[2];     /* 2 octets de fin de charge utile (variante longue p0x00_16) */
} io_profile_t;

/* Enregistre (ou remplace) le profil d'emission de cette source. */
void io_store_set_profile(const uint8_t src[3], const io_profile_t *p);
/* Rend true et remplit p si un profil est enregistre pour cette source. */
bool io_store_get_profile(const uint8_t src[3], io_profile_t *p);

#ifdef __cplusplus
}
#endif
