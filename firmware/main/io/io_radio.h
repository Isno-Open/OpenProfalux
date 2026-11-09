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

/* Pose le canal io et la correction NVS cfg/io_offset (kHz, defaut 0). */
void io_radio_set_channel(io_channel_t ch);

/* Applique une nouvelle correction de quartz io (kHz, -100..100) sans relire la NVS. */
void io_radio_set_freq_offset(int32_t khz);
/* Porteuse reelle (kHz absolus, hors offset carte) mesuree sur la DERNIERE trame io captee :
 * canal io + FREQEST. Apprise a la capture (geste 1) pour alimenter le parametre frequence par
 * appareil existant (volet_t.freq_khz). 0 = pas encore mesure. */
uint32_t io_radio_last_carrier_khz(void);
/* Frequence nominale (kHz) d'un canal io (CH1 868250, CH2 868950, CH3 869850), pour le journal. */
uint32_t io_radio_channel_khz(io_channel_t ch);
/* Pose la porteuse io a une valeur absolue (surcharge PAR APPAREIL) ; l'offset carte s'ajoute encore.
 * A appeler APRES io_radio_config (qui a charge l'offset). Sans appel = CH2 868950 par defaut. */
void io_radio_set_carrier_khz(uint32_t khz);

/* Premiere trame : preambule 500 ms ; suivantes : 10 ms. Une config ouvre une rafale. */
int io_radio_tx_packet(const uint8_t *buf, size_t len);

/* Encapsule `n` octets io (UART 8N1) et les emet. CHEMIN D'EMISSION UNIQUE : tout ce qui part
 * en io passe par ici, et y est journalise (« Trame io emise (N octets) : ... »), dans la meme
 * forme que la ligne de reception de radio.c. Rend 0 si l'emission a eu lieu.
 *
 * ⚠️ Avant le 2026-10-28, chaque appelant encapsulait de son cote et n'ecrivait rien : de test1
 * a test8 (issue #87) on a compare notre INTENTION au trafic d'une passerelle tierce, jamais nos octets aux
 * siens. Un defaut d'assemblage ne pouvait se voir nulle part. */
int io_radio_tx_frame(const uint8_t *io_bytes, uint8_t n);
/* Variante horodatee pour une paire dont l'intervalle RF doit etre exact. `data_start_us == 0`
 * conserve le preambule normal ; sinon les premiers bits de donnees sont programmes a cet instant
 * esp_timer. `actual_start_us` recoit l'instant effectif de remplissage de la FIFO. */
int io_radio_tx_frame_at(const uint8_t *io_bytes, uint8_t n, int64_t data_start_us,
                         int64_t *actual_start_us);
int io_radio_rx_packet(uint8_t *buf, size_t max, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
