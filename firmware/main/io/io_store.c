/* io_store : voir io_store.h. Identites io de la carte (une par source), persistees en NVS. */
#include "io_store.h"
#include <string.h>
#include <stdio.h>
#include "nvs.h"

#define NS "iohc"

/* Cle NVS propre a une source : "k<6 hexa>" pour la cle, "r<6 hexa>" pour le compteur.
 * 7 caracteres, bien sous la limite NVS de 15. */
static void key_name(const uint8_t src[3], char prefix, char out[8]) {
    snprintf(out, 8, "%c%02X%02X%02X", prefix, src[0], src[1], src[2]);
}

bool io_store_get_key(const uint8_t src[3], uint8_t key[16]) {
    if (!src || !key) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char nk[8]; key_name(src, 'k', nk);
    size_t n = 16;
    bool ok = nvs_get_blob(h, nk, key, &n) == ESP_OK && n == 16;
    nvs_close(h);
    return ok;
}

/* Reserve avant TX : une erreur NVS interdit l'emission, jamais de retour a zero. */
bool io_store_next_rolling(const uint8_t src[3], uint16_t *out) {
    if (!src || !out) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    char nr[8]; key_name(src, 'r', nr);
    uint32_t roll;
    esp_err_t e = nvs_get_u32(h, nr, &roll);
    if (e == ESP_OK && roll > UINT16_MAX) e = ESP_ERR_INVALID_STATE;
    if (e == ESP_OK) e = nvs_set_u32(h, nr, roll + 1);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) return false;
    *out = (uint16_t)roll;
    return true;
}

/* rolling = prochain compteur emis ; -1 conserve l'existant, ou initialise a 1. */
bool io_store_set(const uint8_t src[3], const uint8_t key[16],
                  const io_profile_t *p, int rolling) {
    if (!src || !key || !p || rolling < -1 || rolling > UINT16_MAX) return false;
    for (int a = 0; a < IO_ACTION_COUNT; a++)
        if (p->payload_len[a] < 3 || p->payload_len[a] > 13) return false;   /* par action : 32 octets moins en-tete, compteur, MAC et CRC */
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    char nk[8], nr[8], np[8];
    key_name(src, 'k', nk); key_name(src, 'r', nr); key_name(src, 'p', np);
    uint32_t roll = 1;
    esp_err_t e = nvs_get_u32(h, nr, &roll);
    bool found = e == ESP_OK;
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (rolling >= 0) {
        /* Une adoption ne peut pas reculer un compteur deja reserve localement. */
        if (e == ESP_OK && found && (uint32_t)rolling < roll) e = ESP_ERR_INVALID_STATE;
        roll = (uint32_t)rolling;
    }
    if (e == ESP_OK) e = nvs_set_blob(h, nk, key, 16);
    if (e == ESP_OK) e = nvs_set_blob(h, np, p, sizeof *p);
    if (e == ESP_OK) e = nvs_set_u32(h, nr, roll);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

bool io_store_get_profile(const uint8_t src[3], io_profile_t *p) {
    if (!src || !p) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    char np[8]; key_name(src, 'p', np);
    size_t n = sizeof *p;
    bool ok = nvs_get_blob(h, np, p, &n) == ESP_OK && n == sizeof *p;
    nvs_close(h);
    return ok;
}

/* Purge explicite des identites, profils et compteurs io. */
int io_store_erase_all(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    size_t used = 0;
    nvs_get_used_entry_count(h, &used);   /* avant purge, pour le compte rendu */
    esp_err_t e = nvs_erase_all(h);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? (int)used : -1;
}
