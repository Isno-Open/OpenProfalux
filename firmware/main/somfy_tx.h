/*
 * somfy_tx.h : emission radio Somfy RTS sur le CC1101 (issue #73).
 *
 * MODULE separe, branche sur le seul aiguillage emit_virt (shutters.c) : le chemin
 * Profalux ne bouge pas. Il assemble l'ENVELOPPE d'une rafale (reveil, trames, silence
 * inter-trame, repetitions) a partir du codec PUR somfy_rts (qui, lui, ne porte qu'une
 * trame), retune la porteuse a 433,42 MHz par l'arbitrage radio.c, puis rend la main.
 *
 * Ce qui reste a VALIDER au banc (pas compile-verifiable) : la polarite Manchester a
 * l'antenne, la jonction sync logiciel / premier bit, et qu'un moteur suive bien.
 */
#ifndef SOMFY_TX_H
#define SOMFY_TX_H

#include <stdint.h>
#include "somfy_rts.h"

/* Plafond de trames repetees APRES la 1re (borne le train : RAM statique + duree radio). */
#define SOMFY_RTS_TX_REPEATS_MAX  7

/* Repetitions par appui bref. En 80 bits (format qui ENCLENCHE la course complete sur les
 * moteurs concernes), la vraie telecommande emet 2 trames par appui (#73, Machin77), donc
 * 1 repetition apres la 1re. Ce n'est PAS le nombre de trames qui fait la course complete,
 * c'est le format 80 bits (ma piste "plus de trames", test2, etait fausse). */
#define SOMFY_RTS_TX_REPEATS  1

/* Traduit un bouton Profalux (PFX_BTN_UP/DOWN/STOP) en commande Somfy RTS. STOP -> My. */
somfy_rts_cmd_t somfy_tx_cmd_from_button(uint8_t pfx_btn);

/* Emet la rafale Somfy RTS de `frame` (7 octets DEJA empaquetes, sortie du codec) :
 * reveil, 1 trame + `repeats` repetitions separees par le silence inter-trame, a
 * 433,42 MHz, puis retour a la porteuse Profalux. `repeats` est borne a
 * SOMFY_RTS_TX_REPEATS_MAX. Rend 0 si emis, -1 sinon. */
int somfy_tx_emit(const uint8_t frame[SOMFY_RTS_FRAME_BYTES], int repeats);

/* Emet en FORMAT 80 BITS (RTS "80-S") : reveil, 1 trame + `repeats` repetitions, chaque
 * trame = `base56` (7 octets empaquetes) + 3 octets d'extension recalcules selon `cmd` et
 * l'index de repetition (somfy_rts_frame80). C'est ce format qui declenche la course complete
 * sur les moteurs concernes (#73). Rend 0 si emis, -1 sinon. */
int somfy_tx_emit80(const uint8_t base56[SOMFY_RTS_FRAME_BYTES], somfy_rts_cmd_t cmd, int repeats);

#endif
