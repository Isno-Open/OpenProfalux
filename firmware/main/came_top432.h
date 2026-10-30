#pragma once
#include <stdint.h>

/* Encodeur CAME TOP-432 (issue #102) : telecommande de portail a CODE FIXE 12 bits, OOK PWM a
 * 433,92 MHz. Contrairement au rejeu brut (rf_capture), on RECONSTRUIT une trame propre au
 * timing nominal (TE 320 us), ce qui ouvre le portail la ou le rejeu de l'enveloppe demodulee
 * echouait. Les CAME a code TOURNANT (ATOMO, AKC) ne sont PAS concernes : la TOP-432 est fixe,
 * donc rejouable. Emetteur branche dans emit_virt (shutters.c) comme Profalux et Somfy. */

#define CAME_TOP432_FREQ_KHZ   433920
#define CAME_TOP432_CODE_BITS  12
/* impulsions d'une trame : 1 start + 12 bits * 2 (creux + impulsion) + 1 gap inter-trame */
#define CAME_TOP432_FRAME_PULSES (1 + CAME_TOP432_CODE_BITS * 2 + 1)
/* trames emises par appui : une vraie telecommande emet en continu tant que le bouton est tenu ;
 * on en envoie assez pour que le portail decode (a ajuster au banc si besoin). */
#define CAME_TOP432_PRESS_REPEATS 12

/* Construit le train d'impulsions OOK (paires level/us) d'UNE trame pour code12 (12 bits, MSB
 * d'abord). Rend le nombre d'impulsions ecrites, <0 si le buffer est trop petit. */
int came_top432_build(uint16_t code12, uint8_t *levels, uint32_t *us, int max);

/* Emet code12 `repeats` fois sur la radio 433 (via radio_tx_ook_pulses, qui choisit la puce par
 * la frequence). Rend 0 si emis, <0 sinon. */
int came_top432_tx(uint16_t code12, int repeats);
