#include "came_top432.h"
#include "radio.h"
#include "esp_log.h"

static const char *TAG = "came";

/* Timing CAME TOP-432 (OOK PWM, TE = 320 us), d'apres le decodeur rtl_433 CAME-TOP432 et la
 * capture RTL-SDR de la telecommande du testeur : impulsion courte = 320 us (1 TE), longue =
 * 640 us (2 TE), separateur inter-trame ~36 TE. Chaque bit = un creux puis une impulsion :
 *   bit 0 = creux 320 + impulsion 640 ; bit 1 = creux 640 + impulsion 320.
 * Une trame = start (impulsion haute 1 TE) + 12 bits (MSB d'abord) + gap inter-trame (creux).
 *
 * A VALIDER avant toute livraison a un testeur : la polarite exacte (quelle largeur vaut 1) et
 * l'ordre creux/impulsion. Validation sure, hors banc : regenerer cette trame et la passer a
 * rtl_433 -r ; elle doit redonner "CAME-TOP432" avec le meme code. En cas d'inversion, echanger
 * SHORT_US/LONG_US ou l'ordre creux/impulsion ci-dessous : tout est en constantes nommees. */
#define TE_US          320
#define SHORT_US       TE_US         /* 320 us, 1 TE */
#define LONG_US        (2 * TE_US)   /* 640 us, 2 TE */
#define START_HIGH_US  TE_US         /* impulsion de depart */
#define GAP_US         (36 * TE_US)  /* 11520 us, separateur inter-trame */

int came_top432_build(uint16_t code12, uint8_t *levels, uint32_t *us, int max) {
    if (!levels || !us || max < CAME_TOP432_FRAME_PULSES) return -1;
    int k = 0;
    levels[k] = 1; us[k] = START_HIGH_US; k++;                 /* start : impulsion haute */
    for (int b = CAME_TOP432_CODE_BITS - 1; b >= 0; b--) {     /* MSB d'abord */
        int bit = (code12 >> b) & 1;
        levels[k] = 0; us[k] = bit ? LONG_US : SHORT_US; k++;  /* creux */
        levels[k] = 1; us[k] = bit ? SHORT_US : LONG_US; k++;  /* impulsion */
    }
    levels[k] = 0; us[k] = GAP_US; k++;                        /* gap inter-trame */
    return k;
}

int came_top432_tx(uint16_t code12, int repeats) {
    if (repeats < 1) repeats = 1;
    uint8_t  lvl[CAME_TOP432_FRAME_PULSES];
    uint32_t us[CAME_TOP432_FRAME_PULSES];
    int n = came_top432_build(code12, lvl, us, CAME_TOP432_FRAME_PULSES);
    if (n < 0) return -1;
    int r = 0;
    for (int i = 0; i < repeats && r == 0; i++)
        r = radio_tx_ook_pulses(CAME_TOP432_FREQ_KHZ, lvl, us, n);
    ESP_LOGW(TAG, "TX CAME TOP-432 code=0x%03X x%d : rc=%d", code12 & 0x0FFF, repeats, r);
    return r;
}
