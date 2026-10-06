#include "mqtt_bridge.h"
#include "hardware_config.h"   /* TARGET_NAME, utilise ligne 130 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <mqtt_client.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <nvs.h>
#include <cJSON.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_mqtt = NULL;
static mqtt_handlers_t          s_hdl = {0};
static char s_client_id[32];
static char s_avail_topic[64];   /* topic de disponibilite HA (LWT) : openprofalux/<device>/status */

/* Certificats TLS charges sur le tas quand mqtts:// est actif, gardes vivants le temps de
 * la connexion (esp-mqtt peut les referencer), liberes au stop. NULL = absent. */
static char *s_tls_ca = NULL, *s_tls_cert = NULL, *s_tls_key = NULL;

/* Topic base, set at start */
#define TOPIC_BASE "openprofalux"

static void publish_log(const char *level, const char *msg) {
    if (!s_mqtt) return;
    char topic[64], payload[512];
    snprintf(topic, sizeof(topic), TOPIC_BASE "/log");
    snprintf(payload, sizeof(payload),
             "{\"lvl\":\"%s\",\"msg\":\"%s\",\"client\":\"%s\"}",
             level, msg, s_client_id);
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 0, 0);
}

/* Route incoming message to handlers based on topic */
static void handle_incoming(const char *topic, int tlen, const char *data, int dlen) {
    char t[128]; int n = tlen < 127 ? tlen : 127;
    memcpy(t, topic, n); t[n] = 0;
    ESP_LOGI(TAG, "RX topic=%s len=%d", t, dlen);
    publish_log("info", t);

    /* Cover HA + switch "Ecoute RF permanente" + repeuplement frames/log -> handler shutters */
    if (strncmp(t, TOPIC_BASE "/cover/", strlen(TOPIC_BASE "/cover/")) == 0
        || strncmp(t, TOPIC_BASE "/frames/log/", strlen(TOPIC_BASE "/frames/log/")) == 0
        || strncmp(t, TOPIC_BASE "/listen/", strlen(TOPIC_BASE "/listen/")) == 0   /* set + state (restauration au boot) */
        || strncmp(t, TOPIC_BASE "/update/", strlen(TOPIC_BASE "/update/")) == 0) {  /* install MAJ via HA */
        if (s_hdl.on_message) s_hdl.on_message(t, data, dlen);
        return;
    }
    /* OTA pull : payload = URL du .bin */
    if (strcmp(t, TOPIC_BASE "/ota/pull") == 0 && s_hdl.on_ota_pull) {
        char url[256]; int un = dlen < 255 ? dlen : 255;
        memcpy(url, data, un); url[un] = 0;
        s_hdl.on_ota_pull(url);
        return;
    }
}

static void mqtt_event_cb(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    (void)arg; (void)base;
    esp_mqtt_event_handle_t evt = event_data;
    switch (evt->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            esp_mqtt_client_publish(s_mqtt, s_avail_topic, "online", 0, 1, 1);   /* disponibilite HA */
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/listen/#", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/cover/+/set", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/cover/+/set_position", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/ota/pull", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/update/install", 1);   /* entite update HA */
            /* PAS d'abonnement a frames/log/# : le ring est deja persiste en SPIFFS (load_ring au boot).
             * S'y abonner = l'ESP s'auto-inonde de ses propres trames retained (jeu qui grossit sans fin)
             * au boot -> la tache MQTT traite ce flot en prenant le LOCK pendant que la discovery publie
             * sous le LOCK -> DEADLOCK au demarrage. Le repeuplement MQTT etait redondant avec SPIFFS. */
            mqtt_pub_system_status();
            publish_log("info", "MQTT connected");
            if (s_hdl.on_connected) s_hdl.on_connected();   /* -> publie la decouverte HA (vraie connexion) */
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            if (s_hdl.on_disconnected) s_hdl.on_disconnected();
            break;
        case MQTT_EVENT_DATA:
            handle_incoming(evt->topic, evt->topic_len, evt->data, evt->data_len);
            break;
        case MQTT_EVENT_ERROR:
            /* Sur un echec de transport/TLS, on remonte la cause exacte du handshake :
             * esp_tls_cert_verify_flags (certificat refuse) et esp_tls_stack_err (mbedTLS). */
            if (evt->error_handle && evt->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
                ESP_LOGE(TAG, "MQTT erreur transport/TLS : tls_stack=0x%x cert_flags=0x%08x sock_errno=%d",
                         (unsigned)evt->error_handle->esp_tls_stack_err,
                         (unsigned)evt->error_handle->esp_tls_cert_verify_flags,
                         evt->error_handle->esp_transport_sock_errno);
            else
                ESP_LOGE(TAG, "MQTT error");
            break;
        default: break;
    }
}

/* ── Certificats TLS en NVS (namespace "mqtt_tls") ──
 * La config MQTT (URI, user, mot de passe) vit deja en NVS ; les certificats l'y rejoignent,
 * sans nouvelle partition, donc compatible OTA. Stockes en blob AVEC le \0 final (esp-mqtt
 * exige un PEM termine par \0). Plafond par certificat (MQTT_CERT_MAX) pour ne pas noyer la
 * config des volets dans la nvs de 16 Ko : au-dela, l'ecriture est refusee proprement. */
#define MQTT_TLS_NS   "mqtt_tls"
#define MQTT_CERT_MAX 2048          /* octets max par certificat PEM, \0 compris */

static const char *cert_key(const char *which) {
    if (!strcmp(which, "ca"))   return "ca";
    if (!strcmp(which, "cert")) return "cert";
    if (!strcmp(which, "key"))  return "key";
    return NULL;
}
/* Lit un certificat PEM sur le tas, termine par \0 (exige par esp-mqtt). NULL si absent,
 * vide ou erreur : l'appelant retombe alors sur le bundle (CA) ou coupe le TLS mutuel. */
static char *read_cert_nvs(const char *which) {
    const char *key = cert_key(which);
    if (!key) return NULL;
    nvs_handle_t h;
    if (nvs_open(MQTT_TLS_NS, NVS_READONLY, &h) != ESP_OK) return NULL;
    size_t n = 0;
    char *buf = NULL;
    if (nvs_get_blob(h, key, NULL, &n) == ESP_OK && n > 1 && n <= MQTT_CERT_MAX) {
        buf = malloc(n);
        if (buf && nvs_get_blob(h, key, buf, &n) != ESP_OK) { free(buf); buf = NULL; }
    }
    nvs_close(h);
    if (buf) buf[n - 1] = 0;   /* garantit le \0 final */
    return buf;
}
/* Hote de l'URI = IPv4 litterale ? Le CN d'un certificat ne peut pas correspondre a une
 * IP, on n'echoue donc pas sur le nom (skip_cert_common_name_check) dans ce cas. */
static bool host_is_ipv4(const char *uri) {
    const char *h = strstr(uri, "://");
    h = h ? h + 3 : uri;
    int groups = 0, digits = 0;
    for (; *h && *h != ':' && *h != '/'; h++) {
        if (*h == '.')      { if (!digits) return false; groups++; digits = 0; }
        else if (isdigit((unsigned char)*h)) digits++;
        else return false;
    }
    return groups == 3 && digits > 0;
}
int mqtt_cert_write(const char *which, const char *pem) {
    const char *key = cert_key(which);
    if (!key) return -1;
    size_t len = pem ? strlen(pem) : 0;
    nvs_handle_t h;
    if (nvs_open(MQTT_TLS_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t e;
    if (len == 0) {                                   /* vide = efface le certificat */
        e = nvs_erase_key(h, key);
        if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;   /* deja absent = OK */
    } else if (len + 1 > MQTT_CERT_MAX) {             /* trop gros : refuse */
        nvs_close(h);
        return -2;
    } else {
        e = nvs_set_blob(h, key, pem, len + 1);       /* stocke le PEM avec son \0 */
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}
size_t mqtt_cert_len(const char *which) {
    const char *key = cert_key(which);
    if (!key) return 0;
    nvs_handle_t h;
    if (nvs_open(MQTT_TLS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t n = 0;
    if (nvs_get_blob(h, key, NULL, &n) != ESP_OK) n = 0;
    nvs_close(h);
    return n > 1 ? n - 1 : 0;   /* longueur du PEM sans le \0 */
}
/* Libere les certificats charges (au stop ou avant un rechargement). */
static void free_tls_certs(void) {
    free(s_tls_ca);   s_tls_ca   = NULL;
    free(s_tls_cert); s_tls_cert = NULL;
    free(s_tls_key);  s_tls_key  = NULL;
}

int mqtt_bridge_start(const char *broker_uri, const char *client_id, const char *user, const char *pass) {
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
    snprintf(s_avail_topic, sizeof(s_avail_topic), TOPIC_BASE "/%s/status", client_id);
    /* Le client MQTT exige une URI avec schema (mqtt://host[:port]). Si l'utilisateur a saisi
     * une IP nue, on prefixe mqtt:// automatiquement (sinon "Error parse uri" -> jamais connecte). */
    static char s_uri[160];
    if (broker_uri && *broker_uri && !strstr(broker_uri, "://"))
        snprintf(s_uri, sizeof(s_uri), "mqtt://%s", broker_uri);
    else
        snprintf(s_uri, sizeof(s_uri), "%s", broker_uri ? broker_uri : "");
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_uri,
        .credentials.client_id = client_id,
        .credentials.username = user,
        .credentials.authentication.password = pass,
        /* Last Will : le broker publie "offline" (retain) si le boitier tombe sans se
         * deconnecter proprement -> HA grise les entites. On republie "online" a la connexion. */
        .session.last_will.topic = s_avail_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 7,
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
    };
    /* TLS (mqtts://) : on verifie le broker et, si fournis, on presente un certificat
     * client (TLS mutuel). Les PEM vivent en NVS, charges ici sur le tas et gardes
     * vivants jusqu'au stop (esp-mqtt peut les referencer apres l'init). */
    free_tls_certs();
    if (strncmp(s_uri, "mqtts://", 8) == 0) {
        s_tls_ca   = read_cert_nvs("ca");
        s_tls_cert = read_cert_nvs("cert");
        s_tls_key  = read_cert_nvs("key");
        if (s_tls_ca) cfg.broker.verification.certificate = s_tls_ca;          /* autorite du broker */
        else          cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;  /* broker public */
        cfg.broker.verification.skip_cert_common_name_check = host_is_ipv4(s_uri);
        if (s_tls_cert && s_tls_key) {   /* TLS mutuel : certificat + cle du client */
            cfg.credentials.authentication.certificate = s_tls_cert;
            cfg.credentials.authentication.key         = s_tls_key;
        }
        ESP_LOGI(TAG, "TLS actif : CA=%s, cert client=%s, skip_cn=%d",
                 s_tls_ca ? "fourni" : "bundle", (s_tls_cert && s_tls_key) ? "oui" : "non",
                 (int)host_is_ipv4(s_uri));
    }
    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_cb, NULL);
    esp_mqtt_client_start(s_mqtt);
    ESP_LOGI(TAG, "MQTT started, broker=%s", broker_uri);
    return 0;
}

void mqtt_bridge_stop(void) {
    if (s_mqtt) { esp_mqtt_client_stop(s_mqtt); esp_mqtt_client_destroy(s_mqtt); s_mqtt = NULL; }
    free_tls_certs();
}

int mqtt_bridge_set_handlers(const mqtt_handlers_t *h) { s_hdl = *h; return 0; }

int mqtt_pub_state(const char *device, const char *json) {
    if (!s_mqtt) return -1;
    char topic[64]; snprintf(topic, sizeof(topic), TOPIC_BASE "/%s/state", device);
    return esp_mqtt_client_publish(s_mqtt, topic, json, 0, 1, 1);
}

int mqtt_pub_raw(const char *topic, const char *payload, int qos, int retain) {
    if (!s_mqtt) return -1;
    return esp_mqtt_client_publish(s_mqtt, topic, payload, 0, qos, retain);
}

int mqtt_pub_system_status(void) {
    if (!s_mqtt) return -1;
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"fw\":\"%s\",\"target\":\"" TARGET_NAME "\",\"free_heap\":%u}",
             esp_app_get_description()->version, (unsigned)esp_get_free_heap_size());
    return esp_mqtt_client_publish(s_mqtt, TOPIC_BASE "/system/status", payload, 0, 1, 1);
}

