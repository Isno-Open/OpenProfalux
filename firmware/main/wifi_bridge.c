#include "wifi_bridge.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <time.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* ── Mode secours : point d'acces ET client (voir wifi_bridge_start_fallback) ──
 * Quand le reseau enregistre ne repond pas au demarrage (box encore en train de demarrer
 * apres une coupure, ou injoignable), le boitier ouvre son point d'acces de configuration
 * SANS eteindre le client : il continue de chercher le reseau, et rend la main (on_back)
 * des qu'il en obtient une adresse IP. Avant, le point d'acces seul arretait le client pour
 * toujours : le boitier restait injoignable depuis Home Assistant jusqu'a un redemarrage
 * manuel.
 * Un essai balaie les canaux et perturbe le point d'acces : 30 s entre deux essais, 5 min
 * tant qu'un appareil y est connecte. Pas de suspension : un telephone peut s'y reconnecter
 * tout seul (reseau ouvert deja connu) et y rester, le retour serait alors bloque. */
#define FALLBACK_RETRY_MS       30000
#define FALLBACK_RETRY_BUSY_MS 300000
static volatile bool s_fallback = false;
static volatile int  s_ap_clients = 0;
static esp_timer_handle_t s_retry_timer = NULL;
static void (*s_on_back)(void) = NULL;
static uint8_t s_last_reason = 0;
static char s_sta_ssid[33] = "";

static void fallback_retry_cb(void *arg) {
    (void)arg;
    if (!wifi_bridge_is_connected()) esp_wifi_connect();
}
static void fallback_schedule(void) {
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)(s_ap_clients > 0 ? FALLBACK_RETRY_BUSY_MS : FALLBACK_RETRY_MS) * 1000);
}
static const char *reason_text(uint8_t r) {
    switch (r) {
        case WIFI_REASON_NO_AP_FOUND:            return "reseau introuvable";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "connexion refusee (mot de passe ?)";
        default:                                 return "echec de connexion";
    }
}

/* Watchdog WiFi : si la STA reste coupee, relance la pile toute seule (plus besoin de
 * debrancher). Demarre APRES la 1re connexion (pas en mode SoftAP-config). */
static TaskHandle_t s_wdt = NULL;
static void wifi_watchdog_task(void *arg) {
    (void)arg; int down_s = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (wifi_bridge_is_connected()) { down_s = 0; continue; }
        down_s += 10;
        if (down_s == 120) {   /* 2 min coupe -> relance propre de la pile (re-declenche STA_START->connect) */
            ESP_LOGW(TAG, "WiFi coupe depuis 2 min -> restart pile WiFi");
            esp_wifi_stop(); vTaskDelay(pdMS_TO_TICKS(500)); esp_wifi_start();
            esp_wifi_set_ps(WIFI_PS_NONE);   /* apres chaque start (voir wifi_bridge_start_sta) */
        } else if (down_s >= 300) {   /* 5 min -> reboot en DERNIER recours (recupere tout) */
            ESP_LOGE(TAG, "WiFi coupe depuis 5 min -> reboot de recuperation");
            esp_restart();
        }
    }
}

static void wifi_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_fallback && s_retry_timer) {
            /* Mode secours : essais espaces ; la raison n'est ecrite que quand elle change
             * (le journal de l'interface ne fait que 8 Ko). ASSOC_LEAVE = notre propre
             * esp_wifi_stop() au passage en mode secours, pas un refus du reseau. */
            uint8_t r = ((wifi_event_sta_disconnected_t *)data)->reason;
            if (r != s_last_reason && r != WIFI_REASON_ASSOC_LEAVE) {
                s_last_reason = r;
                ESP_LOGW(TAG, "mode secours : '%s' %s (raison %u), nouvel essai toutes les %d s",
                         s_sta_ssid, reason_text(r), r, FALLBACK_RETRY_MS / 1000);
            }
            fallback_schedule();
        } else {
            ESP_LOGW(TAG, "Disconnected, retry");
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_ap_clients++;
        if (s_fallback && s_retry_timer && esp_timer_is_active(s_retry_timer)) fallback_schedule();   /* -> 5 min */
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_ap_clients > 0) s_ap_clients--;
        if (s_fallback && s_retry_timer && s_ap_clients == 0 && esp_timer_is_active(s_retry_timer)) fallback_schedule();   /* -> 30 s */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (!s_wdt) xTaskCreate(wifi_watchdog_task, "wifi_wdt", 3072, NULL, 4, &s_wdt);   /* arme le watchdog a la 1re connexion */
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        /* Mode secours : le reseau repond ET a donne une adresse (une box qui redemarre
         * emet souvent son Wi-Fi avant de distribuer des adresses). L'appelant decide. */
        if (s_fallback && s_on_back) s_on_back();
        /* Horloge reelle (SNTP) : date correctement les trames RF, meme apres reboot. Une seule init. */
        static bool s_sntp = false;
        if (!s_sntp) {
            s_sntp = true;
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();   /* Europe/Paris (heure d'ete auto) */
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
        }
    }
}

int wifi_bridge_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_events = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_cb, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_cb, NULL, NULL));
    return 0;
}

int wifi_bridge_start_sta(const char *ssid, const char *pass) {
    esp_netif_create_default_wifi_sta();
    wifi_config_t cfg = {0};
    strncpy((char*)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strncpy((char*)cfg.sta.password, pass, sizeof(cfg.sta.password));
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* ROOT CAUSE instabilite : le power-save par defaut (WIFI_PS_MIN_MODEM) endort le modem
     * -> l'AP casse l'agregation (block-ack "delba code:39" = timeout) et le lien meurt
     * SILENCIEUSEMENT (pas de STA_DISCONNECTED) -> pas de reconnexion -> MQTT/HTTP morts jusqu'au
     * reboot. On desactive le modem-sleep : lien stable. (Deja fait ponctuellement pendant l'OTA.) */
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "STA started, SSID=%s (power-save OFF)", ssid);
    return 0;
}

int wifi_bridge_start_softap(const char *ssid, const char *pass) {
    esp_netif_create_default_wifi_ap();
    wifi_config_t cfg = {0};
    strncpy((char*)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(ssid);
    strncpy((char*)cfg.ap.password, pass, sizeof(cfg.ap.password));
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = strlen(pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP started, SSID=%s", ssid);
    return 0;
}

/* Point d'acces de configuration (ouvert, comme wifi_bridge_start_softap) AVEC le client,
 * qui garde la config posee par wifi_bridge_start_sta et continue de chercher le reseau. */
int wifi_bridge_start_fallback(const char *ap_ssid, void (*on_back)(void)) {
    s_on_back = on_back;
    if (!s_retry_timer) {
        const esp_timer_create_args_t ta = { .callback = fallback_retry_cb, .name = "wifi_retry" };
        if (esp_timer_create(&ta, &s_retry_timer) != ESP_OK) s_retry_timer = NULL;   /* -> essais immediats */
    }
    esp_netif_create_default_wifi_ap();
    wifi_config_t cfg = {0};
    strncpy((char*)cfg.ap.ssid, ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(ap_ssid);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_OPEN;
    s_fallback = true; s_ap_clients = 0; s_last_reason = 0;
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());   /* STA_START -> premier essai aussitot */
    esp_wifi_set_ps(WIFI_PS_NONE);       /* apres chaque start (voir wifi_bridge_start_sta) */
    ESP_LOGW(TAG, "mode secours : point d'acces '%s' (ouvert) + recherche de '%s'", ap_ssid, s_sta_ssid);
    return 0;
}
bool wifi_bridge_in_fallback(void) { return s_fallback; }
bool wifi_bridge_is_connected(void) {
    EventBits_t bits = xEventGroupGetBits(s_wifi_events);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

int wifi_bridge_rssi(void) {
    wifi_ap_record_t ap;   /* pas d'appel au pilote hors connexion (voir shutters_status_json) */
    return wifi_bridge_is_connected() && esp_wifi_sta_get_ap_info(&ap) == 0 ? ap.rssi : 0;
}

void wifi_bridge_get_ip(char *buf, int len) {
    esp_netif_ip_info_t info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &info) == 0) {
        snprintf(buf, len, IPSTR, IP2STR(&info.ip));
    } else strncpy(buf, "0.0.0.0", len);
}
