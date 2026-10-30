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
#include <nvs.h>
#include "captive_dns.h"
#include <time.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "hardware_config.h"   /* BOARD_HAS_ETHERNET + brochage ETH (board_pins.h) */
#if BOARD_HAS_ETHERNET
#include <esp_eth.h>
#include <esp_eth_mac_esp.h>      /* eth_esp32_emac_config_t, ETH_ESP32_EMAC_DEFAULT_CONFIG */
#include <esp_eth_phy_lan87xx.h>  /* esp_eth_phy_new_lan87xx (composant espressif/lan87xx, IDF >= 6.1) */
#include <driver/gpio.h>
static esp_eth_handle_t s_eth = NULL;
static esp_netif_t     *s_eth_netif = NULL;
#endif

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

#if BOARD_HAS_ETHERNET
/* Lien Ethernet : le bit CONNECTED (partage avec le Wi-Fi) suit l'etat du cable. L'adresse
 * IP arrive via IP_EVENT_ETH_GOT_IP, traitee dans wifi_event_cb comme un STA_GOT_IP. */
static void eth_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet : lien physique etabli");
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Ethernet : cable debranche");
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}
#endif

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
    } else if (base == IP_EVENT && (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP)) {
        /* Le watchdog Wi-Fi relance la pile STA : inutile (et a risque) sur un lien Ethernet. */
        if (id == IP_EVENT_STA_GOT_IP && !s_wdt) xTaskCreate(wifi_watchdog_task, "wifi_wdt", 3072, NULL, 4, &s_wdt);
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

/* La pile Wi-Fi d'ESP-IDF garde par defaut sa propre copie de la config en NVS (espace
 * nvs.net80211) : ~85-100 entrees, un quart de la NVS de 16 Ko, pour rien, puisque le firmware
 * lui redonne SSID et mot de passe a chaque demarrage depuis cfg. Saturee, la NVS refusait la
 * calibration radio a chaque demarrage (store_cal_data, ESP_ERR_NVS_NOT_ENOUGH_SPACE). La pile
 * travaille donc en RAM (nvs_enable = 0) et l'ancienne copie est effacee : une seule fois en
 * pratique, ensuite l'espace est vide et rien n'est ecrit. */
static void wifi_nvs_purge(void) {
    nvs_handle_t h; size_t used = 0;
    if (nvs_open("nvs.net80211", NVS_READONLY, &h) != ESP_OK) return;   /* espace absent : rien a faire */
    nvs_get_used_entry_count(h, &used);
    nvs_close(h);
    if (!used || nvs_open("nvs.net80211", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK)
        ESP_LOGW(TAG, "NVS : copie de la config Wi-Fi par la pile effacee (%u entrees liberees)", (unsigned)used);
    nvs_close(h);
}

int wifi_bridge_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_events = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;   /* config Wi-Fi en RAM : le firmware la redonne a chaque demarrage (cfg) */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_nvs_purge();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_cb, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_cb, NULL, NULL));
#if BOARD_HAS_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, wifi_event_cb, NULL, NULL));
#endif
    return 0;
}

#if BOARD_HAS_ETHERNET
/* Reseau filaire (LAN8720 via RMII). Si le cable donne une adresse, le flux normal
 * (mDNS, MQTT, decouverte HA) continue comme en Wi-Fi ; sinon l'appelant retombe sur le STA. */
int wifi_bridge_start_eth(void) {
    /* Alim du PHY (Olimex : GPIO12 pilote l'alimentation du LAN8720). */
    if (ETH_PHY_POWER_GPIO >= 0) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << ETH_PHY_POWER_GPIO, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&io);
        gpio_set_level(ETH_PHY_POWER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);

    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num  = ETH_MDC_GPIO;
    emac_cfg.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    /* La carte POE n'a pas d'oscillateur externe : l'ESP32 genere l'horloge 50 MHz et la
     * sort sur ETH_CLK_GPIO (ETH_CLK_MODE lu du JSON de la carte, jamais en dur). En entree
     * aucune horloge n'arrive et le MAC ne se reset pas (emac_esp32_init: reset timeout). */
    emac_cfg.clock_config.rmii.clock_mode = ETH_CLK_MODE;
    emac_cfg.clock_config.rmii.clock_gpio = ETH_CLK_GPIO;
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = -1;   /* alim/reset gere ci-dessus */
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_cfg);

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&eth_cfg, &s_eth) != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet : installation du pilote echouee");
        return -1;
    }
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth)));
    esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_cb, NULL, NULL);
    ESP_ERROR_CHECK(esp_eth_start(s_eth));
    ESP_LOGI(TAG, "Ethernet demarre (PHY %s addr %d, MDC=%d MDIO=%d clk=%d)",
             ETH_PHY_MODEL, ETH_PHY_ADDR, ETH_MDC_GPIO, ETH_MDIO_GPIO, ETH_CLK_GPIO);
    return 0;
}
#endif

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

/* Portail captif du point d'acces (configuration et mode secours) : DNS qui renvoie tout vers le
 * boitier, et option DHCP 114 (RFC 8910) qui annonce l'adresse du portail. Le serveur web redirige
 * les adresses inconnues vers la page de configuration tant que le point d'acces est actif. */
static volatile bool s_ap_on = false;
bool wifi_bridge_ap_active(void) { return s_ap_on; }
static void captive_start(esp_netif_t *ap) {
    static const char uri[] = "http://192.168.4.1/";
    if (ap) {
        esp_netif_dhcps_stop(ap);
        esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)uri, sizeof(uri) - 1);
        esp_netif_dhcps_start(ap);
    }
    captive_dns_start();
    s_ap_on = true;
}

int wifi_bridge_start_softap(const char *ssid, const char *pass) {
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    wifi_config_t cfg = {0};
    strncpy((char*)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(ssid);
    strncpy((char*)cfg.ap.password, pass, sizeof(cfg.ap.password));
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = strlen(pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    captive_start(ap);
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
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
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
    captive_start(ap);
    esp_wifi_set_ps(WIFI_PS_NONE);       /* apres chaque start (voir wifi_bridge_start_sta) */
    ESP_LOGW(TAG, "mode secours : point d'acces '%s' (ouvert) + recherche de '%s'", ap_ssid, s_sta_ssid);
    return 0;
}
bool wifi_bridge_in_fallback(void) { return s_fallback; }
static char s_hostname[33] = "openprofalux";
void wifi_bridge_set_hostname(const char *host) { if (host && *host) strlcpy(s_hostname, host, sizeof(s_hostname)); }
const char *wifi_bridge_hostname(void) { return s_hostname; }
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
    esp_netif_t *netif = NULL;
#if BOARD_HAS_ETHERNET
    netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (netif && esp_netif_get_ip_info(netif, &info) == 0 && info.ip.addr) {
        snprintf(buf, len, IPSTR, IP2STR(&info.ip)); return;
    }
#endif
    netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &info) == 0) {
        snprintf(buf, len, IPSTR, IP2STR(&info.ip));
    } else strncpy(buf, "0.0.0.0", len);
}
