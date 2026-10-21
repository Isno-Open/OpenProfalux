#pragma once
/* Backend radio CC1101 pour io-homecontrol : 2-FSK 868 MHz, ~38,4 kbps, sync 0x57FD.
 *
 * La couche protocole (io/iohome_*, CC0) est agnostique de la radio. Ce module fournit
 * la config CC1101, le choix de canal, et (a venir) l'emission/reception de paquets par
 * la FIFO. La config registres est reprise de samr037/iohc-flipper (MIT), qui pilote de
 * l'io sur un vrai CC1101 : valeurs prouvees, pas devinees. Voir firmware/main/io/README.md. */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canaux io (half-duplex, saut de frequence). */
typedef enum { IO_CH1 = 0, IO_CH2 = 1, IO_CH3 = 2 } io_channel_t;

/* Applique la config CC1101 io (2-FSK, debit, deviation, sync, PATABLE) et pose le canal
 * par defaut (CH2, 868,95 MHz). A appeler sous le mutex radio ; l'appelant restaure ensuite
 * la porteuse/config Profalux (meme patron que le retune Somfy RTS). */
void io_radio_config(void);

/* Pose la frequence du canal io (CH1 868,25 / CH2 868,95 / CH3 869,85 MHz). */
void io_radio_set_channel(io_channel_t ch);

/* Emission / reception d'un paquet io par la FIFO CC1101. PAS ENCORE CABLE : demande des
 * primitives FIFO/strobe a exposer dans cc1101.c (le chemin OOK actuel passe par le RMT, pas
 * la FIFO). Reference d'implementation : iohc-flipper radio_cc1101.c / phy_rx.c (MIT). */
int io_radio_tx_packet(const uint8_t *buf, size_t len);
int io_radio_rx_packet(uint8_t *buf, size_t max, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
