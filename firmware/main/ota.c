/**
 * OpenProfalux - OTA (repris d'OpenXtraflame, éprouvé au terrain).
 */
#include <string.h>
#include <stdlib.h>
#include "ota.h"
#include "hardware_config.h"   /* TARGET_NAME -> variante d'asset GitHub */
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "nvs.h"
#include "ota_rollback.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

_Static_assert(ORB_NEW == ESP_OTA_IMG_NEW && ORB_PENDING_VERIFY == ESP_OTA_IMG_PENDING_VERIFY &&
               ORB_VALID == ESP_OTA_IMG_VALID && ORB_INVALID == ESP_OTA_IMG_INVALID &&
               ORB_ABORTED == ESP_OTA_IMG_ABORTED && ORB_UNDEFINED == ESP_OTA_IMG_UNDEFINED,
               "ota_rollback.h doit reprendre les valeurs d'esp_ota_img_states_t");

static const char *TAG = "OTA";

static SemaphoreHandle_t ota_mutex;
static ota_status_t status;
static esp_ota_handle_t update_handle;
static const esp_partition_t *update_partition;

/* Retour automatique (voir ota_rollback.h) : 1 actif, 0 inactif, -1 inconnu. Retenu en NVS
 * (cfg/ota_rb) : on ne le voit qu'au premier demarrage d'une mise a jour. */
static int8_t s_rb_auto = -1;
static char s_discarded[32];          /* version de la derniere image ecartee ("" : aucune) */
static bool s_discarded_auto;         /* ecartee par le bootloader (elle n'a pas demarre) */
/* Bouton Version precedente : une version valide attend-elle dans l'autre partition ?
 * esp_ota_check_rollback_is_possible verifie TOUTE son image (~1,2 Mo relus) : calcule une fois
 * au demarrage, pas a chaque lecture de l'etat. Ne change qu'avec un redemarrage, ou une mise a
 * jour, qui efface l'autre partition (ota_upload_begin, ota_pull_from_url). */
static bool s_rb_possible;
static char s_previous[32];           /* sa version ("" : aucune) */

static void previous_load(void)
{
    esp_app_desc_t d;
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    s_rb_possible = esp_ota_check_rollback_is_possible();
    s_previous[0] = 0;
    if (s_rb_possible && other && esp_ota_get_partition_description(other, &d) == ESP_OK)
        strncpy(s_previous, d.version, sizeof(s_previous) - 1);
}

static void previous_erased(void) { s_rb_possible = false; s_previous[0] = 0; }

static bool running_state(uint32_t *st)
{
    esp_ota_img_states_t s;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) != ESP_OK) return false;
    *st = (uint32_t)s;
    return true;
}

static void validate_if_on_trial(const char *why)
{
    uint32_t st = ORB_UNDEFINED; bool known = running_state(&st);
    if (orb_validate_now(known, st) && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        ESP_LOGI(TAG, "version en essai validee (%s)", why);
}

/* esp_restart() appelle les fonctions d'arret, ce que ne font ni une panique ni une coupure :
 * un redemarrage VOULU (retour du Wi-Fi apres le mode secours, config enregistree, chien de
 * garde Wi-Fi) prouve que la version tourne. Sans cela, il ferait abandonner une bonne
 * version pendant sa minute d'essai. */
static void on_restart(void) { validate_if_on_trial("redemarrage voulu"); }

static void rb_auto_load_and_observe(bool known, uint32_t st)
{
    nvs_handle_t h; uint8_t v = 0xFF;
    int8_t stored = -1;
    if (nvs_open("cfg", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "ota_rb", &v) == ESP_OK && v <= 1) stored = (int8_t)v;
    s_rb_auto = orb_auto_observe(known, st, stored);
    if (s_rb_auto >= 0 && s_rb_auto != stored) { nvs_set_u8(h, "ota_rb", (uint8_t)s_rb_auto); nvs_commit(h); }
    nvs_close(h);
}

esp_err_t ota_init(void)
{
    ota_mutex = xSemaphoreCreateMutex();
    memset(&status, 0, sizeof(status));
    status.state = OTA_STATE_IDLE;
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc) strncpy(status.active_version, desc->version, sizeof(status.active_version) - 1);
    /* Ne PAS valider ici : main.c valide l'image apres ~60 s sans plantage (ota_mark_valid),
     * ou plus tot sur un redemarrage voulu ou une nouvelle mise a jour (ci-dessous). */
    uint32_t st = ORB_UNDEFINED; bool known = running_state(&st);
    rb_auto_load_and_observe(known, st);
    if (orb_on_trial(known, st))
        ESP_LOGW(TAG, "version en essai : si elle redemarre avant d'etre validee (~60 s), retour automatique a la precedente");
    else if (known && st == ORB_NEW)
        ESP_LOGW(TAG, "bootloader sans retour automatique (anterieur a l'option) : voir firmware/README.md");
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_ota_img_states_t bs; esp_app_desc_t bd;
    if (bad && esp_ota_get_state_partition(bad, &bs) == ESP_OK && esp_ota_get_partition_description(bad, &bd) == ESP_OK) {
        orb_discard_t k = orb_discarded(true, (uint32_t)bs);
        if (k != ORB_DISCARD_NONE) {
            strncpy(s_discarded, bd.version, sizeof(s_discarded) - 1);
            s_discarded_auto = (k == ORB_DISCARD_AUTO);
            if (s_discarded_auto) ESP_LOGW(TAG, "la mise a jour %s n'a pas demarre : retour automatique a la version precedente", s_discarded);
        }
    }
    previous_load();
    esp_register_shutdown_handler(on_restart);
    return ESP_OK;
}

void ota_rb_info(ota_rb_info_t *out)
{
    uint32_t st = ORB_UNDEFINED; bool known = running_state(&st);
    out->rb_auto = s_rb_auto;
    out->on_trial = orb_on_trial(known, st);
    strncpy(out->discarded, s_discarded, sizeof(out->discarded) - 1);
    out->discarded[sizeof(out->discarded) - 1] = 0;
    out->discarded_auto = s_discarded_auto;
    out->rb_possible = s_rb_possible;
    strncpy(out->previous, s_previous, sizeof(out->previous) - 1);
    out->previous[sizeof(out->previous) - 1] = 0;
}

bool ota_rollback_possible(void) { return esp_ota_check_rollback_is_possible(); }

void ota_get_status(ota_status_t *out)
{
    if (xSemaphoreTake(ota_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        memcpy(out, &status, sizeof(status));
        xSemaphoreGive(ota_mutex);
    }
}

esp_err_t ota_upload_begin(size_t total_bytes)
{
    /* En essai, esp_ota_begin refuse toute ecriture (ESP_ERR_OTA_ROLLBACK_INVALID_STATE) :
     * qui envoie une mise a jour a montre que la version actuelle tourne, on la valide. */
    validate_if_on_trial("nouvelle mise a jour");
    previous_erased();   /* esp_ota_begin efface l'autre partition */
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) return ESP_FAIL;
    esp_err_t err = esp_ota_begin(update_partition, total_bytes, &update_handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err)); return err; }
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_RECEIVING; status.total_bytes = total_bytes; status.written_bytes = 0;
    strncpy(status.message, "receiving", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    return ESP_OK;
}

esp_err_t ota_upload_data(const void *data, size_t len)
{
    esp_err_t err = esp_ota_write(update_handle, data, len);
    if (err != ESP_OK) return err;
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.written_bytes += len;
    xSemaphoreGive(ota_mutex);
    return ESP_OK;
}

esp_err_t ota_upload_end(void)
{
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_VERIFYING;
    strncpy(status.message, "verifying", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);

    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err)); goto fail; }
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_boot: %s", esp_err_to_name(err)); goto fail; }

    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_REBOOTING;
    strncpy(status.message, "rebooting", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    ESP_LOGI(TAG, "OTA ok, reboot 3s...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
fail:
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_FAILED;
    strncpy(status.message, esp_err_to_name(err), sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    return err;
}

esp_err_t ota_upload_abort(void)
{
    esp_ota_abort(update_handle);
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    status.state = OTA_STATE_IDLE;
    strncpy(status.message, "aborted", sizeof(status.message) - 1);
    xSemaphoreGive(ota_mutex);
    return ESP_OK;
}

esp_err_t ota_pull_from_url(const char *url)
{
    /* buffer_size par defaut (512) trop petit pour les en-tetes GitHub (302) + CDN
     * -> "HTTP_CLIENT: Out of buffer". On agrandit le buffer RX d'en-tetes. */
    esp_http_client_config_t http_cfg = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
                                          .timeout_ms = 15000, .keep_alive_enable = true,
                                          .buffer_size = 4096, .buffer_size_tx = 1024 };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    esp_https_ota_handle_t handle = NULL;
    validate_if_on_trial("nouvelle mise a jour");   /* comme ota_upload_begin */
    previous_erased();
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) { status.state = OTA_STATE_FAILED; return err; }
    status.state = OTA_STATE_RECEIVING;
    status.total_bytes = esp_https_ota_get_image_size(handle);
    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS)
        status.written_bytes = esp_https_ota_get_image_len_read(handle);
    if (!esp_https_ota_is_complete_data_received(handle)) { esp_https_ota_abort(handle); status.state = OTA_STATE_FAILED; return err; }
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) { status.state = OTA_STATE_FAILED; return err; }
    status.state = OTA_STATE_REBOOTING;
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

/* ── Check GitHub : derniere version + URL d'asset de la variante ── */
static char s_latest_ver[24] = "";
static char s_latest_url[192] = "";
const char *ota_latest_version(void) { return s_latest_ver; }
const char *ota_latest_url(void)     { return s_latest_url; }
static volatile uint32_t s_check_n = 0;   /* l'UI y voit la fin d'une verification demandee */
uint32_t ota_check_count(void)       { return s_check_n; }

esp_err_t ota_check_github(void)
{
    /* ⚠️ L'adresse doit être la définitive : cette fonction lit la réponse avec
     * esp_http_client_open + fetch_headers + read, qui NE SUIVENT PAS les redirections
     * (seul esp_http_client_perform le fait). Constaté le 2026-09-20 après le passage du
     * dépôt sous Isno-Open : l'ancienne adresse Shad107 répond 301 sans tag_name, la
     * vérification échouait en silence et l'entité de mise à jour côté HA ne voyait plus
     * jamais de nouvelle version. Le téléchargement lui-même (esp_https_ota) suit les
     * redirections, y compris celle de GitHub vers objects.githubusercontent.com. */
    esp_http_client_config_t cfg = {
        .url = "https://api.github.com/repos/Isno-Open/OpenProfalux/releases/latest",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000, .buffer_size = 4096, .buffer_size_tx = 1024,
        .user_agent = "OpenProfalux-OTA",   /* l'API GitHub exige un User-Agent */
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(c); s_check_n++; return err; }
    esp_http_client_fetch_headers(c);
    char *buf = malloc(6144);
    if (!buf) { esp_http_client_close(c); esp_http_client_cleanup(c); s_check_n++; return ESP_ERR_NO_MEM; }
    int total = 0, r;
    while (total < 6143 && (r = esp_http_client_read(c, buf + total, 6143 - total)) > 0) total += r;
    buf[total > 0 ? total : 0] = 0;
    esp_http_client_close(c); esp_http_client_cleanup(c);
    /* tag_name arrive tot dans le JSON -> recherche legere (pas de gros parse). URL par convention. */
    esp_err_t rc = ESP_FAIL;
    char *t = strstr(buf, "\"tag_name\":\"");
    if (t) {
        t += strlen("\"tag_name\":\"");
        char *e = strchr(t, '"');
        if (e && (e - t) < 20) {
            char tag[24]; size_t nn = (size_t)(e - t); memcpy(tag, t, nn); tag[nn] = 0;
            strlcpy(s_latest_ver, tag[0] == 'v' ? tag + 1 : tag, sizeof(s_latest_ver));
            /* Asset de CETTE carte : scripts/release.sh nomme chaque binaire
             * openprofalux-<carte>-ota.bin, <carte> etant l'id de boards/<carte>.json,
             * le meme que TARGET_NAME. L'ancienne table (m5stack_atom -> atom,
             * external -> devkit) datait d'avant ces fichiers : plus aucune carte n'y
             * correspondait, et la mise a jour depuis GitHub etait impossible. */
            snprintf(s_latest_url, sizeof(s_latest_url),
                "https://github.com/Isno-Open/OpenProfalux/releases/download/%s/openprofalux-%s-ota.bin", tag, TARGET_NAME);
            rc = ESP_OK;
        }
    }
    free(buf);
    ESP_LOGI(TAG, "GitHub latest=%s url=%s", s_latest_ver, s_latest_url[0] ? s_latest_url : "(?)");
    s_check_n++;
    return rc;
}

esp_err_t ota_rollback(void)   { return esp_ota_mark_app_invalid_rollback_and_reboot(); }
esp_err_t ota_mark_valid(void) { return esp_ota_mark_app_valid_cancel_rollback(); }
