#include "rf_capture.h"
#include "radio.h"
#include "cc1101.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "esp_log.h"

/* Journal commun (owne par la session soeur, main/capture_journal.{h,c}) : s'auto-cable des que
 * le header est present sur la branche d'integration. Tant qu'il n'y est pas, ce module compile et
 * tourne en standalone (capture + rejeu + export UI), sans ecrire le journal. */
#if defined(__has_include) && __has_include("capture_journal.h")
#include "capture_journal.h"
#define RFCAP_JOURNAL 1
#endif

static const char *TAG = "rfcap";

/* Derniere capture (une seule a la fois ; sous le meme usage manuel que l'enrolement). */
static uint8_t  s_lvl[RFCAP_MAX_PULSES];
static uint16_t s_us[RFCAP_MAX_PULSES];
static int      s_n = 0;
static int      s_radio = 0;
static int      s_band_mhz = 0;
static uint32_t s_freq_khz = 0;
static int8_t   s_rssi = 0;
static int64_t  s_t_us = 0;

int rfcap_capture(int radio, uint32_t timeout_ms) {
    const char *band = NULL; uint32_t carrier = 0, cur = 0, low = 0, high = 0; uint8_t pn = 0, ver = 0;
    cc1101_radio_info(radio, &band, &carrier, &cur, &low, &high, &pn, &ver);   /* remplit band/cur ; l'idx est valide par radio_capture_raw_on */
    int8_t rssi = 0;
    int n = radio_capture_raw_on(radio, timeout_ms, s_lvl, s_us, RFCAP_MAX_PULSES, &rssi);
    if (n <= 0) { ESP_LOGW(TAG, "capture radio %d : rien (rc=%d)", radio, n); return n; }
    s_n = n; s_radio = radio;
    s_freq_khz = cur ? cur : carrier;        /* porteuse courante de la radio (pour rejouer juste) */
    s_band_mhz = band ? atoi(band) : 0;
    s_rssi = rssi;
    s_t_us = esp_timer_get_time();
    ESP_LOGW(TAG, "capture radio %d bande %s : %d impulsions a %u kHz, RSSI %d dBm",
             radio, band ? band : "?", n, (unsigned)s_freq_khz, rssi);
#ifdef RFCAP_JOURNAL
    /* Entree du journal commun : type raw-ook, data = impulsions [[lvl,us]]. L'enveloppe du rapport
     * (carte, marque/modele, note, email, issue) est geree par la couche assistance en aval. */
    capture_journal_mark("capture OOK brute");
    capture_journal_add_rawook((uint8_t)s_radio, (uint16_t)s_band_mhz, s_freq_khz, s_rssi,
                               s_lvl, s_us, (uint16_t)s_n);
    capture_journal_persist();
#endif
    return n;
}

int rfcap_replay(int repeats) {
    if (s_n <= 0) return -1;
    if (repeats < 1) repeats = 1;
    /* radio_tx_ook_pulses prend les durees en uint32_t ; on convertit depuis le stockage uint16_t. */
    static uint32_t us32[RFCAP_MAX_PULSES];
    for (int i = 0; i < s_n; i++) us32[i] = s_us[i];
    int r = 0;
    for (int k = 0; k < repeats && r == 0; k++)
        r = radio_tx_ook_pulses(s_freq_khz, s_lvl, us32, s_n);
    ESP_LOGW(TAG, "rejeu %d impulsions x%d a %u kHz : rc=%d", s_n, repeats, (unsigned)s_freq_khz, r);
    return r;
}

int rfcap_last_json(char *out, int max) {
    if (!out || max < 16) return 0;
    if (s_n <= 0) return snprintf(out, max, "{\"n\":0}");
    uint32_t age_ms = (uint32_t)((esp_timer_get_time() - s_t_us) / 1000);
    int off = snprintf(out, max, "{\"n\":%d,\"radio\":%d,\"band\":\"%d\",\"freq_khz\":%u,\"rssi\":%d,\"age_ms\":%u,\"pulses\":[",
                       s_n, s_radio, s_band_mhz, (unsigned)s_freq_khz, s_rssi, (unsigned)age_ms);
    for (int i = 0; i < s_n && off < max - 24; i++)
        off += snprintf(out + off, max - off, "%s[%d,%u]", i ? "," : "", s_lvl[i], (unsigned)s_us[i]);
    off += snprintf(out + off, max - off, "]}");
    return off;
}
