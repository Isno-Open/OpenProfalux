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

/* Plafond de trames repetees APRES la 1re. Borne le train (RAM statique + duree radio).
 * Monte a 23 (24 trames) : certains moteurs n'ENCLENCHENT la course complete qu'au-dela
 * d'un seuil de trames. Mesure communautaire (NodeMCU) : ~2 trames = un cran, ~16 = course
 * complete. Nos 8 d'avant restaient sous le seuil sur un volet a battants (#73). */
#define SOMFY_RTS_TX_REPEATS_MAX  23

/* Nombre de repetitions par type de commande. Monter/descendre : au-dessus du seuil
 * d'enclenchement, pour que le moteur parte en course complete (le modele de la carte =
 * une rafale puis arret minute par do_stop). Stop/My : une rafale courte suffit a figer. */
#define SOMFY_RTS_TX_REPEATS_MOVE 20
#define SOMFY_RTS_TX_REPEATS_STOP 3

/* Traduit un bouton Profalux (PFX_BTN_UP/DOWN/STOP) en commande Somfy RTS. STOP -> My. */
somfy_rts_cmd_t somfy_tx_cmd_from_button(uint8_t pfx_btn);

/* Emet la rafale Somfy RTS de `frame` (7 octets DEJA empaquetes, sortie du codec) :
 * reveil, 1 trame + `repeats` repetitions separees par le silence inter-trame, a
 * 433,42 MHz, puis retour a la porteuse Profalux. `repeats` est borne a
 * SOMFY_RTS_TX_REPEATS_MAX. Rend 0 si emis, -1 sinon. */
int somfy_tx_emit(const uint8_t frame[SOMFY_RTS_FRAME_BYTES], int repeats);

#endif
