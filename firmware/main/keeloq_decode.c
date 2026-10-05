/* === Decodage OOK HCS30x/KeeLoq, pipeline tolerant au bruit (inspire Flipper keeloq.c +
 * rtl_433 pulse_slicer_pwm). HCS301 : Te~430us, bit=3Te, '0'=2Te haut +1Te bas,
 * '1'=1Te haut +2Te bas ; preambule 23 Te (50% duty), entete TH~10 Te (LOW long),
 * 66 bits, garde ~39 Te, trame repetee ~10x. Pas de CRC : on valide par serial+bouton.
 * Unite PURE (testable sur hote) : cf keeloq_decode.h et firmware/test/keeloq. === */
#include "keeloq_decode.h"
#include <string.h>
#include <stdbool.h>

#define GLITCH_US 140          /* slivers < ~Te/3 = bruit de demod OOK -> jetes */
#define TE_NOM    430          /* temps elementaire mesure (us) */
#define VOTE_MINBITS 16        /* repetitions >=16 bits incluses dans le vote (prefixes alignes) */
#define VOTE_MAXW   32

/* Liste de fronts (niveau,duree) de-glitchee, partagee par le decodeur. */
static uint16_t s_ed_dur[2 * (KEELOQ_CAP_SYMBOLS + 2)];
static uint8_t  s_ed_lvl[2 * (KEELOQ_CAP_SYMBOLS + 2)];

/* Aplatit les symboles RMT en fronts, JETTE les slivers de bruit (<GLITCH_US) et FUSIONNE
 * les fronts de meme niveau qui deviennent adjacents (recolle une impulsion coupee par un
 * glitch). Rend le nombre de fronts. Les fronts resultants alternent strictement H/L. */
static size_t deglitch_edges(const kq_sym_t *in, size_t n) {
    size_t w = 0; const size_t EMAX = sizeof(s_ed_lvl) / sizeof(s_ed_lvl[0]);
    for (size_t i = 0; i < n; i++) {
        uint32_t d[2] = { in[i].duration0, in[i].duration1 };
        uint8_t  l[2] = { in[i].level0,    in[i].level1 };
        for (int k = 0; k < 2; k++) {
            if (d[k] == 0) return w;                 /* duree 0 = marqueur EOF du driver */
            if (d[k] < GLITCH_US) continue;          /* sliver de bruit : on jette */
            if (w > 0 && s_ed_lvl[w - 1] == l[k]) {  /* meme niveau que le precedent garde : fusion */
                uint32_t s = (uint32_t)s_ed_dur[w - 1] + d[k];
                s_ed_dur[w - 1] = s > 65535 ? 65535 : (uint16_t)s;
            } else if (w < EMAX) {
                s_ed_dur[w] = d[k] > 65535 ? 65535 : (uint16_t)d[k]; s_ed_lvl[w] = l[k]; w++;
            }
        }
    }
    return w;
}

/* Decode UNE repetition a partir d'une entete (LOW long a l'index hdr). Apparie HIGH/LOW,
 * classe le bit par la duree du HAUT (court ~1Te = '1', long ~2Te = '0'). Une impulsion
 * hors gabarit ARRETE cette repetition (le scan des entetes suivantes re-synchronise,
 * on n'abandonne jamais la capture). Rend nb bits ; *out_te = moyenne des HAUT courts. */
static int decode_word(size_t hdr, size_t w, char *out, int *out_te) {
    int nb = 0; uint32_t sum = 0; int cnt = 0;
    for (size_t i = hdr + 1; i < w && nb < 66; i += 2) {
        /* 66e bit : son BAS se confond avec la garde qui suit la trame. Quand ce silence
         * termine la reception (toujours sur ESP32, seuil idle 8 ms), la capture finit sur
         * ce HAUT sans BAS apres : on le garde, le bit se lisant sur le HAUT seul. Une
         * repetition coupee plus tot (last && nb!=65) s'arrete comme avant. (fix @Akkeoss) */
        bool last = i + 1 >= w;
        if (last && nb != 65) break;
        if (s_ed_lvl[i] != 1 || (!last && s_ed_lvl[i + 1] != 0)) break;   /* alternance HIGH/LOW attendue */
        uint32_t hi = s_ed_dur[i], lo = last ? 0 : s_ed_dur[i + 1];
        if (hi < 190 || hi > 1200) break;                      /* HAUT hors gabarit */
        if (hi < 680) { out[nb++] = '1'; sum += hi; cnt++; } else out[nb++] = '0';
        if (lo > 1600) break;                                  /* BAS trop long = garde/entete suivante */
    }
    out[nb] = 0;
    if (out_te && cnt) *out_te = (int)(sum / cnt);
    return nb;
}

/* 1) de-glitch ; 2) scanne TOUTES les entetes (LOW long ~10 Te) et decode chaque repetition
 * sans jamais abandonner ; 3) VOTE MAJORITAIRE bit a bit sur les repetitions >=16 bits (un
 * glitch tombe a des positions differentes selon la repet, le vote le corrige). */
int keeloq_decode(const kq_sym_t *raw, size_t raw_n, char *out, int max_bits, int *out_te) {
    (void)max_bits;
    size_t w = deglitch_edges(raw, raw_n);
    static char words[VOTE_MAXW][67];
    int nwords = 0, best = -4, te_any = 0;
    char tmp[80], bestpart[80]; bestpart[0] = 0;
    for (size_t i = 0; i < w; i++) {
        /* On ancre UNIQUEMENT sur le vrai en-tete HCS (~10 Te = 4,5-6,5 ms). Les gardes
         * et dropouts (>9 ms) sont suivis du PREAMBULE, pas du bit 0 : s'y ancrer
         * decalerait les fragments et polluerait le vote. On les exclut donc. */
        if (!(s_ed_lvl[i] == 0 && s_ed_dur[i] > 3000 && s_ed_dur[i] < 8500)) continue;
        int te = 0;
        int nb = decode_word(i, w, tmp, &te);
        if (nb > best) { best = nb; memcpy(bestpart, tmp, (size_t)nb + 1); }
        if (nb >= VOTE_MINBITS && nwords < VOTE_MAXW) {
            memcpy(words[nwords], tmp, (size_t)nb + 1); nwords++;
            if (te) te_any = te;
        }
    }
    if (nwords > 0) {                                  /* vote majoritaire bit a bit */
        int L = 0;
        for (int p = 0; p < 66; p++) {
            int ones = 0, tot = 0;
            for (int k = 0; k < nwords; k++)
                if ((int)strlen(words[k]) > p) { tot++; if (words[k][p] == '1') ones++; }
            if (tot == 0) break;                       /* plus aucune repetition ne couvre cette position */
            out[p] = (ones * 2 >= tot) ? '1' : '0';
            L = p + 1;
        }
        out[L] = 0;
        if (out_te && te_any) *out_te = te_any;
        if (L >= 64) return L;                         /* trame votee complete */
    }
    if (best < 0) { out[0] = 0; return -4; }
    memcpy(out, bestpart, (size_t)best + 1);          /* pas de trame complete : meilleure partielle */
    return best;
}
