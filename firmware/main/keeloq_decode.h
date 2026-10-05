#pragma once
#include <stdint.h>
#include <stddef.h>

/* Decodeur OOK HCS30x / KeeLoq, PUR (aucune dependance ESP-IDF) : il se compile et se
 * teste sur hote (cible linux), cf firmware/test/keeloq. cc1101.c lui passe ses symboles
 * RMT via un cast (meme disposition binaire, verifie par _Static_assert cote cc1101.c). */

/* Capacite de capture, en symboles. Source unique : cc1101.c dimensionne son buffer RX
 * dessus, et le decodeur dimensionne sa liste de fronts dessus. */
#define KEELOQ_CAP_SYMBOLS 1024

/* Symbole RMT minimal : meme disposition binaire que esp-idf rmt_symbol_word_t
 * (duration0:15, level0:1, duration1:15, level1:1), pour decoder sans les en-tetes RMT. */
typedef struct {
    uint16_t duration0 : 15;
    uint16_t level0    : 1;
    uint16_t duration1 : 15;
    uint16_t level1    : 1;
} kq_sym_t;

/* Decode une capture OOK HCS30x/KeeLoq (de-glitch + scan des entetes + vote majoritaire).
 * raw/raw_n : symboles captes. out : chaine de '0'/'1' terminee (ordre du fil). max_bits :
 * taille utile de out (>=67 conseille). out_te (optionnel) : TE moyen mesure, en us.
 * Retour : nombre de bits (66 si trame votee complete ; meilleure partielle sinon ; -4 si
 * aucune entete). */
int keeloq_decode(const kq_sym_t *raw, size_t raw_n, char *out, int max_bits, int *out_te);
