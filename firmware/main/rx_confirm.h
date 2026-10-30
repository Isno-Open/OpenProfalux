#pragma once
/* rx_confirm : une trame recue n'est retenue que CONFIRMEE par une seconde trame identique.
 *
 * Une vraie telecommande repete le meme mot de code tant que le bouton est tenu (meme code
 * tournant, seul le bit Repeat change). Une trame isolee, souvent faible, qui porte un ou deux
 * bits faux ne se repete pas a l'identique : elle creait des telecommandes fantomes (liste de
 * 16 saturee) et pouvait etre apprise (serial ou commande corrompus). Issue #39.
 *
 * Confirmee = memes 64 premiers bits (code tournant, serial, bouton) que la trame precedente,
 * recue moins de RXC_WINDOW_MAX_US apres elle. Les bits 64 (VLOW) et 65 (Repeat) sont ignores.
 *
 * Fenetre ADAPTATIVE, auto, sans reglage : une reception DENSE confirme vite (ecart sous
 * RXC_WINDOW_US) ; une reception CLAIRSEMEE (signal faible, trames rares) confirme plus tard,
 * jusqu'a RXC_WINDOW_MAX_US ; au-dela c'est un nouvel appui. La protection de #39 reste ENTIERE :
 * il faut toujours DEUX trames identiques, jamais une trame isolee. Le champ `weak` dit si la
 * confirmation a demande la fenetre longue (reception faible), pour l'afficher a l'utilisateur
 * sans rien lui demander de regler.
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/rx-confirm. */
#include <stdbool.h>
#include <stdint.h>

#define RXC_CMP_BITS   64          /* bits compares : code tournant + serial + bouton */
#define RXC_FULL_BITS  66          /* trame complete (VLOW + Repeat compris) */
/* Une trame dure ~125 ms (Te 448). L'ecoute se re-arme entre deux trames et peut en manquer
 * une : 500 ms couvre la trame suivante, ou celle d'apres (reception normale). */
#define RXC_WINDOW_US      500000
/* Plafond pour une reception FAIBLE : tant que le bouton est tenu, la telecommande repete le
 * meme code, donc deux trames identiques finissent par arriver meme si le recepteur en rate
 * beaucoup. 4 s couvre un recepteur tres clairseme, sans jamais confirmer entre deux appuis
 * distincts (le code tournant change a chaque appui : pas de fausse confirmation possible). */
#define RXC_WINDOW_MAX_US  4000000

typedef struct {
    char    last[RXC_FULL_BITS + 1];   /* trame precedente ("" = aucune) */
    int64_t last_us;
    char    full[RXC_FULL_BITS + 1];   /* derniere trame de 66 bits de la serie confirmee ("" = aucune) */
    bool    weak;                      /* derniere confirmation obtenue par la fenetre longue (reception faible) */
} rx_confirm_t;

void rx_confirm_reset(rx_confirm_t *c);

/* Presente une trame decodee de n bits ('0'/'1', ordre du fil), recue a now_us. Rend true si
 * elle confirme la precedente. Une trame de moins de 64 bits est ignoree (rend false, etat
 * inchange). Apres un true, c->full contient une trame de 66 bits de la serie si l'une des
 * trames confirmees l'etait (sinon ""), et c->weak vaut true si la confirmation a demande plus
 * de RXC_WINDOW_US (reception faible). */
bool rx_confirm_push(rx_confirm_t *c, const char *bits, int n, int64_t now_us);
