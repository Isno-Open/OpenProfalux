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
/* Une action de volet : trois captures 0x00, une par touche de la vraie telecommande. */
typedef enum { IO_ACTION_UP = 0, IO_ACTION_STOP = 1, IO_ACTION_DOWN = 2, IO_ACTION_COUNT = 3 } io_action_t;

/* Profil d'un volet io, TOUT appris des trames captees de sa VRAIE telecommande, rien suppose :
 *  - cmd_target    : destination des commandes, captee du 0x00 (Somfy 00 01 BF, Velux 00 00 3F) ;
 *  - enroll_target : destination d'enrolement, captee du 0x30 (00 00 3F observe) ;
 *  - payload[a]    : charge utile reelle de chaque action, rejouee VERBATIM (aucune reecriture) ;
 *  - low_power     : octet de controle 1 capte (la DESTINATION est basse conso : solaire vs secteur),
 *                    appris une fois, sert a l'enrolement ET aux commandes ;
 *  - man_id        : fabricant capte du 0x30 (seule valeur de marque ; 0x02 Somfy, 0x01 Velux) ;
 *  - pair_status   : octet de statut capte du 0x30.
 * La porteuse n'est PAS ici : elle vit par appareil (volet_t.freq_khz), MESUREE a la 1re capture
 * (FREQEST du CC1101) puis ajustable comme la porteuse Profalux (meme parametre, pas de code double).
 * Un ancien profil (struct plus petit) est rejete par io_store_get_profile (taille differente) et
 * traite comme a re-appairer. */
#define IO_PROFILE_PAYLOAD_MAX 21   /* = iohome::FRAME_MAX_DATA_SIZE, repris ici pour rester en C */
typedef struct {
    uint8_t cmd_target[3];                                     /* dest commande, captee du 0x00 */
    uint8_t enroll_target[3];                                  /* dest enrolement, captee du 0x30 */
    uint8_t payload[IO_ACTION_COUNT][IO_PROFILE_PAYLOAD_MAX];  /* charge reelle PAR action, captee */
    uint8_t payload_len[IO_ACTION_COUNT];                      /* longueur reelle par action */
    uint8_t low_power;                                         /* f[1] capte (basse conso destination) */
    uint8_t man_id;                                            /* fabricant capte du 0x30 */
    uint8_t pair_status;                                       /* f[26] du 0x30 capte */
} io_profile_t;

/* Compteur roulant INITIAL d'une identite d'enrolement fraiche (jamais vue en NVS).
 * 3, et pas 1 : verite terrain de la passerelle de reference, une identite fraichement creee demarre son 0x30 a
 * 0x0003 (relevee sur deux de ses identites) ; partir de 1/2 risque de
 * les voir lies a leur systeme. */
#define IO_ENROLL_INITIAL_ROLLING 3

/* Ecrit cle, profil et prochain compteur. -1 conserve le compteur (defaut IO_ENROLL_INITIAL_ROLLING).
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
