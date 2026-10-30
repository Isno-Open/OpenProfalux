/*
 * radio.c : arbitrage RX/TX de l'unique CC1101 (voir radio.h).
 */
#include "radio.h"
#include "cc1101.h"
#include "freq_sweep.h"
#include "hardware_config.h"
#include "rx_confirm.h"
#include "io/io_radio.h"
#include "io/io_send.h"
#include "io/io_store.h"
#include "io/io_pair.h"
#include "io/io_trace.h"
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
static volatile bool     s_io_listen = false; /* ecoute permanente io (2-FSK) : exclusive de l'ecoute PFX */

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

int radio_listen_confirmed(uint32_t timeout_ms, char *buf, int max, bool full) {
    if (!s_mtx || max < RXC_FULL_BITS + 1) return -1;
    bool was = s_paused; s_paused = true;   /* met la tache permanente en pause pendant l'ecoute dediee */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    /* Fenetres successives : on prefere deux trames identiques (rx_confirm.h, issue #39), mais a
     * defaut on accepte une trame complete (repli ci-dessous, issue #1), car ici l'utilisateur a
     * lance la capture expres et tient le bouton. */
    static rx_confirm_t c;   /* sous s_mtx : une seule ecoute dediee a la fois */
    rx_confirm_reset(&c);
    char bits[80];
    /* Derniere trame complete et valide vue, a defaut de paire confirmee. */
    char repli[RXC_FULL_BITS + 1]; repli[0] = 0;
    const int need = full ? RXC_FULL_BITS : RXC_CMP_BITS;
    int64_t repli_us = 0;   /* instant de la 1re trame complete vue (delai de grace, issue #1) */
    int r = -1;
    int64_t t_end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (int64_t now; (now = esp_timer_get_time()) < t_end; ) {
        /* Trame complete deja captee et courte grace ecoulee : on la prend sans attendre la fin de
         * la fenetre. On laisse d'abord une chance a une paire, mais on ne fait pas patienter. */
        if (repli[0] && now - repli_us > 1500000) break;
        uint32_t w = (uint32_t)((t_end - now) / 1000) + 1;
        if (w > 300) w = 300;   /* fenetres courtes : la grace ci-dessus reagit vite */
        int n = cc1101_rx_listen_bits(w, bits, 79);
        if (n < 64) continue;
        bits[n] = 0;
        if (n >= need && n <= RXC_FULL_BITS) { strlcpy(repli, bits, sizeof repli); if (!repli_us) repli_us = now; }
        if (!rx_confirm_push(&c, bits, n, esp_timer_get_time())) continue;
        const char *ok = full ? c.full : c.last;   /* apprentissage : une trame complete de 66 bits */
        if (!ok[0]) continue;                      /* confirmee mais coupee : on attend la suivante */
        strlcpy(buf, ok, (size_t)max);
        r = (int)strlen(buf);
        ESP_LOGW(TAG, "ecoute dediee : trame confirmee (%s)",
                 c.weak ? "reception faible, repetition lente" : "repetition normale");
        break;
    }
    /* Pas de paire confirmee dans la fenetre, mais une trame complete est passee : on l'accepte.
     * La double trame reste de rigueur pour l'ecoute permanente (telecommandes fantomes, #39) ;
     * ici la capture est explicite, une trame bien formee suffit (issue #1). */
    if (r < 0 && repli[0]) { strlcpy(buf, repli, (size_t)max); r = (int)strlen(buf); }
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

void radio_pause_rx(bool pause) { s_paused = pause; }

/* Sniffer io-homecontrol : passe la radio en FSK io (config + canal CH2), ecoute UNE trame
 * desassemblee, puis RESTAURE la modulation OOK Profalux et la porteuse. Sous mutex, ecoute
 * permanente en pause (comme l'ecoute dediee). Rend le nb d'octets io, 0 si rien, -1 si erreur. */
int radio_io_sniff(uint8_t *buf, int max, uint32_t timeout_ms) {
    if (!s_mtx || !buf || max <= 0) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();                         /* FSK io + canal CH2 */
    int n = io_radio_rx_packet(buf, (size_t)max, timeout_ms);
    cc1101_apply_config_ook();                 /* restaure la modulation OOK Profalux */
    cc1101_set_freq_khz(back);                 /* restaure la porteuse */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return n;
}

/* Capture CIBLEE d'un 0x30 io : la cle de la telecommande, diffusee quand on la met en PROG (appui
 * ~3 s). Passe en FSK io et ECOUTE jusqu'a total_ms a la recherche d'une trame 0x30 (ignore les
 * autres trames de la ceremonie 0x39/0x20/0x2e), puis RESTAURE l'OOK Profalux. Fenetre longue : on
 * clique PUIS on va faire le PROG. Rend le nb d'octets du 0x30 capte, 0 si rien dans le delai. */
int radio_io_capture_0x30(uint8_t *buf, int max, uint32_t total_ms) {
    if (!s_mtx || !buf || max <= 0) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();                         /* FSK io + canal CH2 */
    /* Le journal RX+TX n'est PAS remis a zero ici : le remise a zero est pilotee par l'etape 1
     * (capter le compteur), pour que compteur + cle + ceremonie + nos TX tiennent dans un journal. */
    int got = 0;
    uint8_t keyframe[32]; int keylen = 0;
    int64_t t0 = esp_timer_get_time(), t_key = 0;
    while ((esp_timer_get_time() - t0) / 1000 < (int64_t)total_ms) {
        int n = io_radio_rx_packet(buf, (size_t)max, 500);
        if (n >= 9) {
            /* TRACE : on log CHAQUE trame captee (qui fait quoi). Pas de la box de copie ici (enrolement par
             * notre firmware), donc tout ce qu'on capte = la telecommande/moteur. On NE S'ARRETE PAS
             * au 0x30 : on capte toute l'instance (commandes, cerem.) pour voir la sequence complete. */
            int8_t rssi = cc1101_get_rssi();
            char hx[2 * 32 + 1];
            for (int i = 0; i < n && i < 32; i++) snprintf(hx + 2 * i, 3, "%02X", buf[i]);
            ESP_LOGW(TAG, "ENROL RX (%d o, RSSI %d dBm) cmd=0x%02X : %s", n, rssi, buf[8], hx);
            io_trace_add('R', rssi, buf, n);   /* journal RX+TX recuperable par /api/io/trace */
            if (n >= 31 && buf[8] == 0x30 && keylen == 0) {   /* garde le 1er 0x30, mais continue a capter */
                memcpy(keyframe, buf, (size_t)(n < 32 ? n : 32)); keylen = n; t_key = esp_timer_get_time();
            }
        }
        if (keylen && (esp_timer_get_time() - t_key) / 1000 > 3000) break;   /* 0x30 + 3 s de suite puis stop */
    }
    if (keylen) { memcpy(buf, keyframe, (size_t)(keylen < 32 ? keylen : 32)); got = keylen; }
    io_trace_persist();                        /* flush du journal RX en NVS (survit au reboot) */
    cc1101_apply_config_ook();                 /* restaure la modulation OOK Profalux */
    cc1101_set_freq_khz(back);                 /* restaure la porteuse */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return got;
}

/* Emetteur io-homecontrol : passe la radio en FSK io, emet UN appui bouton 1W depuis la source
 * src du volet (sa cle + son compteur roulant lus dans io_store), diffuse vers le broadcast
 * Somfy, puis RESTAURE l'OOK Profalux et la porteuse. Sous mutex, ecoute permanente en pause.
 * Rend 0 si emis, -1 sinon (source inconnue dans io_store, ou echec radio). */
int radio_io_send(const uint8_t src[3], uint8_t button) {
    if (!s_mtx || !src) return -1;
    uint8_t key[16];
    if (!io_store_get_key(src, key)) return -1;    /* ce volet n'a pas d'identite io appairee */
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();                         /* FSK io + canal CH2 */
    int r = io_hc_send_shutter_1w(src, key, button);
    cc1101_apply_config_ook();                 /* restaure la modulation OOK Profalux */
    cc1101_set_freq_khz(back);                 /* restaure la porteuse */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* Appairage 1W io (poussee de cle vers un moteur en mode PROG) : passe la radio en FSK io,
 * lance io_hc_pair_1w, puis RESTAURE l'OOK Profalux. Sous mutex, ecoute en pause. Rend 0 et
 * remplit out_src (identite d'emetteur creee pour ce volet) si emis, -2 sur erreur. */
int radio_io_pair_1w(const io_profile_t *profile, const uint8_t *fixed_src,
                     const uint8_t *fixed_key, int man_id, uint8_t out_src[3], uint32_t timeout_ms) {
    if (!s_mtx || !out_src || !profile) return -2;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();                         /* FSK io */
    int r = io_hc_pair_1w(profile, fixed_src, fixed_key, man_id, out_src, timeout_ms);
    cc1101_apply_config_ook();                 /* restaure la modulation OOK Profalux */
    cc1101_set_freq_khz(back);                 /* restaure la porteuse */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* Enrolement (mode 1 simple temoin / 2 rituel signe ORIGINE + NOTRE identite) sous une seule session
 * io : pose la config FSK io, appelle io_hc_enroll, puis RESTAURE l'OOK Profalux. Voir io_pair.h.
 * La cle de l'origine (origin_src/origin_key/origin_seq) est captee AVANT, par le bouton dedie
 * « Ecoute en mode PROG » (radio_io_capture_0x30 -> /api/io/captkey), puis fournie ici. */
int radio_io_enroll(const io_profile_t *profile,
                    const uint8_t our_src[3], const uint8_t our_key[16], int man_id,
                    int mode, const uint8_t *origin_src, const uint8_t *origin_key,
                    int origin_seq, int gap_ms, int confirm_repeats, uint8_t out_src[3]) {
    if (!s_mtx || !out_src || !profile) return -2;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();                         /* FSK io */
    int r = io_hc_enroll(profile, our_src, our_key, man_id, mode, origin_src, origin_key,
                         origin_seq, gap_ms, confirm_repeats, out_src);
    cc1101_apply_config_ook();                 /* restaure la modulation OOK Profalux */
    cc1101_set_freq_khz(back);                 /* restaure la porteuse */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}


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

/* ── Balayage RSSI : porteuse de la telecommande (issue #51) ──────────────────────
 * Pendant une emission longue de la telecommande (commutateur P seul), balaie la frequence de
 * reception autour de la frequence actuelle et trouve sa porteuse (freq_sweep.h). Tache de
 * fond, ecoute permanente en pause. N'applique rien : l'UI propose la frequence trouvee. */
#define SWEEP_SPAN_KHZ  150
#define SWEEP_STEP_KHZ  10
#define SWEEP_DWELL_MS  150   /* > une trame (~135 ms a TE 448) : le RSSI max voit un HAUT */
#define SWEEP_MAX       (2 * SWEEP_SPAN_KHZ / SWEEP_STEP_KHZ + 1)
static volatile int s_sw_state = 0;   /* 0 jamais lance, 1 en cours, 2 fini */
static uint32_t s_sw_khz[SWEEP_MAX];
static int16_t  s_sw_hdb[SWEEP_MAX];
static int      s_sw_n;
static uint32_t s_sw_from;            /* frequence au lancement */
static int      s_sw_status;          /* fsweep_status_t, ou -10 si la puce n'est pas passee en RX */
static fsweep_result_t s_sw_res;

static void sweep_task(void *arg) {
    (void)arg;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_sw_from = cc1101_get_freq_khz();
    s_sw_n = 0;
    for (int o = -SWEEP_SPAN_KHZ; o <= SWEEP_SPAN_KHZ && s_sw_n < SWEEP_MAX; o += SWEEP_STEP_KHZ) {
        uint32_t f = s_sw_from + o;
        if (f >= CC1101_BAND_LOW_KHZ && f <= CC1101_BAND_HIGH_KHZ) s_sw_khz[s_sw_n++] = f;   /* bande de la carte */
    }
    int rc = cc1101_rssi_scan(s_sw_khz, s_sw_n, SWEEP_DWELL_MS, s_sw_hdb);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    s_sw_status = rc ? -10 : fsweep_analyze(s_sw_khz, s_sw_hdb, s_sw_n, &s_sw_res);
    if (s_sw_status == FSWEEP_OK)
        ESP_LOGW(TAG, "balayage RSSI : porteuse %u kHz (profil %u kHz, ecart %+d kHz), pic %d dBm, bruit %d dBm",
                 (unsigned)s_sw_res.center_khz, (unsigned)s_sw_from, (int)s_sw_res.center_khz - (int)s_sw_from,
                 s_sw_res.peak_hdb / 2, s_sw_res.floor_hdb / 2);
    else
        ESP_LOGW(TAG, "balayage RSSI : echec (%d), pic %d dBm, bruit %d dBm", s_sw_status,
                 s_sw_res.peak_hdb / 2, s_sw_res.floor_hdb / 2);
    s_sw_state = 2;
    vTaskDelete(NULL);
}

int radio_sweep_start(void) {
    if (!s_mtx) return -1;
    if (s_sw_state == 1 || s_cal_state == 1) return 1;   /* deja en cours (ou calibration de gain) */
    s_sw_state = 1; s_sw_status = 0; memset(&s_sw_res, 0, sizeof(s_sw_res));
    if (xTaskCreate(sweep_task, "radio_sweep", 4096, NULL, 5, NULL) != pdPASS) { s_sw_state = 0; return -1; }
    return 0;
}

int radio_sweep_status(int *status, uint32_t *from_khz, const uint32_t **khz, const int16_t **hdb, int *n,
                       uint32_t *center_khz, int16_t *peak_hdb, int16_t *floor_hdb) {
    *status = s_sw_status; *from_khz = s_sw_from; *khz = s_sw_khz; *hdb = s_sw_hdb;
    *n = s_sw_state == 2 ? s_sw_n : 0;
    *center_khz = s_sw_res.center_khz; *peak_hdb = s_sw_res.peak_hdb; *floor_hdb = s_sw_res.floor_hdb;
    return s_sw_state;
}

void radio_set_listening(bool on) {
    s_listen = on;
    ESP_LOGI(TAG, "ecoute permanente %s (case UI 'capture toutes les trames')", on ? "ON" : "OFF");
}

void radio_set_io_listening(bool on) {
    s_io_listen = on;
    ESP_LOGW(TAG, "ecoute io permanente %s (2-FSK Somfy io)", on ? "ON" : "OFF");
}

bool radio_is_io_listening(void) { return s_io_listen; }

static void rx_task(void *arg) {
    (void)arg;
    char bits[80];
    static rx_confirm_t c;   /* serie en cours : une trame n'est transmise que confirmee (issue #39) */
    rx_confirm_reset(&c);
    bool     io_posed = false;   /* la config io 2-FSK est-elle actuellement en place ? */
    uint32_t ook_freq = 0;       /* porteuse OOK a restaurer en quittant le mode io */
    while (s_run) {
        /* ── Ecoute io permanente (2-FSK) : branche isolee, prioritaire et exclusive de l'OOK.
         * On pose la config io une fois (en memorisant la porteuse OOK), on boucle la reception io
         * et on journalise chaque trame ; a l'arret on restaure l'OOK. Le chemin PFX ci-dessous
         * n'est pas touche tant que s_io_listen est faux (io_posed reste faux). */
        if (s_io_listen && !s_paused) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (!io_posed) { ook_freq = cc1101_get_freq_khz(); io_radio_config(); io_posed = true; }
            uint8_t iob[32];
            int n = io_radio_rx_packet(iob, sizeof iob, 300);
            int8_t io_rssi = cc1101_get_rssi();   /* dBm du dernier paquet, avant de rendre le mutex */
            xSemaphoreGive(s_mtx);
            if (n > 0) {
                char hex[2 * 32 + 1];
                int m = n > 32 ? 32 : n;
                for (int i = 0; i < m; i++) snprintf(hex + 2 * i, 3, "%02X", iob[i]);
                ESP_LOGW(TAG, "Trame io captee (%d octets, RSSI %d dBm) : %s", n, io_rssi, hex);
                /* Trame d'enrolement 0x30 d'une identite dont on n'a PAS la cle : on la note de
                 * facon persistante (NVS) avec RSSI + horodatage, pour la decoder plus tard et
                 * demeler l'emetteur. iob[8] = commande, iob[5:8] = source. */
                if (n >= 31 && iob[8] == 0x30) {
                    uint8_t usrc[3] = { iob[5], iob[6], iob[7] };
                    uint32_t ts_s = (uint32_t)(esp_timer_get_time() / 1000000);
                    if (io_store_record_unknown(usrc, iob, n, io_rssi, ts_s))
                        ESP_LOGW(TAG, "identite io INCONNUE notee (RSSI %d dBm) : %02X%02X%02X",
                                 io_rssi, usrc[0], usrc[1], usrc[2]);
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            continue;
        }
        if (io_posed) {   /* on quitte le mode io : restaure la modulation OOK et la porteuse */
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            cc1101_apply_config_ook();
            cc1101_set_freq_khz(ook_freq);
            xSemaphoreGive(s_mtx);
            io_posed = false;
        }
        if (!s_listen || s_paused) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }  /* veille tant que capture OFF ou TX en cours */
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        int n = (!s_listen || s_paused) ? -1 : cc1101_rx_listen_bits(RX_WINDOW_MS, bits, 79);
        int8_t rssi = (n >= 64) ? cc1101_get_rssi() : 0;
        xSemaphoreGive(s_mtx);
        if (n >= 64) bits[n] = 0;
        /* Confirmee = identique a la precedente (64 premiers bits) : la 1re trame d'un appui
         * attend la 2e (~125 ms). Les suivantes de la rafale passent toutes (le suivi de
         * position et le journal RF dedupliquent). */
        if (n >= 64 && rx_confirm_push(&c, bits, n, esp_timer_get_time()) && s_cb) {
            if (c.weak) ESP_LOGW(TAG, "trame confirmee en reception faible (repetition lente)");
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
