/* io_store : voir io_store.h. Identites io de la carte (une par source), persistees en NVS. */
#include "io_store.h"
#include <string.h>
#include <stdio.h>
#include "nvs.h"
#include "esp_random.h"

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

void io_store_set(const uint8_t src[3], const uint8_t key[16]) {
    if (!src || !key) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    char nk[8], nr[8];
    key_name(src, 'k', nk);
    key_name(src, 'r', nr);
    nvs_set_blob(h, nk, key, 16);
    /* Compteur roulant : depart aleatoire la premiere fois, pour ne pas rejouer un vieux code. */
    uint32_t roll;
    if (nvs_get_u32(h, nr, &roll) != ESP_OK) {
        nvs_set_u32(h, nr, esp_random() & 0xFFFF);
    }
    nvs_commit(h);
    nvs_close(h);
}

uint16_t io_store_next_rolling(const uint8_t src[3]) {
    if (!src) return 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return 0;
    char nr[8]; key_name(src, 'r', nr);
    uint32_t roll = 1;
    nvs_get_u32(h, nr, &roll);
    nvs_set_u32(h, nr, (roll + 1) & 0xFFFF);
    nvs_commit(h);
    nvs_close(h);
    return (uint16_t)roll;
}

void io_store_set_profile(const uint8_t src[3], const io_profile_t *p) {
    if (!src || !p) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    char np[8]; key_name(src, 'p', np);   /* "p<6 hexa>" : le profil de cette source */
    nvs_set_blob(h, np, p, sizeof *p);
    nvs_commit(h);
    nvs_close(h);
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
