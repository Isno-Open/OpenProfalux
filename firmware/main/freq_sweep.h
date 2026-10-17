#pragma once
/* freq_sweep — porteuse d'une telecommande trouvee par balayage RSSI (issue #51).
 *
 * Le CC1101 n'estime pas l'ecart de frequence en OOK (FREQEST reste a 0, mesure sur #51) :
 * on balaie la frequence de reception pendant une emission longue de la telecommande
 * (commutateur P seul : trames continues, bouton 0x1 ignore du moteur) en relevant le RSSI
 * max a chaque pas. Le filtre RX donne un sommet PLAT, et le RSSI max d'une emission OOK y
 * fluctue de pas en pas (jusqu'a 12 dB mesures) : ni le maximum, ni un seuil sous le pic ne
 * sont fiables. On prend le milieu des bords du plateau, au seuil A MI-HAUTEUR (en dB) entre
 * le bruit et le pic, la ou les flancs du filtre sont raides (~20 dB par pas de 10 kHz).
 * Deux balayages reels successifs donnent alors la meme porteuse a 2 kHz pres.
 *
 * La frequence trouvee est celle de la telecommande VUE PAR CE CC1101 : emettre a cette
 * frequence aligne l'emission sur la porteuse reelle, erreur du quartz du module comprise
 * (reception et emission passent par le meme synthetiseur).
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/freq-sweep. */
#include <stdint.h>

#define FSWEEP_MIN_SNR_HDB  20   /* pic au moins 10 dB au-dessus du bruit, sinon pas de signal */

typedef enum {
    FSWEEP_OK        =  0,
    FSWEEP_TOO_FEW   = -1,   /* moins de 5 pas */
    FSWEEP_WEAK      = -2,   /* pic trop pres du bruit : telecommande absente ou trop loin */
    FSWEEP_AT_EDGE   = -3,   /* plateau qui touche un bord du balayage : elargir la plage */
} fsweep_status_t;

typedef struct {
    uint32_t center_khz;   /* milieu des bords au seuil mi-hauteur (si FSWEEP_OK) */
    int16_t  peak_hdb;     /* RSSI max, en demi-dB (dBm = valeur / 2) */
    int16_t  floor_hdb;    /* bruit : 1er quartile des RSSI releves */
    uint32_t lo_khz, hi_khz;   /* bords interpoles du plateau, au seuil mi-hauteur */
} fsweep_result_t;

/* khz[] croissant, rssi_hdb[] = RSSI max releve a chaque pas (demi-dB). n pas. */
fsweep_status_t fsweep_analyze(const uint32_t *khz, const int16_t *rssi_hdb, int n, fsweep_result_t *r);
