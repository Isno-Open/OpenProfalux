#include "rf_capture.h"
#include "radio.h"
#include "cc1101.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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

/* Silence inter-trame ajoute entre deux repetitions au rejeu. Une capture ne prend qu'UNE trame
 * (le creux final n'est pas capte), et sans silence les repetitions arrivent collees : le
 * recepteur ne delimite plus les trames (constate sur un portail CAME chez un testeur). Valeur
 * generique qui couvre le CAME (silence nominal ~11,5 ms) et reste tolerable pour les autres
 * telecommandes OOK a code fixe. A affiner au banc si une marque exige un creux different. */
#define RFCAP_GAP_US 12000

/* Derniere capture (une seule a la fois ; sous le meme usage manuel que l'enrolement). */
static uint8_t  s_lvl[RFCAP_MAX_PULSES];
static uint16_t s_us[RFCAP_MAX_PULSES];
static int      s_n = 0;
static int      s_radio = 0;
static int      s_band_mhz = 0;
static uint32_t s_freq_khz = 0;
static int8_t   s_rssi = 0;
static int64_t  s_t_us = 0;

/* Lot de trames collecte par rfcap_capture_multi : persiste entre la capture et l'envoi du rapport
 * (deux requetes HTTP distinctes). Le texte JSON des trames est accumule a la volee. */
#define RFCAP_FRAMES_JSON_MAX 20480
static char     s_frames_json[RFCAP_FRAMES_JSON_MAX];
static int      s_frames_len = 0;
static int      s_frames_count = 0;
/* Derniere trame AJOUTEE au lot : sert a dedoublonner la suivante (code fixe presse plusieurs fois)
 * et de trame `pulses`/`n` de compatibilite dans le JSON du rapport. */
static uint8_t  s_prev_lvl[RFCAP_MAX_PULSES];
static uint16_t s_prev_us[RFCAP_MAX_PULSES];
static int      s_prev_n = 0;

int rfcap_capture(int radio, uint32_t timeout_ms) {
    s_frames_count = 0;   /* une capture SIMPLE invalide un lot multi precedent : le rapport prendra celle-ci */
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
    /* On rejoue la trame SUIVIE d'un silence inter-trame (porteuse OFF) : sans lui les repetitions
     * sont collees et le recepteur ne delimite plus les trames. radio_tx_ook_pulses prend les
     * durees en uint32_t ; on recopie depuis le stockage uint16_t et on ajoute le creux final. */
    static uint8_t  lvl[RFCAP_MAX_PULSES + 1];
    static uint32_t us[RFCAP_MAX_PULSES + 1];
    int n = 0;
    for (int i = 0; i < s_n; i++) { lvl[n] = s_lvl[i]; us[n] = s_us[i]; n++; }
    lvl[n] = 0; us[n] = RFCAP_GAP_US; n++;   /* silence inter-trame */
    int r = 0;
    for (int k = 0; k < repeats && r == 0; k++)
        r = radio_tx_ook_pulses(s_freq_khz, lvl, us, n);
    ESP_LOGW(TAG, "rejeu %d impulsions +gap %u us x%d a %u kHz : rc=%d",
             s_n, (unsigned)RFCAP_GAP_US, repeats, (unsigned)s_freq_khz, r);
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

/* La capture courante (s_lvl/s_us/s_n) est-elle identique a la derniere AJOUTEE au lot ? */
static bool rfcap_same_as_prev(void) {
    if (s_n <= 0 || s_n != s_prev_n) return false;
    for (int i = 0; i < s_n; i++)
        if (s_lvl[i] != s_prev_lvl[i] || s_us[i] != s_prev_us[i]) return false;
    return true;
}

/* Ajoute la capture courante au lot JSON s_frames_json. Rend false si pas la place (lot clos tel
 * quel), de sorte qu'on n'ecrive JAMAIS une trame partielle (JSON toujours valide). */
static bool rfcap_frames_append(void) {
    int need = s_n * 10 + 48;   /* taille pessimiste : "[l,uuuuu]," ~ 10 car. par impulsion + enveloppe */
    if (s_frames_len + need > RFCAP_FRAMES_JSON_MAX - 64) return false;
    int off = s_frames_len;
    off += snprintf(s_frames_json + off, RFCAP_FRAMES_JSON_MAX - off,
                    "%s{\"rssi\":%d,\"pulses\":[", s_frames_count ? "," : "", s_rssi);
    for (int i = 0; i < s_n; i++)
        off += snprintf(s_frames_json + off, RFCAP_FRAMES_JSON_MAX - off,
                        "%s[%d,%u]", i ? "," : "", s_lvl[i], (unsigned)s_us[i]);
    off += snprintf(s_frames_json + off, RFCAP_FRAMES_JSON_MAX - off, "]}");
    s_frames_len = off;
    s_frames_count++;
    memcpy(s_prev_lvl, s_lvl, (size_t)s_n);
    memcpy(s_prev_us, s_us, (size_t)s_n * sizeof(uint16_t));
    s_prev_n = s_n;
    return true;
}

int rfcap_capture_multi(int radio, uint32_t window_ms) {
    const char *band = NULL; uint32_t carrier = 0, cur = 0, low = 0, high = 0; uint8_t pn = 0, ver = 0;
    cc1101_radio_info(radio, &band, &carrier, &cur, &low, &high, &pn, &ver);   /* meta de la radio (band/porteuse) */
    s_frames_len = 0; s_frames_count = 0; s_prev_n = 0;
    int64_t t_end = esp_timer_get_time() + (int64_t)window_ms * 1000;
    while (esp_timer_get_time() < t_end && s_frames_count < RFCAP_MAX_FRAMES) {
        int64_t remain_us = t_end - esp_timer_get_time();
        if (remain_us < 300 * 1000) break;   /* trop peu de temps restant pour une pression utile */
        int8_t rssi = 0;
        int n = radio_capture_raw_on(radio, (uint32_t)(remain_us / 1000), s_lvl, s_us, RFCAP_MAX_PULSES, &rssi);
        if (n <= 0) continue;                 /* rien capte dans ce qui restait de la fenetre */
        s_n = n; s_radio = radio;
        s_freq_khz = cur ? cur : carrier;
        s_band_mhz = band ? atoi(band) : 0;
        s_rssi = rssi; s_t_us = esp_timer_get_time();
        if (rfcap_same_as_prev()) continue;   /* meme trame qu'a l'instant (code fixe re-presse) : on ignore */
        if (!rfcap_frames_append()) break;    /* lot plein : on s'arrete proprement */
    }
    ESP_LOGW(TAG, "capture multi radio %d : %d trame(s) distincte(s) sur %u ms",
             radio, s_frames_count, (unsigned)window_ms);
    return s_frames_count;
}

int rfcap_frames_json(char *out, int max) {
    if (!out || max < 32) return 0;
    if (s_frames_count <= 0) return rfcap_last_json(out, max);   /* repli : capture simple, ou {"n":0} */
    int off = snprintf(out, max, "{\"radio\":%d,\"band\":\"%d\",\"freq_khz\":%u,\"nframes\":%d,\"frames\":[",
                       s_radio, s_band_mhz, (unsigned)s_freq_khz, s_frames_count);
    int copy = s_frames_len;
    if (copy > max - off - 96) copy = max - off - 96;   /* garde-fou : laisse la place a la fin + compat */
    if (copy > 0) { memcpy(out + off, s_frames_json, (size_t)copy); off += copy; }
    off += snprintf(out + off, max - off, "]");
    /* Compat ancien serveur : `pulses` + `n` = la DERNIERE trame (decodee au minimum si le serveur
     * ne lit pas `frames`). */
    off += snprintf(out + off, max - off, ",\"n\":%d,\"rssi\":%d,\"pulses\":[", s_prev_n, s_rssi);
    for (int i = 0; i < s_prev_n && off < max - 8; i++)
        off += snprintf(out + off, max - off, "%s[%d,%u]", i ? "," : "", s_prev_lvl[i], (unsigned)s_prev_us[i]);
    off += snprintf(out + off, max - off, "]}");
    return off;
}
