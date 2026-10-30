/* Banc de test de freq_sweep (porteuse par balayage RSSI), sur PC. Voir README.md.
 * Rend 0 si tout passe, 1 sinon. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "freq_sweep.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

/* Reponse d'un filtre RX de ~58 kHz a une porteuse fc : sommet plat sur +-20 kHz, flancs
 * lineaires jusqu'au bruit a +-45 kHz. RSSI en demi-dB, bruit pseudo-aleatoire +-amp. */
static int16_t rssi_at(uint32_t f, uint32_t fc, int peak, int floor_, int amp, unsigned *seed) {
    int d = abs((int)f - (int)fc);
    int v = d <= 20 ? peak : d >= 45 ? floor_ : peak - (peak - floor_) * (d - 20) / 25;
    *seed = *seed * 1103515245u + 12345u;
    if (amp) v += (int)((*seed >> 16) % (unsigned)(2 * amp + 1)) - amp;
    return (int16_t)v;
}

/* Balayage center+-span par pas de step ; rend l'ecart (kHz) entre porteuse trouvee et fc. */
static fsweep_status_t sweep(uint32_t center, int span, int step, uint32_t fc, int peak, int floor_,
                             int amp, fsweep_result_t *r) {
    uint32_t khz[128]; int16_t v[128]; int n = 0; unsigned seed = 7;
    for (int o = -span; o <= span && n < 128; o += step, n++) {
        khz[n] = center + o;
        v[n] = rssi_at(khz[n], fc, peak, floor_, amp, &seed);
    }
    return fsweep_analyze(khz, v, n, r);
}

int main(void) {
    fsweep_result_t r;
    printf("Banc freq_sweep\n");

    /* T0 : porteuse a 868318 (12 kHz sous le profil Eveno), signal fort, sans bruit. */
    check(sweep(868330, 150, 10, 868318, -90, -210, 0, &r) == FSWEEP_OK, "T0 porteuse trouvee");
    check(abs((int)r.center_khz - 868318) <= 2, "T0 a +-2 kHz");

    /* T1 : meme chose avec du bruit de mesure (+-1,5 dB). */
    check(sweep(868330, 150, 10, 868318, -90, -210, 3, &r) == FSWEEP_OK, "T1 bruit : trouvee");
    check(abs((int)r.center_khz - 868318) <= 4, "T1 bruit : a +-4 kHz");

    /* T2 : porteuse decalee de +73 kHz (pas d'un pas entier). */
    check(sweep(868330, 150, 10, 868403, -100, -210, 2, &r) == FSWEEP_OK, "T2 +73 kHz : trouvee");
    check(abs((int)r.center_khz - 868403) <= 4, "T2 +73 kHz : a +-4 kHz");

    /* T3 : signal a peine au-dessus du bruit (6 dB) : refuse. */
    check(sweep(868330, 150, 10, 868318, -198, -210, 0, &r) == FSWEEP_WEAK, "T3 signal faible : refuse");

    /* T4 : porteuse hors du balayage (plateau au bord) : refuse, elargir. */
    check(sweep(868330, 150, 10, 868470, -90, -210, 0, &r) == FSWEEP_AT_EDGE, "T4 porteuse au bord : refuse");

    /* T6, T7 : deux balayages REELS successifs (module de test d1-mini, telecommande Eveno sur P,
     * profil 868330), plateau qui fluctue de 12 dB. Ils doivent donner la meme porteuse. */
    {
        static const int16_t r1[31] = { -192,-192,-192,-192,-192,-192,-192,-192,-192,-192,-192,-192,
            -179,-179,-179,-179,-175,-173,-169,-167,-143,-120,-127,-123,-136,-130,-175,-192,-192,-192,-192 };
        static const int16_t r2[31] = { -192,-192,-192,-192,-192,-192,-192,-192,-192,-179,-179,-175,
            -175,-173,-179,-173,-173,-161,-161,-157,-114,-107,-127,-127,-129,-131,-169,-173,-179,-192,-192 };
        uint32_t kk[31]; for (int i = 0; i < 31; i++) kk[i] = 868180 + 10 * i;
        fsweep_result_t a1, a2;
        check(fsweep_analyze(kk, r1, 31, &a1) == FSWEEP_OK && fsweep_analyze(kk, r2, 31, &a2) == FSWEEP_OK,
              "T6 courbes reelles : porteuse trouvee");
        printf("       -> %u et %u kHz\n", (unsigned)a1.center_khz, (unsigned)a2.center_khz);
        check(abs((int)a1.center_khz - (int)a2.center_khz) <= 3, "T7 courbes reelles : meme porteuse a +-3 kHz");
    }

    /* T5 : trop peu de pas. */
    uint32_t k[3] = { 1, 2, 3 }; int16_t v[3] = { 0, 0, 0 };
    check(fsweep_analyze(k, v, 3, &r) == FSWEEP_TOO_FEW, "T5 trop peu de pas : refuse");

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
