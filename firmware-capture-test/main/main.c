/*
 * OpenProfalux main task orchestration
 * Boot flow:
 *   1. Init NVS
 *   2. Init CC1101
 *   3. Init Wi-Fi (STA from NVS config, else SoftAP)
 *   4. Init MQTT (broker from NVS config)
 *   5. Publish HA discovery
 *   6. Listen for MQTT commands
 */
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "hardware_config.h"
#include "profalux.h"
#include "cc1101.h"
#include "driver/gpio.h"
#include "wifi_bridge.h"
#include "mqtt_bridge.h"

static const char *TAG = "main";

/* Device name / config from NVS */
static char s_device_name[32] = "volet_test";
static char s_wifi_ssid[32]   = "";
static char s_wifi_pass[64]   = "";
static char s_mqtt_uri[128]   = "mqtt://ha.local:1883";
static char s_mqtt_user[32]   = "";
static char s_mqtt_pass[64]   = "";

static pfx_tx_state_t g_state;

/* Load user config from NVS "cfg" namespace */
static void load_config(void) {
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) return;
    size_t sz;
    sz = sizeof(s_device_name); nvs_get_str(h, "device", s_device_name, &sz);
    sz = sizeof(s_wifi_ssid);   nvs_get_str(h, "wifi_ssid", s_wifi_ssid, &sz);
    sz = sizeof(s_wifi_pass);   nvs_get_str(h, "wifi_pass", s_wifi_pass, &sz);
    sz = sizeof(s_mqtt_uri);    nvs_get_str(h, "mqtt_uri", s_mqtt_uri, &sz);
    sz = sizeof(s_mqtt_user);   nvs_get_str(h, "mqtt_user", s_mqtt_user, &sz);
    sz = sizeof(s_mqtt_pass);   nvs_get_str(h, "mqtt_pass", s_mqtt_pass, &sz);
    nvs_close(h);
}

/* Publish state JSON */
static void publish_state(const char *last_cmd) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"serial\":\"0x%08X\",\"counter\":%u,\"last_cmd\":\"%s\","
         "\"rssi\":%d,\"free_heap\":%u}",
        (unsigned)g_state.serial, (unsigned)g_state.counter, last_cmd,
        wifi_bridge_rssi(), (unsigned)esp_get_free_heap_size());
    mqtt_pub_state(s_device_name, json);
    ESP_LOGI(TAG, "State published: counter=%u last=%s", (unsigned)g_state.counter, last_cmd);
}

/* ────── Persistance NVS des trames captees (analyse ulterieure si test KO) ────── */
/* Sauvegarde chaque trame captee en flash (survit reboot / perte du log UART).
 * Namespace "caps" : "n" = compteur, "cK" = "rssi=.. gap=.. <bits bruts>". */
static void save_frame_nvs(const char *raw, int rssi, uint32_t gap) {
    nvs_handle_t h;
    if (nvs_open("caps", NVS_READWRITE, &h) != ESP_OK) return;
    uint16_t n = 0; nvs_get_u16(h, "n", &n);
    if (n < 64) {
        char key[8];  snprintf(key, sizeof(key), "c%u", (unsigned)n);
        char val[96]; snprintf(val, sizeof(val), "rssi=%d gap=%u %s", rssi, (unsigned)gap, raw);
        if (nvs_set_str(h, key, val) == ESP_OK && nvs_set_u16(h, "n", n + 1) == ESP_OK)
            nvs_commit(h);
    }
    nvs_close(h);
}
/* Redump toutes les trames sauvegardees (appele au boot). */
static void dump_frames_nvs(void) {
    nvs_handle_t h;
    if (nvs_open("caps", NVS_READONLY, &h) != ESP_OK) return;
    uint16_t n = 0; nvs_get_u16(h, "n", &n);
    if (n) ESP_LOGI("caps", "=== %u trame(s) sauvegardee(s) en NVS (analyse) ===", (unsigned)n);
    for (uint16_t i = 0; i < n; i++) {
        char key[8]; snprintf(key, sizeof(key), "c%u", (unsigned)i);
        char val[96]; size_t sz = sizeof(val);
        if (nvs_get_str(h, key, val, &sz) == ESP_OK) ESP_LOGI("caps", "  NVS[%u] %s", (unsigned)i, val);
    }
    nvs_close(h);
}
/* Efface le journal NVS des trames (nouvelle campagne de test). */
static void clear_frames_nvs(void) {
    nvs_handle_t h;
    if (nvs_open("caps", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h); nvs_commit(h); nvs_close(h);
    ESP_LOGW("caps", "journal NVS des trames EFFACE.");
}

/* Recharge les trames NVS dans le tableau de rejeu (au boot) : le rejeu 2/3 marche
 * sans re-capturer, meme apres coupure de courant. Retourne le nb charge. */
static int load_frames_nvs(char capbuf[][80], int *capbits, uint32_t *capgap, int maxn) {
    nvs_handle_t h;
    if (nvs_open("caps", NVS_READONLY, &h) != ESP_OK) return 0;
    uint16_t n = 0; nvs_get_u16(h, "n", &n);
    int cnt = 0;
    for (uint16_t i = 0; i < n && cnt < maxn; i++) {
        char key[8]; snprintf(key, sizeof(key), "c%u", (unsigned)i);
        char val[96]; size_t sz = sizeof(val);
        if (nvs_get_str(h, key, val, &sz) != ESP_OK) continue;
        /* format "rssi=%d gap=%u <bits>" : les bits sont apres le 2e espace */
        char *p = strchr(val, ' '); if (!p) continue;
        p = strchr(p + 1, ' ');     if (!p) continue;
        p++;
        unsigned int gtmp = 0; sscanf(val, "rssi=%*d gap=%u", &gtmp);
        uint32_t gap = gtmp;
        int nb = strlen(p);
        if (nb < 64 || nb > 78) continue;
        strncpy(capbuf[cnt], p, 79); capbuf[cnt][nb] = '\0';
        capbits[cnt] = nb; capgap[cnt] = gap;
        cnt++;
    }
    nvs_close(h);
    if (cnt) ESP_LOGI("caps", "%d trame(s) rechargee(s) depuis NVS -> rejeu 2/3 pret sans re-capture", cnt);
    return cnt;
}

/* Cherche dans NVS UNE trame (serial, bouton) et copie ses bits bruts dans out[80].
 * Retourne 1 si trouvee. Sert au test clair/hop : une seule trame de reference. */
static int find_frame_nvs(uint32_t want_serial, int want_btn, char *out) {
    nvs_handle_t h;
    if (nvs_open("caps", NVS_READONLY, &h) != ESP_OK) return 0;
    uint16_t n = 0; nvs_get_u16(h, "n", &n);
    int found = 0;
    for (uint16_t i = 0; i < n && !found; i++) {
        char key[8]; snprintf(key, sizeof(key), "c%u", (unsigned)i);
        char val[96]; size_t sz = sizeof(val);
        if (nvs_get_str(h, key, val, &sz) != ESP_OK) continue;
        char *p = strchr(val, ' '); if (!p) continue;
        p = strchr(p + 1, ' ');     if (!p) continue;
        p++;
        int nb = strlen(p); if (nb < 64) continue;
        uint32_t sr = 0; for (int b = 59; b >= 32; b--) sr = (sr << 1) | (p[b]-'0');
        int bb = 0;      for (int b = 60; b < 64; b++)  bb = (bb << 1) | (p[b]-'0');
        if (sr == want_serial && bb == want_btn) { strncpy(out, p, 79); out[nb < 79 ? nb : 79] = '\0'; found = 1; }
    }
    nvs_close(h);
    return found;
}

/* ────── MQTT handlers ────── */

static void on_pair(const char *device) {
    (void)device;
    /* Commande virtuelle DEVMEL capturee : bouton special 0x5. Ne pas la
     * confondre avec STOP+P (0x8) emis par une vraie telecommande. */
    ESP_LOGI(TAG, "▶ ENROLEMENT DEVMEL : SETTINGS interne silencieux ~5 s, puis ENROLL 0x5/cnt suivant.");
    ESP_LOGI(TAG, "  PUIS (vraie telecommande) : montee+Stop, descente+Stop, montee+Stop (SANS butees).");
    pfx_emit_enroll(&g_state);
    mqtt_pub_pair_result(s_device_name, true, 1);
    publish_state("PAIR");
}

static void on_reset(const char *device) {
    (void)device;
    ESP_LOGW(TAG, "⚠ RESET: generating new random state. LOSES PAIRING!");
    pfx_state_reset(&g_state);
    publish_state("RESET");
}

static void on_cmd(const char *device, const char *btn) {
    (void)device;
    uint8_t b = 0;
    if      (strcmp(btn, "UP")   == 0) b = PFX_BTN_UP;
    else if (strcmp(btn, "STOP") == 0) b = PFX_BTN_STOP;
    else if (strcmp(btn, "DOWN") == 0) b = PFX_BTN_DOWN;
    else { ESP_LOGW(TAG, "Unknown button: %s", btn); return; }
    ESP_LOGI(TAG, "▶ CMD %s", btn);
    pfx_emit_command(&g_state, b);
    publish_state(btn);
}

static void rx_frame_cb(const uint8_t *frame, size_t bits, int8_t rssi) {
    (void)bits;
    pfx_rx_frame_t rxf; pfx_frame_parse(frame, &rxf);
    char json[256];
    snprintf(json, sizeof(json),
        "{\"serial\":\"0x%08X\",\"button\":%u,\"encrypted_hop\":\"0x%08X\","
         "\"status\":%u,\"rssi\":%d}",
        (unsigned)rxf.serial, rxf.button, (unsigned)rxf.encrypted_hop,
        rxf.status_flags, rssi);
    mqtt_pub_rx_frame(json);
    ESP_LOGI(TAG, "◀ RX serial=0x%08X btn=%u rssi=%d",
             (unsigned)rxf.serial, rxf.button, rssi);
}

static void on_listen_start(void) {
    ESP_LOGI(TAG, "◀ LISTEN start");
    cc1101_rx_start(rx_frame_cb);
}

static void on_listen_stop(void) {
    ESP_LOGI(TAG, "◀ LISTEN stop");
    cc1101_rx_stop();
}

/* ────── App entry ────── */

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_INFO);   /* coupe le flot verbeux SPI/RMT: log lisible */
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ OpenProfalux firmware — target=" TARGET_NAME);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* 1. NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    /* 2. Config */
    load_config();
    ESP_LOGI(TAG, "device=%s wifi_ssid=%s mqtt=%s",
             s_device_name, s_wifi_ssid, s_mqtt_uri);

    /* 2b. Redump des trames captees sauvegardees (analyse) */
    dump_frames_nvs();

    /* 3. Profalux state */
    pfx_state_init(&g_state);

    /* 4. CC1101 */
    if (cc1101_init() != 0) {
        ESP_LOGE(TAG, "CC1101 init FAILED. Check wiring per hardware_config.h");
    }
    /* 4b. Auto-test emission au boot (prouve que la puce passe en TX). */
    if (cc1101_tx_selftest() == 0)
        ESP_LOGI(TAG, "TX SELFTEST OK : la puce emet (MARCSTATE=TX).");
    else
        ESP_LOGW(TAG, "TX SELFTEST ECHEC : puce NE passe PAS en TX (config/SPI).");
    /* 4c. Auto-verif trame : re-capture GDO0 de notre propre emission (slot 54, 0x8). */
    pfx_selfverify(&g_state, PFX_BTN_PROG);

    /* 5. Wi-Fi */
    wifi_bridge_init();
    if (strlen(s_wifi_ssid) > 0) {
        wifi_bridge_start_sta(s_wifi_ssid, s_wifi_pass);
        /* Wait for connection or timeout */
        int retry = 0;
        while (!wifi_bridge_is_connected() && retry++ < 60) vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!wifi_bridge_is_connected()) {
        ESP_LOGW(TAG, "Wi-Fi not connected. Starting SoftAP for config.");
        wifi_bridge_start_softap("OpenProfalux-Setup", "openprofalux");
    }

    /* 6. MQTT */
    if (wifi_bridge_is_connected() && strlen(s_mqtt_uri) > 0) {
        mqtt_bridge_start(s_mqtt_uri, s_device_name,
                          strlen(s_mqtt_user) ? s_mqtt_user : NULL,
                          strlen(s_mqtt_pass) ? s_mqtt_pass : NULL);
        mqtt_handlers_t h = {
            .on_pair = on_pair, .on_reset = on_reset, .on_cmd = on_cmd,
            .on_listen_start = on_listen_start, .on_listen_stop = on_listen_stop,
        };
        mqtt_bridge_set_handlers(&h);
        vTaskDelay(pdMS_TO_TICKS(2000));  /* let MQTT connect */
        mqtt_ha_publish_discovery(s_device_name);
        publish_state("BOOT");
    }

    /* Trigger LOCAL pour le test d'enrolement (pas besoin de MQTT/WiFi) :
     * appui sur le bouton integre de l'ATOM Lite (GPIO39) -> burst d'appairage. */
    gpio_config_t btn = { .pin_bit_mask = 1ULL << 39, .mode = GPIO_MODE_INPUT };
    gpio_config(&btn);
    ESP_LOGI(TAG, "PRET. Bouton G39 : 1=ENREGISTRE | 2=rejeu BRUT | 3=via OpenProfalux | 4=RECHARGE | 5=TEST clair/hop | 6+=efface.");

    /* Bouton ATOM (G39) :
     *   - appui LONG (>1,5 s) = ENROLEMENT (commande DEVMEL 0x5)
     *   - appui court        = PILOTAGE, cycle MONTEE -> STOP -> DESCENTE
     * Une seule image pour enroler puis piloter sans reflasher. */
    /* ===== MODE 2-TESTS (RollJam + preuve d'emission OpenProfalux) =====
     * Bouton ATOM G39, comptage d'appuis RAPIDES (fenetre 450 ms entre appuis) :
     *   - 1 appui       = ENREGISTRE la SEQUENCE : ecoute continue pendant que tu fais
     *                     haut/stop/bas/stop... ; chaque commande DISTINCTE est stockee
     *                     (repetitions de burst = meme hop dedoublonnees) avec le vrai
     *                     gap. Fin apres 4 s de silence.
     *   - 2 appuis      = TEST 1 : rejeu BRUT de la sequence avec le VRAI timing (magnetophone)
     *   - 3 appuis      = TEST 2 : rejeu de la sequence via OpenProfalux (build_with_hop,
     *                     vrai hop reversé) = preuve que OpenProfalux emet correctement
     *                     (framing byte-identique 7/7 offline).
     *   - 4 appuis      = RECHARGE les trames NVS dans le tableau (rejeu 2/3 sans re-capture, ex: apres coupure).
     *   - 5 appuis      = TEST clair/hop : hopping d'une trame EMPX (ref, non rejouee) emis avec bouton clair derive (0x1, 0x2).
     *   - 6 appuis ou + = efface le journal NVS des trames (nouvelle campagne).
     * Chaque commande est aussi persistee en NVS (analyse ulterieure, redump au boot).
     * Rappel : le rejeu ne marche que si le moteur n'a PAS entendu la trame (rolling). */
    #define CAP_MAX 16
    static char capbuf[CAP_MAX][80];
    static int  capbits[CAP_MAX];
    static uint32_t capgap[CAP_MAX];      /* ms depuis la trame precedente */
    int count = 0;   /* pas de recharge auto : 4 appuis pour recharger depuis NVS */
    int prev = 1; uint32_t hb = 0;
    while (1) {
        int now = gpio_get_level(39);
        if (prev == 1 && now == 0) {          /* front descendant = 1er appui */
            /* compte les appuis RAPIDES : fenetre 450 ms entre relachement et suivant */
            int taps = 0;
            for (;;) {
                while (gpio_get_level(39) == 0) vTaskDelay(pdMS_TO_TICKS(15));  /* attend relachement */
                taps++;
                int waited = 0, again = 0;
                while (waited < 700) {   /* fenetre elargie : compte 5 appuis fiablement */
                    vTaskDelay(pdMS_TO_TICKS(15)); waited += 15;
                    if (gpio_get_level(39) == 0) { again = 1; break; }
                }
                if (!again) break;
            }

            if (taps == 1) {
                /* ---- 1 APPUI : ENREGISTRE LA SEQUENCE (ecoute continue) ----
                 * Fais ta sequence sur la telecommande (haut/stop/bas/stop...). Chaque
                 * commande DISTINCTE est stockee (repetitions de burst = meme hop ignorees)
                 * avec le vrai gap. Fin apres 4 s de silence ou tableau plein. */
                count = 0;
                uint32_t last_hop = 0; int have_last = 0; int64_t t_prev_f = 0;
                char tmp[80];
                ESP_LOGI(TAG, ">>> ENREGISTREMENT SEQUENCE : fais haut/stop/bas/stop... (fin apres 4 s de silence) <<<");
                while (count < CAP_MAX) {
                    int n = cc1101_rx_listen_bits(4000, tmp, 79);   /* 4 s sans trame = fin de sequence */
                    if (n < 64) { ESP_LOGI(TAG, "  (silence : fin de sequence)"); break; }
                    tmp[n] = '\0';
                    uint32_t hop = 0; for (int b = 0; b < 32; b++) hop = (hop << 1) | (tmp[b]-'0');
                    if (have_last && hop == last_hop) continue;     /* repetition de burst : ignore */
                    int64_t tnow = esp_timer_get_time();
                    uint32_t gap = (t_prev_f == 0) ? 0 : (uint32_t)((tnow - t_prev_f) / 1000);
                    t_prev_f = tnow; last_hop = hop; have_last = 1;
                    memcpy(capbuf[count], tmp, n + 1); capbits[count] = n; capgap[count] = gap;
                    uint32_t serial = 0; for (int b = 59; b >= 32; b--) serial = (serial << 1) | (tmp[b]-'0');
                    int btn = 0;         for (int b = 60; b < 64; b++)  btn    = (btn    << 1) | (tmp[b]-'0');
                    ESP_LOGI(TAG, "  cmd#%d serial=0x%05X btn=0x%X hop=0x%08X gap=%ums",
                             count, (unsigned)serial, btn, (unsigned)hop, (unsigned)gap);
                    ESP_LOGI(TAG, "  RAW=%s", tmp);
                    save_frame_nvs(tmp, cc1101_get_rssi(), gap);    /* persistance flash */
                    count++;
                }
                ESP_LOGI(TAG, ">>> SEQUENCE enregistree : %d commande(s). 2 appuis=rejeu brut, 3 appuis=via OpenProfalux <<<", count);

            } else if (taps == 2) {
                /* ---- 2 APPUIS : TEST 1 = rejeu BRUT de la sequence + vrai timing ---- */
                if (count == 0) { ESP_LOGW(TAG, ">>> TEST 1 : rien a rejouer (enregistre d'abord : 1 appui) <<<"); }
                else {
                    ESP_LOGI(TAG, ">>> TEST 1 : REJEU BRUT de %d commande(s) avec le vrai timing <<<", count);
                    for (int i = 0; i < count; i++) {
                        if (i > 0 && capgap[i] > 0 && capgap[i] < 20000) vTaskDelay(pdMS_TO_TICKS(capgap[i]));
                        for (int k = 0; k < 3; k++) { cc1101_tx_raw_bits(capbuf[i], capbits[i]); vTaskDelay(pdMS_TO_TICKS(30)); }
                        ESP_LOGI(TAG, "  rejoue cmd#%d (%d bits, gap=%ums)", i, capbits[i], (unsigned)capgap[i]);
                    }
                    ESP_LOGI(TAG, "  TEST 1 fini. Le volet a suivi la sequence ?");
                }

            } else if (taps == 3) {
                /* ---- 3 APPUIS : TEST 2 = rejeu de la SEQUENCE via OpenProfalux ---- */
                if (count == 0) { ESP_LOGW(TAG, ">>> TEST 2 : rien a rejouer (enregistre d'abord : 1 appui) <<<"); }
                else {
                    ESP_LOGI(TAG, ">>> TEST 2 : REJEU via OpenProfalux de %d commande(s) avec le vrai timing <<<", count);
                    for (int i = 0; i < count; i++) {
                        if (i > 0 && capgap[i] > 0 && capgap[i] < 20000) vTaskDelay(pdMS_TO_TICKS(capgap[i]));
                        /* vrai hop KeeLoq = lecture LSB-first des bits d'air (bit-reverse du MSB-first) */
                        uint32_t hop_true = 0; for (int b = 0; b < 32; b++) hop_true |= (uint32_t)(capbuf[i][b]-'0') << b;
                        uint32_t serial   = 0; for (int b = 59; b >= 32; b--) serial = (serial << 1) | (capbuf[i][b]-'0');
                        int btn = 0;           for (int b = 60; b < 64; b++)  btn    = (btn    << 1) | (capbuf[i][b]-'0');
                        uint8_t frame[9];
                        pfx_frame_build_with_hop(hop_true, serial, (uint8_t)btn, frame);
                        for (int k = 0; k < 3; k++) { cc1101_tx_ook_frame(frame, 66); vTaskDelay(pdMS_TO_TICKS(30)); }
                        ESP_LOGI(TAG, "  cmd#%d via OpenProfalux (btn=0x%X hop_true=0x%08X gap=%ums)",
                                 i, btn, (unsigned)hop_true, (unsigned)capgap[i]);
                    }
                    ESP_LOGI(TAG, "  TEST 2 fini. Le volet a suivi la sequence ? (si oui = OpenProfalux emet correctement)");
                }

            } else if (taps == 4) {
                /* ---- 4 APPUIS : RECHARGE les trames NVS dans le tableau de rejeu ---- */
                count = load_frames_nvs(capbuf, capbits, capgap, CAP_MAX);
                ESP_LOGI(TAG, ">>> RECHARGE NVS : %d trame(s) prete(s). 2 appuis=rejeu brut, 3=via OpenProfalux <<<", count);
            } else if (taps == 5) {
                /* ---- 5 APPUIS : TEST "clair ou hopping decide ?" ----
                 * REFERENCE = une trame EMPX (0x813) bouton 0x4. On NE la rejoue PAS :
                 * on prend son HOPPING et on l'emet avec le bouton clair DERIVE vers
                 * les autres commandes (0x1 puis 0x2), avec pause pour observer. Si le
                 * volet suit le bouton clair => le CLAIR decide. EMPX SEULEMENT, jamais
                 * la Noe globale (0x415560). AUTONOME : va chercher la ref dans NVS. */
                char ref[80];
                if (!find_frame_nvs(0x813, 0x4, ref)) {
                    ESP_LOGW(TAG, ">>> TEST : aucune trame EMPX (0x813) bouton 0x4 en NVS (capture-en une d'abord) <<<");
                } else {
                    uint32_t hop_true = 0; for (int b = 0; b < 32; b++) hop_true |= (uint32_t)(ref[b]-'0') << b;
                    uint32_t hop_msb = 0;  for (int b = 0; b < 32; b++) hop_msb = (hop_msb << 1) | (ref[b]-'0');
                    ESP_LOGI(TAG, ">>> TEST clair/hop | REFERENCE (NON rejouee) : EMPX 0x813 bouton_orig 0x4 hopping=0x%08X <<<",
                             (unsigned)hop_msb);
                    uint8_t deriv[3] = { 0x1, 0x2, 0x4 };   /* tous les boutons trouves */
                    for (int j = 0; j < 3; j++) {
                        uint8_t frame[9];
                        pfx_frame_build_with_hop(hop_true, 0x813, deriv[j], frame);  /* MEME hopping, bouton clair change */
                        ESP_LOGI(TAG, "  -> DERIVATION %d/3 : meme hopping + bouton clair 0x%X -- REGARDE LE VOLET", j + 1, deriv[j]);
                        for (int k = 0; k < 8; k++) { cc1101_tx_ook_frame(frame, 66); vTaskDelay(pdMS_TO_TICKS(30)); }  /* ~1 s */
                        ESP_LOGI(TAG, "     (emis. Pause 4 s pour observer avant la commande suivante...)");
                        vTaskDelay(pdMS_TO_TICKS(4000));
                    }
                    ESP_LOGI(TAG, "  Meme hopping, 3 boutons clairs differents : le volet a-t-il fait 3 choses ? OUI = le CLAIR decide.");
                }
            } else {
                /* ---- 6 APPUIS ou + : efface le journal NVS des trames ---- */
                clear_frames_nvs();
                count = 0;
            }
            now = 1;                            /* relache */
        }
        prev = now;
        if (++hb >= 1200) { hb = 0; publish_state("HEARTBEAT"); }  /* ~60s */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
