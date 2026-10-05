/*
 * Banc de test du decodeur OOK HCS30x/KeeLoq (keeloq_decode.c), sur PC.
 *
 * Le decodeur est PUR : il prend des symboles RMT (duree/niveau) et rend la
 * chaine de bits du fil. On lui fabrique ici des trames « en bouchon », avec les
 * memes temps qu'une HCS301 reelle (Te ~430 us, '0' = 2Te haut +1Te bas, '1' =
 * 1Te haut +2Te bas, en-tete LOW long ~10 Te), et on verifie ce qu'il rend.
 *
 * Ce qui est couvert : une trame propre, le 66e bit coupe par l'idle (sans BAS
 * final, correctif @Akkeoss), un glitch de demod recolle par le de-glitch, du
 * bruit sans en-tete (-4), et le vote majoritaire qui corrige un bit faux dans
 * une repetition. Rend 0 si tout passe, 1 sinon.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "keeloq_decode.h"

#define TE 430 /* temps elementaire, comme la vraie carte */

/* Une trame de reference arbitraire mais fixe : 66 bits, ordre du fil. */
static const char EXPECTED[] =
    "100110100011110000101011"
    "110100101100011110000101"
    "101100011110000101";

/* ---- Fabrique de trames : liste de fronts, puis compactage en symboles. ---- */

typedef struct {
    uint16_t dur;
    uint8_t lvl;
} edge_t;

static void push(edge_t *e, size_t *n, uint16_t dur, uint8_t lvl) {
    e[*n].dur = dur;
    e[*n].lvl = lvl;
    (*n)++;
}

/* Preambule minimal (une pulse HIGH, pour que deux LOW ne fusionnent pas au
 * de-glitch) puis l'en-tete : un LOW long ~10 Te, que le decodeur prend comme
 * point d'ancrage (fenetre 3000..8500 us). */
static void push_header(edge_t *e, size_t *n) {
    push(e, n, TE, 1);
    push(e, n, 10 * TE, 0);
}

/* Les 66 bits. final_low=false : on omet le DERNIER BAS (66e bit coupe par
 * l'idle, cas @Akkeoss). glitch_bit>=0 : le HAUT de ce bit est fendu par un
 * sliver BAS < GLITCH_US, que le de-glitch doit jeter et recoller. */
static void push_bits(edge_t *e, size_t *n, const char *bits, bool final_low, int glitch_bit) {
    int nb = (int)strlen(bits);
    for (int i = 0; i < nb; i++) {
        uint16_t hi = (bits[i] == '1') ? TE : 2 * TE;
        uint16_t lo = (bits[i] == '1') ? 2 * TE : TE;
        if (i == glitch_bit) {
            push(e, n, hi / 2, 1);
            push(e, n, 60, 0); /* sliver < GLITCH_US (140) : bruit, doit etre jete */
            push(e, n, hi - hi / 2, 1);
        } else {
            push(e, n, hi, 1);
        }
        if (i == nb - 1 && !final_low) break; /* 66e bit sans BAS final */
        push(e, n, lo, 0);
    }
}

/* Compacte les fronts en symboles RMT (2 fronts par symbole) et termine par un
 * symbole de duree 0, comme le marqueur de fin du pilote RMT. */
static size_t pack(const edge_t *e, size_t n, kq_sym_t *out, size_t cap) {
    size_t s = 0;
    for (size_t i = 0; i < n && s < cap; i += 2) {
        out[s].duration0 = e[i].dur;
        out[s].level0 = e[i].lvl;
        if (i + 1 < n) {
            out[s].duration1 = e[i + 1].dur;
            out[s].level1 = e[i + 1].lvl;
        } else {
            out[s].duration1 = 0;
            out[s].level1 = 0;
        }
        s++;
    }
    if (n % 2 == 0 && s < cap) { /* compte pair : il faut encore le marqueur de fin */
        out[s].duration0 = 0;
        out[s].level0 = 0;
        out[s].duration1 = 0;
        out[s].level1 = 0;
        s++;
    }
    return s;
}

/* ---- Verification ---- */

static int g_fail = 0;

static void check(bool ok, const char *name, const char *msg) {
    if (ok) {
        printf("  ok   %s\n", name);
    } else {
        printf("  FAIL %s : %s\n", name, msg);
        g_fail++;
    }
}

/* Decode une liste de fronts deja construite. */
static int run(const edge_t *e, size_t n, char *out, int *te) {
    static kq_sym_t syms[KEELOQ_CAP_SYMBOLS];
    size_t ns = pack(e, n, syms, KEELOQ_CAP_SYMBOLS);
    return keeloq_decode(syms, ns, out, 80, te);
}

int main(void) {
    char out[80];
    int te;

    if (strlen(EXPECTED) != 66) {
        printf("EXPECTED fait %zu bits, attendu 66\n", strlen(EXPECTED));
        return 2;
    }

    printf("Banc keeloq_decode\n");

    /* T0 : trame propre, une repetition. */
    {
        static edge_t e[2048];
        size_t n = 0;
        push_header(e, &n);
        push_bits(e, &n, EXPECTED, true, -1);
        te = 0;
        int nb = run(e, n, out, &te);
        check(nb == 66, "T0 trame propre : 66 bits", "mauvais nombre de bits");
        check(strcmp(out, EXPECTED) == 0, "T0 trame propre : bits exacts", out);
        check(te >= 380 && te <= 480, "T0 trame propre : Te ~430", "Te hors plage");
    }

    /* T1 : 66e bit coupe par l'idle (pas de BAS final) -> doit quand meme rendre
     * 66 bits (correctif @Akkeoss, issue #12). */
    {
        static edge_t e[2048];
        size_t n = 0;
        push_header(e, &n);
        push_bits(e, &n, EXPECTED, false, -1);
        int nb = run(e, n, out, NULL);
        check(nb == 66, "T1 66e bit sans BAS : 66 bits", "le 66e bit est perdu");
        check(strcmp(out, EXPECTED) == 0, "T1 66e bit sans BAS : bits exacts", out);
    }

    /* T2 : un glitch de demod (sliver < GLITCH_US) fend un HAUT -> le de-glitch
     * doit le jeter, recoller le HAUT, et rendre la trame intacte. */
    {
        static edge_t e[2048];
        size_t n = 0;
        push_header(e, &n);
        push_bits(e, &n, EXPECTED, true, 20); /* glitch sur le bit 20 */
        int nb = run(e, n, out, NULL);
        check(nb == 66, "T2 glitch recolle : 66 bits", "nombre de bits");
        check(strcmp(out, EXPECTED) == 0, "T2 glitch recolle : bits exacts", out);
    }

    /* T3 : du bruit sans en-tete long -> aucun ancrage -> -4. */
    {
        static edge_t e[2048];
        size_t n = 0;
        uint16_t d[] = {200, 350, 180, 520, 260, 300, 240, 600, 190, 410};
        for (int i = 0; i < 40; i++) push(e, &n, d[i % 10], i % 2); /* alterne H/L, rien dans 3000..8500 */
        int nb = run(e, n, out, NULL);
        check(nb == -4, "T3 bruit : pas d'en-tete -> -4", "a cru decoder du bruit");
    }

    /* T4 : vote majoritaire. Trois repetitions, dont une avec UN bit faux a une
     * position donnee -> le vote 2 contre 1 doit rendre la trame correcte. */
    {
        static edge_t e[4096];
        size_t n = 0;
        char corrompu[67];
        strcpy(corrompu, EXPECTED);
        corrompu[30] = (corrompu[30] == '1') ? '0' : '1'; /* un bit faux dans la 2e repet */

        push_header(e, &n);
        push_bits(e, &n, EXPECTED, true, -1);
        push_header(e, &n);
        push_bits(e, &n, corrompu, true, -1);
        push_header(e, &n);
        push_bits(e, &n, EXPECTED, true, -1);

        int nb = run(e, n, out, NULL);
        check(nb == 66, "T4 vote : 66 bits", "nombre de bits");
        check(strcmp(out, EXPECTED) == 0, "T4 vote : le bit faux est corrige", out);
    }

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
