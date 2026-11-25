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
/* Profil d'emission d'un volet io, appris d'une trame captee de sa VRAIE telecommande. La charge
 * utile d'une commande io (origin, fabricant, bouton, puis une signature propre au type d'appareil)
 * et sa cible (broadcast encodant le type) dependent de l'appareil. On capture la charge utile
 * REELLE telle qu'elle passe sur l'air, longueur comprise (6 octets pour une "p0x00_14", 8 pour une
 * "p0x00_16"), et on la rejoue a l'identique en ne changeant que l'octet du bouton (position 2), la
 * source, le code roulant et le HMAC (signes avec NOTRE cle). Lire a offsets fixes cassait sur une
 * trame de 6 octets, ou frame[15]/frame[16] sont deja le code roulant, pas la charge utile. */
#define IO_PROFILE_PAYLOAD_MAX 21   /* = iohome::FRAME_MAX_DATA_SIZE, repris ici pour rester en C */
typedef struct {
    uint8_t target[3];                        /* destination de la vraie trame (broadcast type-dependant) */
    uint8_t payload[IO_PROFILE_PAYLOAD_MAX];  /* charge utile reelle captee, rejouee telle quelle */
    uint8_t payload_len;                      /* longueur reelle de la charge utile */
} io_profile_t;

/* Ecrit cle, profil et prochain compteur. -1 conserve le compteur (defaut 1).
 * Refuse un compteur explicite inferieur a celui deja stocke. Sous mutex radio. */
bool io_store_set(const uint8_t src[3], const uint8_t key[16], const io_profile_t *p, int rolling);
/* Reserve et persiste le prochain compteur avant TX. Refuse erreurs et debordement. */
bool io_store_next_rolling(const uint8_t src[3], uint16_t *out);
/* Rend true et remplit p si un profil est enregistre pour cette source. */
bool io_store_get_profile(const uint8_t src[3], io_profile_t *p);

/* Vide le namespace io. Les volets restent ; adopter de nouveau avant de les commander. */
int io_store_erase_all(void);

#ifdef __cplusplus
}
#endif
