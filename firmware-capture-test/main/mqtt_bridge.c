#include "mqtt_bridge.h"
#include "hardware_config.h"   /* TARGET_NAME, utilise ligne 130 */
#include <string.h>
#include <stdio.h>
#include <mqtt_client.h>
#include <esp_log.h>
#include <cJSON.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_mqtt = NULL;
static mqtt_handlers_t          s_hdl = {0};
static char s_client_id[32];

/* Topic base : set at start */
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

    /* Match pattern openprofalux/{device}/pair */
    if (strstr(t, TOPIC_BASE "/") == t) {
        const char *after = t + strlen(TOPIC_BASE "/");
        char device[64]; const char *slash = strchr(after, '/');
        if (!slash) return;
        int dlen2 = slash - after;
        if (dlen2 >= (int)sizeof(device)) return;
        memcpy(device, after, dlen2); device[dlen2] = 0;
        const char *cmd = slash + 1;

        if (strcmp(cmd, "pair") == 0 && s_hdl.on_pair) {
            ESP_LOGI(TAG, "-> pair(%s)", device);
            s_hdl.on_pair(device);
        } else if (strcmp(cmd, "reset") == 0 && s_hdl.on_reset) {
            s_hdl.on_reset(device);
        } else if (strcmp(cmd, "cmd") == 0 && s_hdl.on_cmd) {
            cJSON *j = cJSON_ParseWithLength(data, dlen);
            cJSON *btn = cJSON_GetObjectItem(j, "btn");
            if (cJSON_IsString(btn)) s_hdl.on_cmd(device, btn->valuestring);
            cJSON_Delete(j);
        }
    } else if (strcmp(t, TOPIC_BASE "/listen/start") == 0 && s_hdl.on_listen_start) {
        s_hdl.on_listen_start();
    } else if (strcmp(t, TOPIC_BASE "/listen/stop") == 0 && s_hdl.on_listen_stop) {
        s_hdl.on_listen_stop();
    }
}

static void mqtt_event_cb(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    (void)arg; (void)base;
    esp_mqtt_event_handle_t evt = event_data;
    switch (evt->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/+/pair", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/+/reset", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/+/cmd", 1);
            esp_mqtt_client_subscribe(s_mqtt, TOPIC_BASE "/listen/#", 1);
            mqtt_pub_system_status();
            publish_log("info", "MQTT connected");
            break;
        case MQTT_EVENT_DATA:
            handle_incoming(evt->topic, evt->topic_len, evt->data, evt->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
        default: break;
    }
}

int mqtt_bridge_start(const char *broker_uri, const char *client_id, const char *user, const char *pass) {
    strncpy(s_client_id, client_id, sizeof(s_client_id) - 1);
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_uri,
        .credentials.client_id = client_id,
        .credentials.username = user,
        .credentials.authentication.password = pass,
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_cb, NULL);
    esp_mqtt_client_start(s_mqtt);
    ESP_LOGI(TAG, "MQTT started, broker=%s", broker_uri);
    return 0;
}

void mqtt_bridge_stop(void) {
    if (s_mqtt) { esp_mqtt_client_stop(s_mqtt); esp_mqtt_client_destroy(s_mqtt); s_mqtt = NULL; }
}

int mqtt_bridge_set_handlers(const mqtt_handlers_t *h) { s_hdl = *h; return 0; }

int mqtt_pub_state(const char *device, const char *json) {
    if (!s_mqtt) return -1;
    char topic[64]; snprintf(topic, sizeof(topic), TOPIC_BASE "/%s/state", device);
    return esp_mqtt_client_publish(s_mqtt, topic, json, 0, 1, 1);
}

int mqtt_pub_pair_result(const char *device, bool ok, uint32_t frames) {
    if (!s_mqtt) return -1;
    char topic[64], payload[128];
    snprintf(topic, sizeof(topic), TOPIC_BASE "/%s/pair_result", device);
    snprintf(payload, sizeof(payload),
             "{\"result\":\"%s\",\"frames_sent\":%u,\"duration_ms\":60000}",
             ok ? "success" : "timeout", (unsigned)frames);
    return esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 0);
}

int mqtt_pub_rx_frame(const char *json) {
    if (!s_mqtt) return -1;
    return esp_mqtt_client_publish(s_mqtt, TOPIC_BASE "/listen/frame", json, 0, 0, 0);
}

int mqtt_pub_system_status(void) {
    if (!s_mqtt) return -1;
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"fw\":\"0.1.0-alpha\",\"target\":\"" TARGET_NAME "\",\"free_heap\":%u}",
             (unsigned)esp_get_free_heap_size());
    return esp_mqtt_client_publish(s_mqtt, TOPIC_BASE "/system/status", payload, 0, 1, 1);
}

int mqtt_ha_publish_discovery(const char *device_name) {
    if (!s_mqtt) return -1;
    char topic[128], payload[512];
    /* Cover entity */
    snprintf(topic, sizeof(topic), "homeassistant/cover/openprofalux_%s/config", device_name);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"OpenProfalux %s\","
        "\"unique_id\":\"openprofalux_%s\","
        "\"device_class\":\"shutter\","
        "\"command_topic\":\"" TOPIC_BASE "/%s/cmd\","
        "\"payload_open\":\"{\\\"btn\\\":\\\"UP\\\"}\","
        "\"payload_close\":\"{\\\"btn\\\":\\\"DOWN\\\"}\","
        "\"payload_stop\":\"{\\\"btn\\\":\\\"STOP\\\"}\"}",
        device_name, device_name, device_name);
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 1);
    /* Pair button */
    snprintf(topic, sizeof(topic), "homeassistant/button/openprofalux_%s_pair/config", device_name);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"Pair %s\","
        "\"unique_id\":\"openprofalux_%s_pair\","
        "\"command_topic\":\"" TOPIC_BASE "/%s/pair\"}",
        device_name, device_name, device_name);
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 1);
    return 0;
}
