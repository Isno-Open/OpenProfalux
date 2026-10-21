/* io_store : voir io_store.h. Identite io-homecontrol de la carte, persistee en NVS. */
#include "io_store.h"
#include <string.h>
#include "nvs.h"
#include "esp_random.h"

#define NS     "iohc"
#define K_SRC  "src"
#define K_KEY  "key"
#define K_ROLL "roll"

static bool read_blob(nvs_handle_t h, const char *k, void *out, size_t len) {
    size_t n = len;
    return nvs_get_blob(h, k, out, &n) == ESP_OK && n == len;
}

bool io_store_get(uint8_t src[3], uint8_t key[16]) {
    if (!src || !key) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = read_blob(h, K_SRC, src, 3) && read_blob(h, K_KEY, key, 16);
    nvs_close(h);
    return ok;
}

bool io_store_has(void) {
    uint8_t s[3], k[16];
    return io_store_get(s, k);
}

void io_store_set(const uint8_t src[3], const uint8_t key[16]) {
    if (!src || !key) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, K_SRC, src, 3);
    nvs_set_blob(h, K_KEY, key, 16);
    /* Compteur roulant : depart aleatoire la premiere fois, pour ne pas rejouer un vieux code. */
    uint32_t roll;
    if (nvs_get_u32(h, K_ROLL, &roll) != ESP_OK) {
        nvs_set_u32(h, K_ROLL, esp_random() & 0xFFFF);
    }
    nvs_commit(h);
    nvs_close(h);
}

uint16_t io_store_next_rolling(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return 0;
    uint32_t roll = 1;
    nvs_get_u32(h, K_ROLL, &roll);
    nvs_set_u32(h, K_ROLL, (roll + 1) & 0xFFFF);
    nvs_commit(h);
    nvs_close(h);
    return (uint16_t)roll;
}
