/*
 * Banc de test du codec logique Somfy RTS (somfy_rts.c), sur PC. Voir README.md.
 *
 * On ne teste ici que ce qui est PROUVABLE sans radio : construction des octets,
 * somme de controle, reversibilite de l'obscurcissement, aller-retour des champs,
 * et depliage Manchester. La polarite a l'antenne et les durees d'emission restent
 * a valider au banc (issue #73) et ne sont pas testees ici.
 */
#include "somfy_rts.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, nom) do {                                   \
    if (cond) {                                                 \
        printf("  ok   %s\n", (nom));                           \
    } else {                                                    \
        printf("  FAIL %s  (%s:%d)\n", (nom), __FILE__, __LINE__); \
        g_fail++;                                               \
    }                                                           \
} while (0)

/* T0 : somme de controle, valeur calculee a la main.
 * bytes = A7 20 00 00 00 00 00 -> quartets A^7^2 = 0xF. */
static void t0_checksum(void) {
    printf("T0 somme de controle (valeur en dur)\n");
    const uint8_t bytes[SOMFY_RTS_FRAME_BYTES] = { 0xA7, 0x20, 0, 0, 0, 0, 0 };
    CHECK(somfy_rts_checksum(bytes) == 0xF, "A7 20 .. -> 0xF");
    const uint8_t zero[SOMFY_RTS_FRAME_BYTES] = { 0 };
    CHECK(somfy_rts_checksum(zero) == 0x0, "trame nulle -> 0x0");
}

/* T1 : pack sur un vecteur en dur, octets obscurcis attendus.
 * {key A7, UP, rolling 0, addr 0} -> clair A7 2F 00.. -> obscurci A7 88 88 88 88 88 88. */
static void t1_pack_vecteur(void) {
    printf("T1 pack, vecteur obscurci en dur\n");
    const somfy_rts_frame_t f = {
        .key = 0xA7, .cmd = SOMFY_RTS_CMD_UP, .rolling_code = 0, .address = 0
    };
    uint8_t out[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f, out);
    const uint8_t att[SOMFY_RTS_FRAME_BYTES] = { 0xA7, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 };
    CHECK(memcmp(out, att, SOMFY_RTS_FRAME_BYTES) == 0, "A7 88 88 88 88 88 88");
}

/* T2 : aller-retour pack -> unpack, champs preserves. */
static void t2_roundtrip(void) {
    printf("T2 aller-retour pack/unpack\n");
    const somfy_rts_frame_t f = {
        .key = 0xA3, .cmd = SOMFY_RTS_CMD_DOWN,
        .rolling_code = 0x1234, .address = 0x0A1B2C
    };
    uint8_t wire[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f, wire);
    somfy_rts_frame_t g;
    CHECK(somfy_rts_unpack(wire, &g), "unpack rend true");
    CHECK(g.key == f.key, "key preserve");
    CHECK(g.cmd == f.cmd, "cmd preservee");
    CHECK(g.rolling_code == f.rolling_code, "code tournant preserve");
    CHECK(g.address == f.address, "adresse preservee");
}

/* T3 : le dernier octet corrompu casse la somme de controle.
 * On corrompt le DERNIER octet : l'obscurcissement en cascade fait qu'un bit du
 * dernier octet ne se propage que sur un seul quartet clair, donc la somme de
 * controle le voit. (Un bit sur un octet du milieu touche deux quartets clairs
 * d'un coup : la somme XOR sur 4 bits peut l'annuler. La somme de controle RTS
 * est faible, elle n'attrape pas toute erreur du fil.) */
static void t3_checksum_refuse(void) {
    printf("T3 dernier octet corrompu -> unpack refuse\n");
    const somfy_rts_frame_t f = {
        .key = 0xA9, .cmd = SOMFY_RTS_CMD_MY, .rolling_code = 0xBEEF, .address = 0x123456
    };
    uint8_t wire[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f, wire);
    wire[SOMFY_RTS_FRAME_BYTES - 1] ^= 0x40;   /* un quartet clair change */
    somfy_rts_frame_t g;
    CHECK(!somfy_rts_unpack(wire, &g), "unpack rend false");
}

/* T4 : depliage Manchester, longueur et transitions valides. */
static void t4_manchester(void) {
    printf("T4 depliage Manchester\n");
    const somfy_rts_frame_t f = {
        .key = 0xA0, .cmd = SOMFY_RTS_CMD_PROG, .rolling_code = 0x00FF, .address = 0x0055AA
    };
    uint8_t wire[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f, wire);
    char sym[2 * SOMFY_RTS_FRAME_BITS + 1];
    int n = somfy_rts_manchester(wire, sym, sizeof(sym));
    CHECK(n == 2 * SOMFY_RTS_FRAME_BITS, "112 symboles");
    CHECK(sym[n] == '\0', "chaine terminee");
    int ok_trans = 1, collapse_ok = 1;
    for (int i = 0; i < SOMFY_RTS_FRAME_BITS; i++) {
        char a = sym[2 * i], b = sym[2 * i + 1];
        if (!((a == '0' && b == '1') || (a == '1' && b == '0'))) ok_trans = 0;
        /* recomposer le bit de donnee : '0''1' = 1, '1''0' = 0 */
        int bit = (a == '0' && b == '1') ? 1 : 0;
        int byte = i >> 3, shift = 7 - (i & 7);
        int src = (wire[byte] >> shift) & 1;
        if (bit != src) collapse_ok = 0;
    }
    CHECK(ok_trans, "chaque symbole est une transition 01 ou 10");
    CHECK(collapse_ok, "recomposition = bits d'origine");
}

/* T5 : un tampon trop court est refuse sans ecrire. */
static void t5_manchester_court(void) {
    printf("T5 tampon Manchester trop court\n");
    const uint8_t wire[SOMFY_RTS_FRAME_BYTES] = { 0xA7, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 };
    char sym[8];
    CHECK(somfy_rts_manchester(wire, sym, sizeof(sym)) == -1, "rend -1");
}

/* T6 : toutes les commandes font un aller-retour par leur quartet. */
static void t6_commandes(void) {
    printf("T6 aller-retour de chaque commande\n");
    const somfy_rts_cmd_t cmds[] = {
        SOMFY_RTS_CMD_MY, SOMFY_RTS_CMD_UP, SOMFY_RTS_CMD_MY_UP, SOMFY_RTS_CMD_DOWN,
        SOMFY_RTS_CMD_MY_DOWN, SOMFY_RTS_CMD_UP_DOWN, SOMFY_RTS_CMD_PROG,
        SOMFY_RTS_CMD_SUN_FLAG, SOMFY_RTS_CMD_FLAG,
    };
    int all = 1;
    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        somfy_rts_frame_t f = { .key = 0xA5, .cmd = cmds[i], .rolling_code = (uint16_t)(i + 1), .address = 0x0000AB };
        uint8_t wire[SOMFY_RTS_FRAME_BYTES];
        somfy_rts_pack(&f, wire);
        somfy_rts_frame_t g;
        if (!somfy_rts_unpack(wire, &g) || g.cmd != cmds[i]) all = 0;
    }
    CHECK(all, "les 9 commandes survivent a l'aller-retour");
}

int main(void) {
    printf("== Banc Somfy RTS (codec logique) ==\n");
    t0_checksum();
    t1_pack_vecteur();
    t2_roundtrip();
    t3_checksum_refuse();
    t4_manchester();
    t5_manchester_court();
    t6_commandes();
    if (g_fail) {
        printf("\n%d verification(s) en echec.\n", g_fail);
        return 1;
    }
    printf("\nTout passe.\n");
    return 0;
}
