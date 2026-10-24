/* somfy_tx : emission radio Somfy RTS (voir somfy_tx.h). */
#include "somfy_tx.h"
#include "radio.h"
#include "pfx_enrol.h"   /* PFX_BTN_UP / PFX_BTN_DOWN / PFX_BTN_STOP */
#include "esp_log.h"

static const char *TAG = "somfy_tx";

/* Train d'impulsions complet d'une rafale : reveil (2) + (1 + repeats) trames, chacune
 * precedee du silence inter-trame (sauf la 1re). Bati en RAM STATIQUE : un seul emetteur
 * a la fois (la tache volet serialise les commandes), et ca evite de charger la pile
 * d'une tache ESP de plusieurs kilo-octets. */
#define SOMFY_TX_MAX_FRAMES   (1 + SOMFY_RTS_TX_REPEATS_MAX)
/* Dimensionne sur la trame la PLUS LONGUE (80 bits) : emit et emit80 partagent le buffer. */
#define SOMFY_TX_MAX_PULSES   (2 + SOMFY_TX_MAX_FRAMES * SOMFY_RTS_MAX_PULSES_80 + (SOMFY_TX_MAX_FRAMES - 1))

static uint8_t  s_level[SOMFY_TX_MAX_PULSES];
static uint32_t s_us[SOMFY_TX_MAX_PULSES];

static int push(int n, uint8_t level, uint32_t us) {
    if (n < 0 || n >= SOMFY_TX_MAX_PULSES) return -1;
    s_level[n] = level;
    s_us[n] = us;
    return n + 1;
}

somfy_rts_cmd_t somfy_tx_cmd_from_button(uint8_t pfx_btn) {
    switch (pfx_btn) {
    case PFX_BTN_UP:   return SOMFY_RTS_CMD_UP;
    case PFX_BTN_DOWN: return SOMFY_RTS_CMD_DOWN;
    default:           return SOMFY_RTS_CMD_MY;   /* STOP Profalux -> My (stop Somfy) */
    }
}

int somfy_tx_emit(const uint8_t frame[SOMFY_RTS_FRAME_BYTES], int repeats) {
    if (!frame) return -1;
    if (repeats < 0) repeats = 0;
    if (repeats > SOMFY_RTS_TX_REPEATS_MAX) repeats = SOMFY_RTS_TX_REPEATS_MAX;

    int n = 0;
    /* Reveil : une impulsion haute puis basse, sans long silence (comme ESPSomfy). */
    n = push(n, 1, SOMFY_RTS_WAKEUP_HIGH_US);
    n = push(n, 0, SOMFY_RTS_WAKEUP_LOW_US);

    somfy_rts_pulse_t p[SOMFY_RTS_MAX_PULSES];
    for (int f = 0; f <= repeats; f++) {
        if (f > 0 && (n = push(n, 0, SOMFY_RTS_INTER_FRAME_US)) < 0) return -1;
        int np = somfy_rts_tx_pulses(frame, f == 0, p, SOMFY_RTS_MAX_PULSES);
        if (np < 0) return -1;
        for (int i = 0; i < np; i++)
            if ((n = push(n, p[i].level, p[i].us)) < 0) return -1;
    }

    int r = radio_tx_ook_pulses(SOMFY_RTS_FREQ_KHZ, s_level, s_us, n);
    ESP_LOGW(TAG, "TX Somfy : %d trame(s), %d impulsions, %u kHz, r=%d",
             repeats + 1, n, (unsigned)SOMFY_RTS_FREQ_KHZ, r);
    return r;
}

int somfy_tx_emit80(const uint8_t base56[SOMFY_RTS_FRAME_BYTES], somfy_rts_cmd_t cmd, int repeats) {
    if (!base56) return -1;
    if (repeats < 0) repeats = 0;
    if (repeats > SOMFY_RTS_TX_REPEATS_MAX) repeats = SOMFY_RTS_TX_REPEATS_MAX;

    int n = 0;
    /* Reveil : une impulsion haute puis basse, sans long silence (comme ESPSomfy). */
    n = push(n, 1, SOMFY_RTS_WAKEUP_HIGH_US);
    n = push(n, 0, SOMFY_RTS_WAKEUP_LOW_US);

    somfy_rts_pulse_t p[SOMFY_RTS_MAX_PULSES_80];
    for (int f = 0; f <= repeats; f++) {
        if (f > 0 && (n = push(n, 0, SOMFY_RTS_INTER_FRAME_US)) < 0) return -1;
        uint8_t frame80[SOMFY_RTS_FRAME_BYTES_80];
        somfy_rts_frame80(base56, cmd, (uint8_t)f, frame80);   /* octet 7 depend de f */
        int np = somfy_rts_tx_pulses80(frame80, f == 0, p, SOMFY_RTS_MAX_PULSES_80);
        if (np < 0) return -1;
        for (int i = 0; i < np; i++)
            if ((n = push(n, p[i].level, p[i].us)) < 0) return -1;
    }
    int r = radio_tx_ook_pulses(SOMFY_RTS_FREQ_KHZ, s_level, s_us, n);
    ESP_LOGW(TAG, "TX Somfy 80 bits : cmd=0x%X, %d trame(s), %d impulsions, r=%d",
             (unsigned)cmd, repeats + 1, n, r);
    return r;
}
