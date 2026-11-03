/*
 * came_tx.h : encodeur CAME TOP-432 (OOK 433,92 MHz, code FIXE 12 bits) : E6 de la pyramide.
 *
 * DIFFERENT du rejeu brut (rf_capture/rejeu_emit) : au lieu de rejouer l'enveloppe captee avec ses
 * scories de demodulation (fragile, c'est la regression de Pascal), on REGENERE une trame PROPRE a
 * partir du code 12 bits connu. Timing nominal de la telecommande : PWM a l'impulsion, TE 320 us
 * (court 320 / long 640), 1 cellule de start, 12 bits de code, silence inter-trame ~11,5 ms, repete
 * tant que le bouton est tenu.
 *
 * POLARITE / ORDRE CALES PAR ROUND-TRIP rtl_433 (conf CAME-TOP432 canonique) le 2026-11-03 :
 *   - bit 0 -> impulsion LONGUE haute (640) + creux court (320)
 *   - bit 1 -> impulsion COURTE haute (320) + creux long (640)
 *   - MSB d'abord, precede d'une cellule de start courte.
 * Verifie : le code 926 (0x39E) resynthetise est redecode en 0x39E par rtl_433. Voir
 * firmware/test/came-tx (banc PC) et [[isno-came-top432-encodeur]].
 */
#ifndef CAME_TX_H
#define CAME_TX_H

#include <stdint.h>
#include <stddef.h>

#define CAME_TX_CODE_BITS   12
#define CAME_TX_SHORT_US    320
#define CAME_TX_LONG_US     640
#define CAME_TX_GAP_US      11500   /* silence inter-trame (36 periodes de TE ~ 11,5 ms) */
#define CAME_TX_FREQ_KHZ    433920
#define CAME_TX_REPEATS     8       /* comme une telecommande tenue ~0,5 s */

/* Construit le train d'impulsions (paires niveau/duree us) d'UNE trame CAME pour `code12` (12 bits
 * utiles). Ecrit dans levels[]/us[], au plus `max` elements, rend le nombre d'elements ecrits (0 si
 * `max` trop petit). N'emet rien : sert au banc PC et a came_tx_emit. */
int came_tx_build_frame(uint16_t code12, uint8_t *levels, uint32_t *us, int max);

/* Emet `repeats` trames CAME de `code12` sur 433,92 MHz via l'arbitre radio (radio_tx_ook_pulses).
 * Rend 0 si emis, <0 sinon. */
int came_tx_emit(uint16_t code12, int repeats);

#endif
