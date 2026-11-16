/* Appairage cloud « a la Netflix » : voir cloud_grant.h. */
#include "cloud_grant.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "cloud_enroll.h"
#include "mqtt_bridge.h"
#include "tls_pool.h"   /* tour TLS : une poignee de main a la fois (voir tls_pool.h) */

static const char *TAG = "cloud_grant";

#define HTTP_TIMEOUT_MS   15000
#define RESP_MAX          8192    /* paquet d'enrolement : trois PEM, bien en dessous */
#define RESP_FIRST        1024    /* une reponse « pending » tient la : on ne grandit que pour le paquet */
#define POLL_MIN_S        5       /* jamais plus souvent : chaque interrogation est une poignee de main TLS */
#define GRANT_STACK       8192    /* client HTTP + TLS, comme ota_pull */
#define GRANT_PRIO        4
#define TURN_WAIT_MS      15000   /* au-dela, la requete part hors tour (comptee par tls_turn_misses) */

static const char *const STATE_WORD[] = {
    [GRANT_IDLE] = "idle", [GRANT_PENDING] = "pending", [GRANT_READY] = "ready",
    [GRANT_EXPIRED] = "expired", [GRANT_DENIED] = "denied", [GRANT_ERROR] = "error",
};

static struct {
    grant_state_t state;
    char server[160], device_code[96], user_code[24], uri[220], device_id[40];
    int interval_s;
    int64_t deadline_us;
    unsigned gen;        /* numero de l'appairage : une tache d'un ancien appairage s'arrete d'elle-meme */
} s_g;
static SemaphoreHandle_t s_lock;

static void lock(void)   { if (!s_lock) s_lock = xSemaphoreCreateMutex(); xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static int http_post(const char *url, const char *payload, char **resp);
static const char *jstr(cJSON *o, const char *k) { const cJSON *v = cJSON_GetObjectItem(o, k); return cJSON_IsString(v) ? v->valuestring : NULL; }

/* Toute la requete (poignee de main, envoi, reponse) se fait pendant le tour TLS : sinon elle
 * croisait la reconnexion du cloud, deux poignees de main a la fois, et la memoire tombait a 360 o
 * (mesure du 2026-11-15 sur l'ATOM). */
int cloud_http_post(const char *url, const char *payload, char **resp) {
    bool turn = tls_turn_take(TURN_WAIT_MS);
    int status = http_post(url, payload, resp);
    if (turn) tls_turn_give();
    return status;
}

static int http_post(const char *url, const char *payload, char **resp) {
    *resp = NULL;
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = HTTP_TIMEOUT_MS };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return 0;
    esp_http_client_set_header(c, "Content-Type", "application/json");
    int status = 0;
    if (esp_http_client_open(c, (int)strlen(payload)) == ESP_OK && esp_http_client_write(c, payload, (int)strlen(payload)) >= 0) {
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        /* tampon qui grandit : chaque interrogation « pending » ne prend qu'1 Ko (mesure du
         * 2026-11-15 : 8 Ko d'office a chaque tour, la memoire tombait a 16 o avec le cloud actif) */
        size_t cap = RESP_FIRST;
        char *b = malloc(cap);
        int total = 0, rd;
        while (b) {
            if ((size_t)total == cap - 1) {
                if (cap >= RESP_MAX) break;
                char *nb = realloc(b, cap * 2);
                if (!nb) { free(b); b = NULL; break; }
                b = nb; cap *= 2;
            }
            rd = esp_http_client_read(c, b + total, (int)(cap - 1 - (size_t)total));
            if (rd <= 0) break;
            total += rd;
        }
        if (b) { b[total] = 0; *resp = b; }
    }
    esp_http_client_close(c); esp_http_client_cleanup(c);
    return status;
}

bool cloud_apply_bundle(cJSON *b, char *dev, size_t dev_len) {
    const char *d = jstr(b, "device_id"), *broker = jstr(b, "broker_url");
    const char *ca = jstr(b, "ca"), *cert = jstr(b, "cert"), *key = jstr(b, "key");
    if (!cloud_bundle_ok(d, broker, ca, cert, key)) return false;
    mqtt_cert_write("cloud_ca", ca);
    mqtt_cert_write("cloud_cert", cert);
    mqtt_cert_write("cloud_key", key);
    char brob[160];
    snprintf(dev, dev_len, "%s", d);
    snprintf(brob, sizeof brob, "%s", broker);
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "cloud_uri", brob);
        nvs_set_str(h, "cloud_dev", dev);
        nvs_set_u8(h, "cloud_on", 1);
        nvs_erase_key(h, CLOUD_NAME_NVS_KEY);   /* nouvelle identite : le nom viendra du portail */
        nvs_commit(h); nvs_close(h);
    }
    mqtt_cloud_request_start(brob, dev);   /* hors du serveur web (voir mqtt_bridge.c) */
    return true;
}

/* Fin d'appairage (sous verrou) : seulement si c'est encore le meme. */
static void finish(unsigned gen, grant_state_t st) {
    lock();
    if (s_g.gen == gen) { s_g.state = st; s_g.device_code[0] = 0; }
    unlock();
}

static void poll_task(void *arg) {
    unsigned gen = (unsigned)(uintptr_t)arg;
    char url[200], payload[140], code[96];
    for (;;) {
        lock();
        bool mine = s_g.gen == gen && s_g.state == GRANT_PENDING;
        int wait = s_g.interval_s;
        bool late = esp_timer_get_time() > s_g.deadline_us;
        snprintf(url, sizeof url, "%s/api/device/poll", s_g.server);
        snprintf(code, sizeof code, "%s", s_g.device_code);
        unlock();
        if (!mine) break;
        if (late) { finish(gen, GRANT_EXPIRED); break; }
        /* cloud reconnecte entre-temps (reactive sur le boitier) : l'appairage n'a plus lieu d'etre, et
         * sa liaison TLS plus nos interrogations epuisaient la memoire (3,3 Ko au plus bas, 2026-11-15) */
        if (mqtt_cloud_is_connected()) { finish(gen, GRANT_IDLE); ESP_LOGI(TAG, "cloud connecte : appairage par QR abandonne"); break; }
        vTaskDelay(pdMS_TO_TICKS(wait * 1000));
        snprintf(payload, sizeof payload, "{\"device_code\":\"%s\"}", code);
        /* le tour TLS d'abord, PUIS l'etat du cloud : pendant l'attente du tour, le cloud a pu se
         * reconnecter, et une interrogation lancee juste apres tombait en pleine resynchronisation
         * (memoire a 1,9 Ko au plus bas, contre 17 Ko sans appairage, 2026-11-15) */
        bool turn = tls_turn_take(TURN_WAIT_MS);
        if (mqtt_cloud_is_connected()) {
            if (turn) tls_turn_give();
            finish(gen, GRANT_IDLE); ESP_LOGI(TAG, "cloud connecte : appairage par QR abandonne"); break;
        }
        char *resp = NULL;
        int status = http_post(url, payload, &resp);
        if (turn) tls_turn_give();
        cJSON *j = resp ? cJSON_Parse(resp) : NULL;
        free(resp);
        const char *st = j ? jstr(j, "status") : NULL;
        if (status == 410) { finish(gen, GRANT_EXPIRED); cJSON_Delete(j); break; }
        if (status == 409) { finish(gen, GRANT_DENIED); cJSON_Delete(j); break; }
        if (status == 200 && st && !strcmp(st, "ready")) {
            char dev[40];
            bool ok = cloud_apply_bundle(j, dev, sizeof dev);
            lock();
            if (s_g.gen == gen) { s_g.state = ok ? GRANT_READY : GRANT_ERROR; s_g.device_code[0] = 0; if (ok) snprintf(s_g.device_id, sizeof s_g.device_id, "%s", dev); }
            unlock();
            ESP_LOGI(TAG, "appairage confirme sur le portail : %s", ok ? dev : "paquet incomplet");
            cJSON_Delete(j);
            break;
        }
        /* pending, ou portail momentanement injoignable : on reessaie jusqu'a l'echeance */
        cJSON_Delete(j);
    }
    vTaskDelete(NULL);
}

int cloud_grant_start(const char *server, char *out, size_t len) {
    char url[200], *resp = NULL;
    snprintf(url, sizeof url, "%s/api/device/start", server);
    int status = cloud_http_post(url, "{\"firmware\":\"" CLOUD_FAMILY "\"}", &resp);
    cJSON *j = (status == 200 && resp) ? cJSON_Parse(resp) : NULL;
    free(resp);
    const char *dc = j ? jstr(j, "device_code") : NULL, *uc = j ? jstr(j, "user_code") : NULL;
    const char *uri = j ? jstr(j, "verification_uri_complete") : NULL;
    const cJSON *iv = j ? cJSON_GetObjectItem(j, "interval") : NULL, *ex = j ? cJSON_GetObjectItem(j, "expires_in") : NULL;
    if (!dc || !uc || !uri || !cJSON_IsNumber(ex)) { cJSON_Delete(j); return status ? status : -1; }
    lock();
    s_g.gen++;
    unsigned gen = s_g.gen;
    s_g.state = GRANT_PENDING;
    snprintf(s_g.server, sizeof s_g.server, "%s", server);
    snprintf(s_g.device_code, sizeof s_g.device_code, "%s", dc);
    snprintf(s_g.user_code, sizeof s_g.user_code, "%s", uc);
    snprintf(s_g.uri, sizeof s_g.uri, "%s", uri);
    s_g.device_id[0] = 0;
    s_g.interval_s = (cJSON_IsNumber(iv) && iv->valueint > POLL_MIN_S) ? iv->valueint : POLL_MIN_S;
    s_g.deadline_us = esp_timer_get_time() + (int64_t)ex->valueint * 1000000;
    unlock();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "user_code", uc);
    cJSON_AddStringToObject(o, "uri", uri);
    cJSON_AddNumberToObject(o, "expires_in", ex->valueint);
    cJSON_Delete(j);
    if (!cJSON_PrintPreallocated(o, out, (int)len, false)) snprintf(out, len, "{}");
    cJSON_Delete(o);
    if (xTaskCreate(poll_task, "cloud_grant", GRANT_STACK, (void *)(uintptr_t)gen, GRANT_PRIO, NULL) != pdPASS) {
        finish(gen, GRANT_ERROR);
        return -1;
    }
    ESP_LOGI(TAG, "appairage par QR : code %s, en attente de confirmation", s_g.user_code);
    return 0;
}

void cloud_grant_status(char *out, size_t len) {
    lock();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", STATE_WORD[s_g.state]);
    if (s_g.state == GRANT_PENDING) { cJSON_AddStringToObject(o, "user_code", s_g.user_code); cJSON_AddStringToObject(o, "uri", s_g.uri); }
    if (s_g.state == GRANT_READY) cJSON_AddStringToObject(o, "device_id", s_g.device_id);
    unlock();
    if (!cJSON_PrintPreallocated(o, out, (int)len, false)) snprintf(out, len, "{\"state\":\"error\"}");
    cJSON_Delete(o);
}
