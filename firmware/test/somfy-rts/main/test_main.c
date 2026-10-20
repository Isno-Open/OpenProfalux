/*
 * Banc de test du codec logique Somfy RTS (somfy_rts.c), sur PC. Voir README.md.
 *
 * On ne teste ici que ce qui est PROUVABLE sans radio : construction des octets,
 * somme de controle, reversibilite de l'obscurcissement, aller-retour des champs,
 * depliage Manchester, et STRUCTURE de la sequence d'emission (T8 : ordre des
 * elements, durees des constantes, fusion des niveaux). La polarite reelle a
 * l'antenne reste, elle, a valider au banc (issue #73).
 *
 * T7 confronte en plus le codec a des VRAIS vecteurs tires des tests unitaires
 * d'open-rts (une implementation tierce), pas a lui-meme : c'est ce qui prouve que
 * notre disposition d'octets, notre checksum et notre obscurcissement sont justes.
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

/* T7 : vecteurs REELS d'open-rts (loopj/open-rts, test/openrts/test_rts_frame.c).
 * On verifie les deux sens contre une implementation tierce : unpack(octets du fil)
 * rend les bons champs, et pack(champs) reproduit les memes octets du fil. Si notre
 * codec divergeait sur un point, ces vecteurs casseraient.
 *
 * Convention d'adresse = celle d'open-rts (octet 4 = poids faible), voir somfy_rts.h.
 * Adresses non symetriques (0x421FEE, 0xFACADE) : une inversion poids fort/faible se
 * verrait. */
static void t7_vecteurs_reels(void) {
    printf("T7 vecteurs reels (open-rts)\n");
    /* Vecteur 1 : UP, code 0x1E63, adresse 0x421FEE, cle 0xA3.
     * Clair A3 29 1E 63 EE 1F 42, obscurci (sur le fil) A3 8A 94 F7 19 06 44. */
    const uint8_t wire1[SOMFY_RTS_FRAME_BYTES] = { 0xA3, 0x8A, 0x94, 0xF7, 0x19, 0x06, 0x44 };
    somfy_rts_frame_t g1;
    CHECK(somfy_rts_unpack(wire1, &g1), "v1 unpack rend true");
    CHECK(g1.key == 0xA3, "v1 cle 0xA3");
    CHECK(g1.cmd == SOMFY_RTS_CMD_UP, "v1 commande UP");
    CHECK(g1.rolling_code == 0x1E63, "v1 code roulant 0x1E63");
    CHECK(g1.address == 0x421FEEu, "v1 adresse 0x421FEE");
    const somfy_rts_frame_t f1 = {
        .key = 0xA3, .cmd = SOMFY_RTS_CMD_UP, .rolling_code = 0x1E63, .address = 0x421FEEu
    };
    uint8_t out1[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f1, out1);
    CHECK(memcmp(out1, wire1, SOMFY_RTS_FRAME_BYTES) == 0, "v1 pack -> A3 8A 94 F7 19 06 44");

    /* Vecteur 2 : UP, code 0x007B, adresse 0xFACADE, cle 0xAB.
     * Clair AB 2F 00 7B DE CA FA, obscurci AB 84 84 FF 21 EB 11. */
    const uint8_t wire2[SOMFY_RTS_FRAME_BYTES] = { 0xAB, 0x84, 0x84, 0xFF, 0x21, 0xEB, 0x11 };
    somfy_rts_frame_t g2;
    CHECK(somfy_rts_unpack(wire2, &g2), "v2 unpack rend true");
    CHECK(g2.key == 0xAB, "v2 cle 0xAB");
    CHECK(g2.cmd == SOMFY_RTS_CMD_UP, "v2 commande UP");
    CHECK(g2.rolling_code == 0x007B, "v2 code roulant 0x007B");
    CHECK(g2.address == 0xFACADEu, "v2 adresse 0xFACADE");
    const somfy_rts_frame_t f2 = {
        .key = 0xAB, .cmd = SOMFY_RTS_CMD_UP, .rolling_code = 0x007B, .address = 0xFACADEu
    };
    uint8_t out2[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f2, out2);
    CHECK(memcmp(out2, wire2, SOMFY_RTS_FRAME_BYTES) == 0, "v2 pack -> AB 84 84 FF 21 EB 11");
}

/* T8 : sequence d'impulsions d'emission. Sans radio on ne prouve pas la polarite,
 * mais on prouve la STRUCTURE : debut en sync materiel (paires a HW_SYNC_US), sync
 * logiciel a SW_SYNC_US, niveaux adjacents toujours differents (fusion), et duree
 * totale egale a la somme attendue des elements. */
static void t8_tx_pulses(void) {
    printf("T8 sequence d'impulsions d'emission\n");
    const somfy_rts_frame_t f = {
        .key = 0xA3, .cmd = SOMFY_RTS_CMD_UP, .rolling_code = 0x1E63, .address = 0x421FEEu
    };
    uint8_t wire[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_pack(&f, wire);
    somfy_rts_pulse_t p[SOMFY_RTS_MAX_PULSES];

    /* 1re trame : HW_SYNC_FIRST (2) paires de sync materiel. */
    int n = somfy_rts_tx_pulses(wire, true, p, SOMFY_RTS_MAX_PULSES);
    CHECK(n > 0 && n <= SOMFY_RTS_MAX_PULSES, "1re trame : n valide");
    CHECK(p[0].level == 1 && p[0].us == SOMFY_RTS_HW_SYNC_US, "[0] = haut 2416 (sync materiel)");
    CHECK(p[1].level == 0 && p[1].us == SOMFY_RTS_HW_SYNC_US, "[1] = bas 2416");
    CHECK(p[2].level == 1 && p[3].level == 0, "deux paires de sync materiel");
    CHECK(p[4].level == 1 && p[4].us == SOMFY_RTS_SW_SYNC_US, "[4] = sync logiciel haut 4550");
    int alt = 1;
    for (int i = 1; i < n; i++) if (p[i].level == p[i - 1].level) alt = 0;
    CHECK(alt, "niveaux adjacents toujours differents (fusion)");
    unsigned long tot = 0; for (int i = 0; i < n; i++) tot += p[i].us;
    const unsigned long att1 = 2ul * SOMFY_RTS_HW_SYNC_FIRST * SOMFY_RTS_HW_SYNC_US
        + SOMFY_RTS_SW_SYNC_US + SOMFY_RTS_HALF_SYMBOL_US
        + 2ul * SOMFY_RTS_FRAME_BITS * SOMFY_RTS_HALF_SYMBOL_US;
    CHECK(tot == att1, "duree totale 1re trame = somme attendue");

    /* Repetition : HW_SYNC_REPEAT (7) paires, le reste identique. */
    int m = somfy_rts_tx_pulses(wire, false, p, SOMFY_RTS_MAX_PULSES);
    CHECK(m > n, "repetition : plus d'impulsions (7 paires contre 2)");
    unsigned long totr = 0; for (int i = 0; i < m; i++) totr += p[i].us;
    const unsigned long attr = 2ul * SOMFY_RTS_HW_SYNC_REPEAT * SOMFY_RTS_HW_SYNC_US
        + SOMFY_RTS_SW_SYNC_US + SOMFY_RTS_HALF_SYMBOL_US
        + 2ul * SOMFY_RTS_FRAME_BITS * SOMFY_RTS_HALF_SYMBOL_US;
    CHECK(totr == attr, "duree totale repetition = somme attendue");

    /* Tampon trop court : refus sans debordement. */
    somfy_rts_pulse_t tiny[4];
    CHECK(somfy_rts_tx_pulses(wire, true, tiny, 4) == -1, "tampon trop court -> -1");
}

/* T9 : telecommande virtuelle. Calee sur l'etat exact des deux vecteurs open-rts,
 * elle doit produire la trame bit pour bit du fil (donc cle = quartet bas du code
 * roulant, et pack correct), puis le code roulant avance et se replie. */
static void t9_remote_next(void) {
    printf("T9 telecommande virtuelle (code roulant + cle)\n");
    somfy_rts_remote_t r1 = { .address = 0x421FEEu, .rolling_code = 0x1E63 };
    uint8_t w1[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_remote_next(&r1, SOMFY_RTS_CMD_UP, w1);
    const uint8_t wire1[SOMFY_RTS_FRAME_BYTES] = { 0xA3, 0x8A, 0x94, 0xF7, 0x19, 0x06, 0x44 };
    CHECK(memcmp(w1, wire1, SOMFY_RTS_FRAME_BYTES) == 0, "v1 : trame = A3 8A 94 F7 19 06 44");
    CHECK(r1.rolling_code == 0x1E64, "v1 : code roulant incremente a 0x1E64");

    somfy_rts_remote_t r2 = { .address = 0xFACADEu, .rolling_code = 0x007B };
    uint8_t w2[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_remote_next(&r2, SOMFY_RTS_CMD_UP, w2);
    const uint8_t wire2[SOMFY_RTS_FRAME_BYTES] = { 0xAB, 0x84, 0x84, 0xFF, 0x21, 0xEB, 0x11 };
    CHECK(memcmp(w2, wire2, SOMFY_RTS_FRAME_BYTES) == 0, "v2 : trame = AB 84 84 FF 21 EB 11");
    CHECK(r2.rolling_code == 0x007C, "v2 : code roulant incremente a 0x007C");

    /* Repli du compteur apres 0xFFFF. */
    somfy_rts_remote_t r3 = { .address = 0x000001u, .rolling_code = 0xFFFF };
    uint8_t w3[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_remote_next(&r3, SOMFY_RTS_CMD_MY, w3);
    CHECK(r3.rolling_code == 0x0000, "code roulant se replie a 0 apres 0xFFFF");

    /* Deux trames successives : codes roulants consecutifs, memes adresse, chacune relue. */
    somfy_rts_remote_t r4 = { .address = 0x0A1B2Cu, .rolling_code = 0x0100 };
    uint8_t a[SOMFY_RTS_FRAME_BYTES], b[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_remote_next(&r4, SOMFY_RTS_CMD_DOWN, a);
    somfy_rts_remote_next(&r4, SOMFY_RTS_CMD_DOWN, b);
    somfy_rts_frame_t ga, gb;
    CHECK(somfy_rts_unpack(a, &ga) && somfy_rts_unpack(b, &gb), "les deux trames se relisent");
    CHECK(ga.rolling_code == 0x0100 && gb.rolling_code == 0x0101, "codes roulants 0x0100 puis 0x0101");
    CHECK(ga.address == 0x0A1B2Cu && gb.address == 0x0A1B2Cu, "meme adresse");
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
    t7_vecteurs_reels();
    t8_tx_pulses();
    t9_remote_next();
    if (g_fail) {
        printf("\n%d verification(s) en echec.\n", g_fail);
        return 1;
    }
    printf("\nTout passe.\n");
    return 0;
}
