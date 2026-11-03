/* came_tx : voir came_tx.h. Encodeur CAME TOP-432 propre (E6), separe du rejeu brut. */
#include "came_tx.h"
#include "radio.h"
#include "esp_log.h"

static const char *TAG = "came_tx";

/* Une cellule = (niveau, duree). Ajoute une paire haut/bas a la liste si la place le permet. */
static int push(uint8_t *lv, uint32_t *us, int n, int max, uint32_t hi, uint32_t lo) {
    if (n + 2 > max) return -1;
    lv[n] = 1; us[n] = hi; n++;
    lv[n] = 0; us[n] = lo; n++;
    return n;
}

int came_tx_build_frame(uint16_t code12, uint8_t *levels, uint32_t *us, int max) {
    if (!levels || !us) return 0;
    int n = 0;
    /* Cellule de start : impulsion courte (le decodeur la compte comme 13e cellule, extrait @1:{12}). */
    n = push(levels, us, n, max, CAME_TX_SHORT_US, CAME_TX_SHORT_US);
    if (n < 0) return 0;
    /* 12 bits, MSB d'abord. Mapping cale par round-trip rtl_433 (voir came_tx.h) :
     * bit 0 -> longue haute + creux court ; bit 1 -> courte haute + creux long. */
    for (int i = CAME_TX_CODE_BITS - 1; i >= 0; i--) {
        const int b = (code12 >> i) & 1;
        n = b ? push(levels, us, n, max, CAME_TX_SHORT_US, CAME_TX_LONG_US)
              : push(levels, us, n, max, CAME_TX_LONG_US,  CAME_TX_SHORT_US);
        if (n < 0) return 0;
    }
    /* Silence inter-trame : on rallonge le dernier creux (porteuse coupee). */
    if (n >= 1) us[n - 1] = CAME_TX_GAP_US;
    return n;
}

int came_tx_emit(uint16_t code12, int repeats) {
    uint8_t  lv[2 * (1 + CAME_TX_CODE_BITS)];
    uint32_t us[2 * (1 + CAME_TX_CODE_BITS)];
    int n = came_tx_build_frame(code12, lv, us, (int)(sizeof lv / sizeof lv[0]));
    if (n < 2) return -1;
    if (repeats < 1) repeats = 1;
    int r = 0;
    for (int k = 0; k < repeats && r == 0; k++)
        r = radio_tx_ook_pulses(CAME_TX_FREQ_KHZ, lv, us, n);
    ESP_LOGW(TAG, "CAME TX code=0x%03X : %d cellules, %d trames a %u kHz, rc=%d",
             code12 & 0xFFF, n, repeats, (unsigned)CAME_TX_FREQ_KHZ, r);
    return r;
}
