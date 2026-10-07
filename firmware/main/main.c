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
#include "cc1101.h"
#include "wifi_bridge.h"
#include "mqtt_bridge.h"
#include "shutters.h"
#include "pfx_enrol.h"
#include "web_ui.h"
#include "ota.h"
#include <esp_timer.h>
#include <esp_mac.h>
#include "mdns.h"
#include "esp_app_desc.h"   /* esp_app_get_description()->version = PROJECT_VER */
#include "log_ring.h"
#include <esp_attr.h>     /* RTC_NOINIT_ATTR */
#include <esp_system.h>   /* esp_reset_reason */

static const char *TAG = "main";
static bool s_log_frames = false;   /* option UI "capture toutes les trames" (namespace cfg) */
static bool s_debug      = false;   /* switch UI "debug console" : logge chaque capture RX */
static uint8_t s_rx_gain = 0x27;    /* plafond de gain RX (AGCCTRL2) reglable via l'UI ; defaut 0x27 */
static uint32_t s_tx_te  = 455;     /* TE d'emission (us) reglable via l'UI ; defaut 455 (Profalux) */

/* Device name / config from NVS */
static char s_device_name[32] = "volet_test";
static char s_wifi_ssid[32]   = "";
static char s_wifi_pass[64]   = "";
static char s_mqtt_uri[160]   = "mqtt://ha.local:1883";
static char s_mqtt_user[128]  = "";   /* certains brokers utilisent des tokens JWT longs */
static char s_mqtt_pass[256]  = "";   /* HA peut generer des mdp broker >64 : marge (nvs_get_str echoue si buffer trop court -> champ vide -> 'not authorized'). Aligne sur OpenXtraflame. */

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
    uint8_t db = 0; nvs_get_u8(h, "debug", &db); s_debug = db;
    uint8_t rg = 0; if (nvs_get_u8(h, "rx_gain", &rg) == ESP_OK && rg) s_rx_gain = rg;
    uint32_t te = 0; if (nvs_get_u32(h, "tx_te", &te) == ESP_OK && te) s_tx_te = te;
    nvs_close(h);
    /* Nom d'appareil vide -> defaut : sinon client_id MQTT vide + topic de disponibilite
     * qui ne coincide pas avec la decouverte HA. */
    if (!s_device_name[0]) strlcpy(s_device_name, "openprofalux", sizeof(s_device_name));
}

/* Publish state JSON (heartbeat : etat liaison + heap libre) */
static void publish_state(const char *last_cmd) {
    char json[192];
    snprintf(json, sizeof(json),
        "{\"last_cmd\":\"%s\",\"rssi\":%d,\"free_heap\":%u}",
        last_cmd, wifi_bridge_rssi(), (unsigned)esp_get_free_heap_size());
    mqtt_pub_state(s_device_name, json);
}

/* ────── MQTT handlers ────── */

static void on_ota_pull(const char *url) {
    ESP_LOGI(TAG, "▶ OTA pull depuis %s", url);
    ota_pull_from_url(url);
}
static void on_mqtt_connected(void) {
    shutters_mqtt_announce(s_device_name);   /* publie la decouverte HA a la VRAIE connexion broker */
}

/* ── Retour du Wi-Fi en mode secours (wifi_bridge_start_fallback) ──
 * Des que le reseau repond, on redemarre : le demarrage normal (MQTT, mDNS, decouverte HA)
 * est deja eprouve, et rien n'est perdu (config et positions en NVS). Une mise a jour
 * envoyee par le point d'acces est attendue TANT QU'ELLE AVANCE : un envoi abandonne
 * (telephone coupe du point d'acces, qui suit le canal de la box au retour) reste "en
 * cours" sans fin cote serveur web ; 20 s sans octet recu -> on redemarre quand meme
 * (la partition de demarrage ne change qu'une fois l'image verifiee).
 * La duree passee en mode secours survit au redemarrage logiciel (memoire RTC, pas de
 * flash) : le journal du demarrage suivant la donne. */
#define FALLBACK_MAGIC 0x57494649u   /* "WIFI" */
static RTC_NOINIT_ATTR uint32_t s_fb_magic;
static RTC_NOINIT_ATTR uint32_t s_fb_secs;
static int64_t s_fb_since_us;
static esp_timer_handle_t s_back_timer;
static void back_restart_now(void) {
    s_fb_secs = (uint32_t)((esp_timer_get_time() - s_fb_since_us) / 1000000);
    s_fb_magic = FALLBACK_MAGIC;
    ESP_LOGW(TAG, "Wi-Fi de retour apres %u s en mode secours : redemarrage", (unsigned)s_fb_secs);
    esp_restart();
}
static void back_restart_cb(void *arg) {
    (void)arg;
    static uint32_t last; static int still;   /* octets recus au passage precedent, passages sans progres */
    ota_status_t st = {0}; ota_get_status(&st);   /* {0} : rien n'est copie si le verrou ne vient pas */
    bool busy = st.state == OTA_STATE_RECEIVING || st.state == OTA_STATE_VERIFYING || st.state == OTA_STATE_APPLYING;
    if (busy && st.written_bytes != last) { last = st.written_bytes; still = 0; }
    if (busy && still++ < 4) {   /* toutes les 5 s, au plus 4 fois sans progres */
        esp_timer_start_once(s_back_timer, 5 * 1000000ULL);
        return;
    }
    back_restart_now();
}
static void on_wifi_back(void) {   /* appele depuis la tache d'evenements : on ne fait qu'armer */
    if (!s_back_timer) {
        const esp_timer_create_args_t ta = { .callback = back_restart_cb, .name = "wifi_back" };
        if (esp_timer_create(&ta, &s_back_timer) != ESP_OK) { back_restart_now(); return; }   /* sinon connecte sans MQTT */
    }
    if (!esp_timer_is_active(s_back_timer)) esp_timer_start_once(s_back_timer, 2 * 1000000ULL);
}

/* NB : le RX radio (capture permanente arbitree) est demarre par shutters_init() ;
 * les trames recues sont routees vers shutters_on_rx en interne. */

/* ────── App entry ────── */

void app_main(void) {
    log_ring_init();   /* journal de l'interface web : avant le premier message */
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ OpenProfalux v%s — target=" TARGET_NAME, esp_app_get_description()->version);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    if (s_fb_magic == FALLBACK_MAGIC && esp_reset_reason() == ESP_RST_SW)
        ESP_LOGW(TAG, "redemarre apres le retour du Wi-Fi (%u s en mode secours)", (unsigned)s_fb_secs);
    s_fb_magic = 0;

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

    /* 3. CC1101 */
    if (cc1101_init() != 0) {
        ESP_LOGE(TAG, "CC1101 init FAILED. Check wiring per hardware_config.h");
    }
    /* 4b. Auto-test emission au boot (prouve que la puce passe en TX). */
    if (cc1101_tx_selftest() == 0)
        ESP_LOGI(TAG, "TX SELFTEST OK : la puce emet (MARCSTATE=TX).");
    else
        ESP_LOGW(TAG, "TX SELFTEST ECHEC : puce NE passe PAS en TX (config/SPI).");
    cc1101_rx_probe();   /* sonde de bruit RX au boot (comparaison module/antenne) */

    /* 4c. Modele cover + arbitre radio (RX permanent gere en interne) */
    shutters_init();
    pfx_enrol_init();   /* identite virtuelle 0x067 (apprentissage sans telecommande, experimental) */
    if (s_log_frames) shutters_set_log_frames(true);
    cc1101_set_rx_debug(s_debug);   /* switch DEBUG console restaure au boot */
    cc1101_set_rx_gain(s_rx_gain);  /* plafond de gain RX restaure au boot */
    cc1101_set_tx_te(s_tx_te);      /* TE d'emission restaure au boot */

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
        if (strlen(s_wifi_ssid) > 0) {
            /* Reseau configure mais muet (box qui redemarre apres une coupure, ou panne) :
             * point d'acces ET client, qui continue de chercher le reseau (mode secours). */
            s_fb_since_us = esp_timer_get_time();
            wifi_bridge_start_fallback(ap_ssid, on_wifi_back);
        } else {
            ESP_LOGW(TAG, "Wi-Fi not connected. Starting SoftAP '%s' (open) for config.", ap_ssid);
            wifi_bridge_start_softap(ap_ssid, "");   /* mdp vide => WIFI_AUTH_OPEN (comme OpenXtraflamme) */
        }
    }

    /* 5b. mDNS (pour l'auto-decouverte du broker MQTT depuis l'UI) */
    if (wifi_bridge_is_connected()) {
        if (mdns_init() == ESP_OK) { mdns_hostname_set("openprofalux"); mdns_instance_name_set("OpenProfalux"); }
    }

    /* 5c. UI web embarquee (config Wi-Fi/MQTT, apprentissage, OTA...) */
    web_ui_start();

    /* 6. MQTT */
    if (wifi_bridge_is_connected() && strlen(s_mqtt_uri) > 0) {
        /* Handlers AVANT le start : la decouverte HA est publiee sur l'evenement CONNECTED
         * (vraie connexion), plus au boot a l'aveugle. Le statut UI suit CONNECTED/DISCONNECTED. */
        mqtt_handlers_t h = {
            .on_message = shutters_mqtt_on_message,   /* cover HA */
            .on_ota_pull = on_ota_pull,               /* OTA pull */
            .on_connected = on_mqtt_connected,        /* -> publie la decouverte HA */
            .on_disconnected = shutters_mqtt_lost,    /* -> statut hors ligne */
        };
        mqtt_bridge_set_handlers(&h);
        mqtt_bridge_start(s_mqtt_uri, s_device_name,
                          strlen(s_mqtt_user) ? s_mqtt_user : NULL,
                          strlen(s_mqtt_pass) ? s_mqtt_pass : NULL);
    }

    ESP_LOGI(TAG, "PRET (%s). Pilotage via HA/MQTT + UI web. Capture/replay actif.", TARGET_NAME);

    /* Boucle de fond : heartbeat MQTT (~60 s) + validation de l'image OTA apres ~60 s
     * (rollback safety). Le capture/replay + le suivi de position tournent dans le
     * module radio et la tache shutters. */
    uint32_t hb = 0; bool ota_marked = false;
    while (1) {
        if (++hb >= 1200) { hb = 0; publish_state("HEARTBEAT");
            if (!ota_marked) { ota_mark_valid(); ota_marked = true; }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
