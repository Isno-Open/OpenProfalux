#pragma once
/* Pile des telecommandes : decision d'alerte a partir du bit VLOW (bit 64 de la trame).
 *
 * Une trame captee pres du bruit peut porter un bit faux (constate le 2026-10-10 : un meme
 * appui decode en deux trames differentes a -91 dBm). On n'alerte donc qu'apres le bit VLOW
 * a 1 sur BATT_LOW_PRESSES appuis DISTINCTS d'affilee, et une seule trame VLOW a 0 leve
 * l'alerte (pile changee). A n'appeler qu'une fois par appui (nouveau code tournant) et
 * seulement sur une trame complete (66 bits).
 *
 * Module PUR : teste sur PC, voir firmware/test/remote-battery.
 */
#include <stdbool.h>
#include <stdint.h>

#define BATT_LOW_PRESSES 2

typedef struct {
    uint8_t hits;   /* appuis distincts d'affilee avec VLOW = 1 */
    bool    low;    /* alerte pile faible en cours */
    bool    known;  /* au moins une trame complete vue depuis le demarrage */
} batt_state_t;

/* Prend en compte un appui. Rend true si l'etat publie (low, ou connu/inconnu) change. */
bool batt_on_press(batt_state_t *s, bool vlow);
