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
#include <nvs_flash.h>
#include <nvs.h>

#include "hardware_config.h"
#include "profalux.h"
#include "cc1101.h"
#include "driver/gpio.h"
#include "wifi_bridge.h"
#include "mqtt_bridge.h"
#include "shutters.h"
#include "web_ui.h"
#include "ota.h"
#include <esp_timer.h>
#include <esp_mac.h>
#include "mdns.h"

static const char *TAG = "main";
static bool s_log_frames = false;   /* option UI "capture toutes les trames" (namespace cfg) */

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
    uint8_t lf = 0; nvs_get_u8(h, "log_frames", &lf); s_log_frames = lf;
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

/* ────── MQTT handlers ────── */

static void on_pair(const char *device) {
    (void)device;
    /* Ne pas confondre le STOP+P d'une vraie telecommande (btn 0x8) avec la
     * commande virtuelle d'enrolement DEVMEL capturee (btn 0x5). Le runtime
     * DEVMEL place explicitement ce nibble dans le plaintext KeeLoq. */
    ESP_LOGI(TAG, "▶ ENROLEMENT DEVMEL : SETTINGS 0x0/cnt courant ~5 s, puis ENROLL 0x5/cnt suivant.");
    ESP_LOGI(TAG, "  PUIS (vraie telecommande) : montee+Stop, descente+Stop, montee+Stop (SANS butees).");
    pfx_emit_hold(&g_state, PFX_BTN_SETTINGS, 5000);
    vTaskDelay(pdMS_TO_TICKS(300));
    pfx_emit_command(&g_state, PFX_BTN_ENROLL);
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

static void on_ota_pull(const char *url) {
    ESP_LOGI(TAG, "▶ OTA pull depuis %s", url);
    ota_pull_from_url(url);
}

/* NB : l'ancien chemin RX MQTT (rx_frame_cb / on_listen_start / on_listen_stop +
 * cc1101_rx_start) est remplace par le module radio (RX permanent arbitre), demarre
 * par shutters_init(). Les trames recues sont routees vers shutters_on_rx en interne. */

/* ────── App entry ────── */

void app_main(void) {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ OpenProfalux firmware — target=" TARGET_NAME);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* 1. NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    /* 1b. OTA (rollback safety : marque l'image valide plus bas apres ~60 s) */
    ota_init();

    /* 2. Config */
    load_config();
    ESP_LOGI(TAG, "device=%s wifi_ssid=%s mqtt=%s",
             s_device_name, s_wifi_ssid, s_mqtt_uri);

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

    /* 4c. Modele cover + arbitre radio (RX permanent gere en interne) */
    shutters_init();
    if (s_log_frames) shutters_set_log_frames(true);

    /* 5. Wi-Fi */
    wifi_bridge_init();
    if (strlen(s_wifi_ssid) > 0) {
        wifi_bridge_start_sta(s_wifi_ssid, s_wifi_pass);
        /* Wait for connection or timeout */
        int retry = 0;
        while (!wifi_bridge_is_connected() && retry++ < 60) vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!wifi_bridge_is_connected()) {
        uint8_t mac[6] = {0}; esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        char ap_ssid[32]; snprintf(ap_ssid, sizeof(ap_ssid), "OpenProfalux_%02X%02X", mac[4], mac[5]);
        ESP_LOGW(TAG, "Wi-Fi not connected. Starting SoftAP '%s' (open) for config.", ap_ssid);
        wifi_bridge_start_softap(ap_ssid, "");   /* mdp vide => WIFI_AUTH_OPEN (comme OpenXtraflamme) */
    }

    /* 5b. mDNS (pour l'auto-decouverte du broker MQTT depuis l'UI) */
    if (wifi_bridge_is_connected()) {
        if (mdns_init() == ESP_OK) { mdns_hostname_set("openprofalux"); mdns_instance_name_set("OpenProfalux"); }
    }

    /* 5c. UI web embarquee (config Wi-Fi/MQTT, apprentissage, OTA...) */
    web_ui_start();

    /* 6. MQTT */
    if (wifi_bridge_is_connected() && strlen(s_mqtt_uri) > 0) {
        mqtt_bridge_start(s_mqtt_uri, s_device_name,
                          strlen(s_mqtt_user) ? s_mqtt_user : NULL,
                          strlen(s_mqtt_pass) ? s_mqtt_pass : NULL);
        mqtt_handlers_t h = {
            .on_pair = on_pair, .on_reset = on_reset, .on_cmd = on_cmd,
            .on_message = shutters_mqtt_on_message,   /* cover HA */
            .on_ota_pull = on_ota_pull,               /* OTA pull */
        };
        mqtt_bridge_set_handlers(&h);
        vTaskDelay(pdMS_TO_TICKS(2000));  /* let MQTT connect */
        shutters_mqtt_announce(s_device_name);   /* 1 cover HA par volet appris */
        publish_state("BOOT");
    }

    /* Trigger LOCAL pour le test d'enrolement (pas besoin de MQTT/WiFi) :
     * appui sur le bouton integre de l'ATOM Lite (GPIO39) -> burst d'appairage. */
    gpio_config_t btn = { .pin_bit_mask = 1ULL << 39, .mode = GPIO_MODE_INPUT };
    gpio_config(&btn);
    ESP_LOGI(TAG, "PRET. Bouton ATOM (G39) = burst appairage. Identite serial=0x%08X counter=%u.",
             (unsigned)g_state.serial, (unsigned)g_state.counter);

    /* Bouton ATOM (G39) :
     *   - appui LONG (>1,5 s) = ENROLEMENT (commande DEVMEL 0x5)
     *   - appui court        = PILOTAGE, cycle MONTEE -> STOP -> DESCENTE
     * Une seule image pour enroler puis piloter sans reflasher. */
    const uint8_t  seq[3]  = { PFX_BTN_UP, PFX_BTN_STOP, PFX_BTN_DOWN };
    const char    *seqn[3] = { "MONTEE", "STOP", "DESCENTE" };
    int si = 0;

    int prev = 1; uint32_t hb = 0; bool ota_marked = false;
    while (1) {
        int now = gpio_get_level(39);
        if (prev == 1 && now == 0) {          /* front descendant = debut appui */
            uint32_t held = 0;
            while (gpio_get_level(39) == 0) { vTaskDelay(pdMS_TO_TICKS(20)); held += 20; }
            if (held >= 1500) {
                ESP_LOGI(TAG, ">>> Appui LONG (%ums) : ENROLEMENT (bouton special 0x5) <<<",
                         (unsigned)held);
                on_pair(s_device_name);
            } else {
                ESP_LOGI(TAG, ">>> Appui court : commande %s (serial=0x%08X counter=%u) <<<",
                         seqn[si], (unsigned)g_state.serial, (unsigned)g_state.counter);
                pfx_emit_command(&g_state, seq[si]);
                si = (si + 1) % 3;
            }
            now = 1;                            /* relache */
        }
        prev = now;
        if (++hb >= 1200) { hb = 0; publish_state("HEARTBEAT");
            if (!ota_marked) { ota_mark_valid(); ota_marked = true; }  /* valide l'image OTA apres ~60 s */
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
