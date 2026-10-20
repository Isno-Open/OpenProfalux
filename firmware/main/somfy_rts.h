/*
 * somfy_rts.h — codec logique du protocole Somfy RTS (433,42 MHz, OOK).
 *
 * Prepare le terrain pour piloter des volets Somfy RTS depuis la meme carte que
 * Profalux (issue #73). Ce module est le NIVEAU LOGIQUE de la trame : il construit
 * et relit les 7 octets d'une trame RTS (somme de controle, obscurcissement en
 * cascade) et deplie les bits en symboles Manchester. Il ne touche NI la radio NI
 * l'ESP-IDF : module PUR, teste sur PC (voir firmware/test/somfy-rts).
 *
 * Ce qui N'EST PAS encore fait, et attend un vrai moteur au banc (issue #73) :
 *   - l'emission reelle (preambule materiel, sync logiciel, Manchester) sur le
 *     CC1101, dont les durees sont declarees plus bas mais PAS encore validees ;
 *   - la polarite Manchester a l'antenne, a confirmer sur une capture reelle ;
 *   - l'appairage (mode prog) et la persistance du code tournant en NVS.
 *
 * Le RTS est entierement documente, et sans cle a recuperer (contrairement a
 * Profalux/KeeLoq) : l'obscurcissement est un simple XOR en cascade, la somme de
 * controle un XOR de quartets. References : Pushstack « Somfy RTS protocol »,
 * projets open-rts (loopj) et ESPSomfy-RTS.
 */
#ifndef SOMFY_RTS_H
#define SOMFY_RTS_H

#include <stdbool.h>
#include <stdint.h>

/* Trame RTS « classique » : 7 octets = 56 bits. */
#define SOMFY_RTS_FRAME_BYTES 7
#define SOMFY_RTS_FRAME_BITS  56

/* Quartet haut constant de l'octet de cle (0xA pour le RTS). */
#define SOMFY_RTS_KEY_PREFIX  0xA0

/* Commandes RTS : quartet haut de l'octet 1. Valeurs du protocole, nommees une
 * fois ici plutot que recopiees en litteraux. */
typedef enum {
    SOMFY_RTS_CMD_MY       = 0x1,  /* stop / position favorite */
    SOMFY_RTS_CMD_UP       = 0x2,
    SOMFY_RTS_CMD_MY_UP    = 0x3,
    SOMFY_RTS_CMD_DOWN     = 0x4,
    SOMFY_RTS_CMD_MY_DOWN  = 0x5,
    SOMFY_RTS_CMD_UP_DOWN  = 0x6,
    SOMFY_RTS_CMD_PROG     = 0x8,  /* appairage */
    SOMFY_RTS_CMD_SUN_FLAG = 0x9,
    SOMFY_RTS_CMD_FLAG     = 0xA,
} somfy_rts_cmd_t;

/* Durees d'emission, en microsecondes. DECLAREES d'apres les references
 * publiques, A VALIDER au banc (issue #73) : rien ne les consomme encore. */
#define SOMFY_RTS_WAKEUP_HIGH_US    9415
#define SOMFY_RTS_WAKEUP_LOW_US    89565
#define SOMFY_RTS_HW_SYNC_US        2416   /* demi-periode du preambule materiel */
#define SOMFY_RTS_SW_SYNC_US        4550   /* sync logiciel */
#define SOMFY_RTS_SYMBOL_US         1208   /* periode d'un bit Manchester */
#define SOMFY_RTS_HALF_SYMBOL_US     604   /* demi-symbole Manchester */
#define SOMFY_RTS_INTER_FRAME_US   30415   /* silence entre deux trames repetees */
#define SOMFY_RTS_HW_SYNC_FIRST        2   /* paires de sync materiel, 1re trame */
#define SOMFY_RTS_HW_SYNC_REPEAT       7   /* paires de sync materiel, repetitions */

/* Une trame RTS, cote logique (avant obscurcissement). */
typedef struct {
    uint8_t         key;           /* octet de cle : SOMFY_RTS_KEY_PREFIX | nibble roulant */
    somfy_rts_cmd_t cmd;           /* commande (quartet) */
    uint16_t        rolling_code;  /* code tournant, 16 bits */
    uint32_t        address;       /* adresse de la telecommande, 24 bits */
} somfy_rts_frame_t;

/* Somme de controle RTS : XOR de tous les quartets des 7 octets, le quartet de
 * controle devant valoir zero. Rend une valeur sur 4 bits. */
uint8_t somfy_rts_checksum(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES]);

/* Serialise `f` en 7 octets prets a emettre : pose les champs, calcule la somme
 * de controle, puis applique l'obscurcissement en cascade (bytes[i] ^= bytes[i-1]). */
void somfy_rts_pack(const somfy_rts_frame_t *f, uint8_t out[SOMFY_RTS_FRAME_BYTES]);

/* Relit 7 octets recus : annule l'obscurcissement, verifie la somme de controle,
 * remplit `f`. Rend false (et `f` inchange) si la somme de controle est fausse. */
bool somfy_rts_unpack(const uint8_t in[SOMFY_RTS_FRAME_BYTES], somfy_rts_frame_t *f);

/* Deplie les 56 bits (poids fort en tete) en symboles Manchester : deux niveaux
 * '0'/'1' par bit de donnee. Convention : un bit a 1 = bas puis haut, un bit a 0 =
 * haut puis bas (polarite A VALIDER au banc). Ecrit 2*SOMFY_RTS_FRAME_BITS
 * caracteres puis le '\0'. Rend le nombre de symboles ecrits, ou -1 si `max` est
 * trop court. */
int somfy_rts_manchester(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES], char *out, int max);

#endif
