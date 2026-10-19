/**
 * OpenProfalux - OTA firmware update (repris d'OpenXtraflame, éprouvé).
 *
 * Écrit le firmware entrant sur la partition OTA inactive puis bascule le boot.
 * Retour automatique a la version precedente si la nouvelle redemarre avant d'etre validee,
 * quand le bootloader a l'option (voir ota_rollback.h et firmware/README.md).
 *  - HTTP POST /api/ota/upload : upload binaire direct depuis l'UI
 *  - URL HTTPS : ota_pull_from_url()
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    OTA_STATE_IDLE      = 0,
    OTA_STATE_RECEIVING = 1,
    OTA_STATE_VERIFYING = 2,
    OTA_STATE_APPLYING  = 3,
    OTA_STATE_REBOOTING = 4,
    OTA_STATE_FAILED    = 99,
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t    total_bytes;
    uint32_t    written_bytes;
    char        message[128];
    char        active_version[64];
    char        pending_version[64];
} ota_status_t;

esp_err_t ota_init(void);
void      ota_get_status(ota_status_t *out);
esp_err_t ota_upload_begin(size_t total_bytes);
esp_err_t ota_upload_data(const void *data, size_t len);
esp_err_t ota_upload_end(void);
esp_err_t ota_upload_abort(void);
esp_err_t ota_pull_from_url(const char *url);   /* telecharge + flashe une image HTTPS (release GitHub) */

/* Interroge GitHub (releases/latest) -> derniere version + URL d'asset de la variante.
 * Renseigne ota_latest_version()/ota_latest_url(). Reseau bloquant (~s), a appeler hors tick. */
esp_err_t   ota_check_github(void);
const char *ota_latest_version(void);   /* "" si pas encore verifie */
const char *ota_latest_url(void);       /* "" si pas encore verifie */
uint32_t    ota_check_count(void);      /* nombre de verifications terminees (reussies ou non) */
esp_err_t ota_rollback(void);
esp_err_t ota_mark_valid(void);

/* Etat du retour automatique, pour l'onglet OTA. */
typedef struct {
    int8_t rb_auto;        /* 1 actif, 0 inactif (bootloader d'avant l'option), -1 inconnu */
    bool   on_trial;       /* version en cours en essai, pas encore validee */
    char   discarded[32];  /* version de la derniere image ecartee ("" : aucune) */
    bool   discarded_auto; /* ecartee par le bootloader : elle n'a pas demarre */
} ota_rb_info_t;
void ota_rb_info(ota_rb_info_t *out);
