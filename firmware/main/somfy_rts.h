/*
 * somfy_rts.h : codec logique du protocole Somfy RTS (433,42 MHz, OOK).
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

/* Porteuse RTS, en kHz : un FAIT du protocole (comme 868,35 pour Profalux), pas un
 * reglage. La legalite de l'emission se verifie cote carte contre bands_allowed_khz. */
#define SOMFY_RTS_FREQ_KHZ  433420

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

/* Durees d'emission, en microsecondes. ALIGNEES sur ESPSomfy-RTS (rstrouse), le
 * projet Somfy le plus utilise, valide sur des milliers de moteurs : on suit sa
 * methode (Transceiver::sendFrame) plutot que de reinventer. Demi-symbole 640, bit
 * 1280, sync materiel 2560 (= 4 demi-symboles), sync logiciel 4850. Le wakeup (haut
 * 10920, bas 7357, sans long silence) et l'inter-trame (27434) sont aussi ceux
 * d'ESPSomfy : ce sont les deux points ou les sources divergeaient le plus. Les
 * moteurs tolerent ~6 % d'ecart sur le symbole, mais on prend la source prouvee. */
#define SOMFY_RTS_WAKEUP_HIGH_US   10920
#define SOMFY_RTS_WAKEUP_LOW_US     7357
#define SOMFY_RTS_HW_SYNC_US        2560   /* demi-periode du preambule materiel (= 4 demi-symboles) */
#define SOMFY_RTS_SW_SYNC_US        4850   /* sync logiciel */
#define SOMFY_RTS_SYMBOL_US         1280   /* periode d'un bit Manchester */
#define SOMFY_RTS_HALF_SYMBOL_US     640   /* demi-symbole Manchester */
#define SOMFY_RTS_INTER_FRAME_US   27434   /* silence entre deux trames repetees */
#define SOMFY_RTS_HW_SYNC_FIRST        2   /* paires de sync materiel, 1re trame */
#define SOMFY_RTS_HW_SYNC_REPEAT       7   /* paires de sync materiel, repetitions */

/* Une trame RTS, cote logique (avant obscurcissement). Disposition sur le fil :
 * octet 0 = cle, octet 1 = commande (quartet haut) + somme de controle (quartet
 * bas), octets 2-3 = code roulant en big-endian, octets 4-6 = adresse en
 * little-endian (octet 4 = poids faible).
 *
 * ATTENTION a la convention d'adresse : open-rts et rtl_433 lisent l'octet 4 en
 * poids faible (celle retenue ici), Nickduino et ESPSomfy-RTS en poids fort. Les
 * memes octets sur le fil donnent donc un entier `address` inverse d'un camp a
 * l'autre. Un vecteur de test emprunte a l'autre convention passerait en inversant
 * l'adresse en silence : on s'aligne sur open-rts/rtl_433. */
typedef struct {
    uint8_t         key;           /* octet de cle : SOMFY_RTS_KEY_PREFIX | nibble roulant */
    somfy_rts_cmd_t cmd;           /* commande (quartet) */
    uint16_t        rolling_code;  /* code tournant, 16 bits (octets 2-3, big-endian) */
    uint32_t        address;       /* adresse telecommande, 24 bits (octets 4-6, little-endian) */
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
 * haut puis bas. C'est la convention des references (G.E. Thomas, front montant =
 * 1 ; Pushstack, rtl_433, Nickduino, ESPSomfy concordent). Reste a confirmer au
 * banc une eventuelle inversion de polarite entre ce niveau logique et l'antenne.
 * Ecrit 2*SOMFY_RTS_FRAME_BITS caracteres puis le '\0'. Rend le nombre de symboles
 * ecrits, ou -1 si `max` est trop court. */
int somfy_rts_manchester(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES], char *out, int max);

/* Inverse de somfy_rts_manchester (sens RECEPTION) : relit 2*SOMFY_RTS_FRAME_BITS
 * symboles '0'/'1' (chaine terminee par '\0') et recompose les 7 octets du fil. Chaque
 * paire doit etre une transition : '0''1' -> bit 1, '1''0' -> bit 0. Rend 0 si tout est
 * valide, ou -1 si la longueur n'est pas 2*FRAME_BITS ou si une paire n'est pas une
 * transition (demi-symbole manquant ou bruite). Les 7 octets rendus sont ceux du fil
 * (obscurcis) : les repasser a somfy_rts_unpack pour obtenir les champs. */
int somfy_rts_demanchester(const char *sym, uint8_t out[SOMFY_RTS_FRAME_BYTES]);

/* Une impulsion OOK : un niveau tenu pendant `us` microsecondes. */
typedef struct {
    uint8_t  level;   /* 0 = bas, 1 = haut */
    uint32_t us;      /* duree, en microsecondes */
} somfy_rts_pulse_t;

/* Majorant du nombre d'impulsions d'UNE trame, avant fusion : sync materiel
 * (2 impulsions par paire, au plus HW_SYNC_REPEAT paires) + sync logiciel (2) +
 * donnees (au plus une impulsion par demi-symbole = 2*FRAME_BITS). */
#define SOMFY_RTS_MAX_PULSES  (2 * SOMFY_RTS_HW_SYNC_REPEAT + 2 + 2 * SOMFY_RTS_FRAME_BITS)

/* Construit la sequence d'impulsions OOK d'UNE trame, prete a jouer sur le RMT :
 * sync materiel (HW_SYNC_FIRST paires a la 1re trame, HW_SYNC_REPEAT aux
 * repetitions), sync logiciel, puis les 56 bits en Manchester. Les niveaux
 * identiques adjacents sont FUSIONNES (le bas du sync logiciel et un premier
 * demi-symbole bas ne font qu'une impulsion). N'inclut NI le wakeup (en tete de
 * transmission) NI le silence inter-trame (entre deux trames) : ce sont des
 * elements d'enveloppe, laisses a la boucle d'emission.
 *
 * `bytes` = les 7 octets DEJA empaquetes (obscurcis), sortie de somfy_rts_pack.
 * Ecrit au plus `max` impulsions (prevoir SOMFY_RTS_MAX_PULSES). Rend le nombre
 * d'impulsions ecrites, ou -1 si un pointeur est nul ou `max` trop petit.
 *
 * Unite PURE, sans radio : la STRUCTURE (ordre, durees des constantes) est testee
 * sur PC. La polarite reelle a l'antenne et la jonction sync logiciel / premier bit
 * restent A VALIDER au banc (issue #73). */
int somfy_rts_tx_pulses(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES], bool first,
                        somfy_rts_pulse_t *out, int max);

/* Telecommande virtuelle Somfy : adresse 24 bits + code roulant 16 bits. Le quartet
 * de cle n'est PAS un etat a part : c'est le quartet BAS du code roulant (verifie sur
 * les vecteurs open-rts : code 0x1E63 -> cle 0xA3, code 0x007B -> cle 0xAB). */
typedef struct {
    uint32_t address;       /* adresse de la telecommande, 24 bits */
    uint16_t rolling_code;  /* compteur anti-rejeu, 16 bits */
} somfy_rts_remote_t;

/* Produit la trame empaquetee (obscurcie) de la commande `cmd` pour la telecommande
 * `r`, avec son code roulant COURANT, puis INCREMENTE le code roulant (repli a 0 apres
 * 0xFFFF). `out` recoit les 7 octets prets a emettre. Le moteur accepte une fenetre de
 * codes en avance : ne pas sauter le compteur hors de cette fenetre, et le persister en
 * NVS cote firmware. */
void somfy_rts_remote_next(somfy_rts_remote_t *r, somfy_rts_cmd_t cmd,
                           uint8_t out[SOMFY_RTS_FRAME_BYTES]);

#endif
