/*
 * radio.h — arbitrage RX/TX sur l'unique CC1101 (GDO0 + canal RMT partages).
 *
 * Le driver cc1101 ne fait PAS de RX asynchrone (cc1101_rx_start est un squelette) :
 * le seul RX reel est cc1101_rx_listen_bits() (bloquant). RX et TX se disputent GDO0
 * et le canal RMT unique. Ce module serialise tout via un mutex + une tache RX unique :
 *   - une tache ecoute en fenetres courtes et dispatche chaque trame recue au callback,
 *   - radio_tx() prend le mutex et rejoue une trame (replay UI/MQTT/HA),
 *   - radio_pause_rx() suspend l'ecoute pendant qu'on STREAM notre propre commande
 *     (maintien), pour que le TX ne soit jamais bloque par une fenetre RX en cours,
 *   - radio_listen_confirmed() fait une ecoute dediee (apprentissage) sans collision.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Callback appele (hors mutex) pour chaque trame recue. bits = chaine '0'/'1' ordre du fil. */
typedef void (*radio_frame_cb_t)(const char *bits, uint32_t serial, uint8_t button,
                                 uint32_t hop, int8_t rssi);

void radio_init(void);                 /* cree le mutex (idempotent) */
void radio_start(radio_frame_cb_t cb); /* demarre la tache RX (en veille tant que listening OFF) */
void radio_tx(const char *bits, int repeats);   /* rejoue une trame `repeats` fois en UNE rafale TX (mutex-garde) */
/* Ecoute dediee (apprentissage, capture d'enrolement), meme ecoute permanente coupee. Rend la
 * 1re trame CONFIRMEE par une seconde identique (rx_confirm.h) ; full = exiger une trame complete
 * de 66 bits. max >= 67. Rend le nombre de bits, ou <0 si rien de confirme avant timeout_ms. */
int  radio_listen_confirmed(uint32_t timeout_ms, char *buf, int max, bool full);
void radio_pause_rx(bool pause);       /* pause TEMPORAIRE pendant nos TX (stream maintien) */
void radio_set_listening(bool on);     /* interrupteur MAITRE : l'ecoute permanente (case UI "capture") */

/* Auto-calibration du gain RX : balaie les gains, verrouille le 1er qui capte une trame
 * valide pendant que l'utilisateur appuie sur sa telecommande. Non bloquant (tache de fond). */
int  radio_set_freq_khz(uint32_t khz); /* frequence porteuse a chaud (sous mutex) ; 0=OK, -1=non prise */
int  radio_tx_selftest(void);          /* relance le self-test TX (sous mutex) ; 0=OK, -1=HS */
int  radio_calibrate_start(void);      /* 0=lance, 1=deja en cours, -1=erreur */
void radio_calibrate_status(int *state, uint8_t *testing, uint8_t *result); /* state:0 idle/1 run/2 ok/3 echec */

/* Balayage RSSI (issue #51) : porteuse de la telecommande pendant une emission longue (commutateur
 * P seul). Non bloquant ; n'applique rien. start : 0 lance, 1 deja en cours, -1 erreur.
 * status : rend l'etat (0 jamais, 1 en cours, 2 fini) ; *status = fsweep_status_t (0 = trouvee)
 * ou -10 (puce pas en RX) ; la courbe (khz[], hdb[] en demi-dB, n points) une fois fini. */
int  radio_sweep_start(void);
int  radio_sweep_status(int *status, uint32_t *from_khz, const uint32_t **khz, const int16_t **hdb, int *n,
                        uint32_t *center_khz, int16_t *peak_hdb, int16_t *floor_hdb);
