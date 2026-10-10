/*
 * radio.c — arbitrage RX/TX de l'unique CC1101 (voir radio.h).
 */
#include "radio.h"
#include "cc1101.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "radio";

#define RX_WINDOW_MS 250   /* fenetre d'ecoute : borne la latence max d'un TX a ~250 ms */

static SemaphoreHandle_t s_mtx = NULL;   /* proprietaire exclusif du radio (GDO0 + RMT) */
static radio_frame_cb_t  s_cb  = NULL;
static volatile bool     s_run     = false;
static volatile bool     s_paused  = false;  /* pause temporaire (pendant nos TX stream) */
static volatile bool     s_listen  = false;  /* interrupteur maitre (case UI "capture toutes les trames") */

/* Lecture LSB-first d'un champ de la trame (ordre du fil). */
static uint32_t lsb(const char *b, int from, int len) {
    uint32_t v = 0;
    for (int i = 0; i < len; i++) if (b[from + i] == '1') v |= (1u << i);
    return v;
}

void radio_init(void) {
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    /* Coupe le spam "RX(GDO0): MARCSTATE=..." emis a chaque fenetre d'ecoute. */
    esp_log_level_set("cc1101", ESP_LOG_WARN);
    /* Coupe le spam "hw buffer too small" : le bruit OOK sature normalement le buffer
     * RMT de l'ecoute permanente ; c'est attendu et sans consequence (on se re-arme). */
    esp_log_level_set("rmt", ESP_LOG_NONE);
}

void radio_tx(const char *bits, int repeats) {
    if (!bits || !bits[0] || !s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cc1101_tx_raw_bits(bits, (int)strlen(bits), repeats);   /* rafale en UNE session TX */
    xSemaphoreGive(s_mtx);
}

int radio_listen_once(uint32_t timeout_ms, char *buf, int max) {
    if (!s_mtx) return -1;
    bool was = s_paused; s_paused = true;   /* met la tache permanente en pause pendant l'ecoute dediee */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int n = cc1101_rx_listen_bits(timeout_ms, buf, max);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return n;
}

void radio_pause_rx(bool pause) { s_paused = pause; }

/* Relance le self-test TX a la demande (bouton UI), sous mutex/pause pour ne pas
 * entrer en collision avec l'ecoute. Retourne 0 si la puce passe en TX, -1 sinon. */
int radio_tx_selftest(void) {
    if (!s_mtx) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = cc1101_tx_selftest();
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* Change la frequence porteuse a chaud (profil radio), sous mutex et ecoute en pause. */
int radio_set_freq_khz(uint32_t khz) {
    if (!s_mtx) return cc1101_set_freq_khz(khz);   /* avant radio_init : boot, pas de concurrence */
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = cc1101_set_freq_khz(khz);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* ── Auto-calibration du gain RX ──────────────────────────────────────────
 * Balaie une echelle de gain (du + sensible au + mordant) ; a chaque cran, ecoute
 * quelques secondes ; DES QU'UNE TRAME VALIDE (>=64 bits) est captee, verrouille ce
 * gain (= le + sensible qui donne une trame propre chez CE module). L'utilisateur
 * appuie sur sa telecommande pendant l'operation, sans lire aucun log. */
static volatile int     s_cal_state   = 0;   /* 0=idle 1=en cours 2=ok 3=echec */
static volatile uint8_t s_cal_testing = 0;   /* gain (AGCCTRL2) en cours de test */
static volatile uint8_t s_cal_result  = 0;   /* gain retenu si ok */

static void cal_task(void *arg) {
    (void)arg;
    static const uint8_t ladder[] = { 0x27, 0x2F, 0x37, 0x3F };
    char bits[80];
    bool was = s_paused; s_paused = true;              /* suspend l'ecoute permanente */
    int found = -1;
    for (int i = 0; i < (int)sizeof(ladder) && found < 0 && s_run; i++) {
        cc1101_set_rx_gain(ladder[i]);
        s_cal_testing = ladder[i];
        int64_t t_end = esp_timer_get_time() + 6 * 1000000;   /* 6 s par cran */
        while (esp_timer_get_time() < t_end && s_run) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            int n = cc1101_rx_listen_bits(400, bits, 79);
            xSemaphoreGive(s_mtx);
            if (n >= 64) { found = ladder[i]; break; }         /* trame propre = ce gain va */
        }
    }
    if (found > 0) { cc1101_set_rx_gain((uint8_t)found); s_cal_result = (uint8_t)found; s_cal_state = 2; }
    else           { cc1101_set_rx_gain(0x27);            s_cal_result = 0;             s_cal_state = 3; }
    s_paused = was;
    ESP_LOGW(TAG, "calibration gain RX : %s (0x%02X)", found > 0 ? "OK" : "ECHEC", found > 0 ? found : 0x27);
    vTaskDelete(NULL);
}

int radio_calibrate_start(void) {
    if (!s_mtx) return -1;
    if (s_cal_state == 1) return 1;                    /* deja en cours */
    s_cal_state = 1; s_cal_testing = 0; s_cal_result = 0;
    if (xTaskCreate(cal_task, "radio_cal", 4096, NULL, 5, NULL) != pdPASS) { s_cal_state = 0; return -1; }
    return 0;
}

void radio_calibrate_status(int *state, uint8_t *testing, uint8_t *result) {
    if (state)   *state   = s_cal_state;
    if (testing) *testing = s_cal_testing;
    if (result)  *result  = s_cal_result;
}

void radio_set_listening(bool on) {
    s_listen = on;
    ESP_LOGI(TAG, "ecoute permanente %s (case UI 'capture toutes les trames')", on ? "ON" : "OFF");
}

static void rx_task(void *arg) {
    (void)arg;
    char bits[80];
    while (s_run) {
        if (!s_listen || s_paused) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }  /* veille tant que capture OFF ou TX en cours */
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        int n = (!s_listen || s_paused) ? -1 : cc1101_rx_listen_bits(RX_WINDOW_MS, bits, 79);
        int8_t rssi = (n >= 64) ? cc1101_get_rssi() : 0;
        xSemaphoreGive(s_mtx);
        if (n >= 64 && s_cb) {
            bits[n] = 0;
            uint32_t serial = lsb(bits, 32, 28);
            uint32_t hop    = lsb(bits, 0, 32);
            uint8_t  button = (uint8_t)lsb(bits, 60, 4);
            s_cb(bits, serial, button, hop, rssi);
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));   /* cede la main entre 2 fenetres */
        }
    }
    vTaskDelete(NULL);
}

void radio_start(radio_frame_cb_t cb) {
    if (!s_mtx) radio_init();
    s_cb = cb; s_run = true;
    xTaskCreate(rx_task, "radio_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "RX permanent demarre (arbitrage RX/TX par mutex, fenetre %d ms)", RX_WINDOW_MS);
}
