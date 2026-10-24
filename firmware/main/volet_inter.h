#pragma once
/* Position intermediaire d'un moteur (la « position de confort » de la notice Profalux de la
 * telecommande Noe, DOC NB016) : c'est le MOTEUR qui la retient, pas la telecommande.
 *   - y aller : appui simultane Montee + Stop (0x6) ou Stop + Descente (0xC) ;
 *   - l'enregistrer : Montee + Descente (0xA) tenus 5 s, le volet fait un mouvement.
 * Le commutateur P est un bouton de plus (S0, 0x1) : P + Montee + Descente vaut 0xB, la
 * programmation d'un emetteur, sans confusion possible avec 0xA.
 *
 * Le moteur ne renvoie rien : le boitier suit la position par le temps, vers le % de
 * fermeture que l'utilisateur a donne (ou releve a l'enregistrement), -1 s'il est inconnu.
 *
 * Module PUR : teste sur PC, voir firmware/test/volet-inter.
 */
#include <stdbool.h>
#include <stdint.h>

#define INTER_BTN_GOTO     0x6    /* Montee + Stop (0xC = Stop + Descente, meme effet) */
#define INTER_BTN_SAVE     0xA    /* Montee + Descente (tenus 5 s : l'enregistrement) */

bool inter_btn_goto(uint8_t button);   /* 0x6 ou 0xC */
bool inter_btn_save(uint8_t button);   /* 0xA */

typedef enum {
    INTER_KEEP,     /* rien a changer (repetition d'un meme appui, ou deja sur place a l'arret) */
    INTER_FREEZE,   /* figer le suivi : % inconnu, ou deja sur place */
    INTER_MOVE,     /* suivre le moteur vers pct, dans le sens *ndir (+1 monte, -1 descend) */
} inter_act_t;

/* Le moteur part de lui-meme vers sa position intermediaire : que faire du suivi ?
 * dir/target : suivi en cours (dir 0 = arrete, target -1 = aucune) ; position : estimee,
 * en % de fermeture ; pct : celui de la position intermediaire, -1 s'il est inconnu.
 * A 1 % pres, le volet est deja sur place (meme tolerance que la commande "pos"). */
inter_act_t inter_plan(int dir, int target, float position, int pct, int *ndir);
