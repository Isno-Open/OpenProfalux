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

void io_store_set_rolling(const uint8_t src[3], uint16_t seq) {
    if (!src) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    char nr[8]; key_name(src, 'r', nr);
    nvs_set_u32(h, nr, seq);   /* impose le compteur de depart (ex. celui capte de l'origine) */
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

/* ── Journal persistant des identites io inconnues (voir io_store.h) ──
 * Stocke en NVS : "unkc" = compteur (u8), "unk" = tableau fixe d'entrees. Dedup par source. */
#define UNK_FRAME_MAX 31   /* une trame d'enrolement 0x30 fait 31 octets */
typedef struct {
    uint8_t  src[3];
    uint8_t  len;
    uint8_t  frame[UNK_FRAME_MAX];
    int8_t   rssi;              /* dBm au RX (pour demeler l'emetteur) */
    uint32_t ts;               /* secondes depuis le boot */
} io_unk_t;

bool io_store_record_unknown(const uint8_t src[3], const uint8_t *frame, int n, int8_t rssi, uint32_t ts_s) {
    if (!src || !frame || n < 9 || n > UNK_FRAME_MAX) return false;
    uint8_t known[16];
    if (io_store_get_key(src, known)) return false;      /* deja connue : ce n'est pas une inconnue */
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    io_unk_t tab[IO_UNKNOWN_MAX];
    memset(tab, 0, sizeof tab);
    uint8_t cnt = 0;
    nvs_get_u8(h, "unkc2", &cnt);                        /* "unk2" : format avec RSSI+ts (repart propre) */
    if (cnt > IO_UNKNOWN_MAX) cnt = IO_UNKNOWN_MAX;
    size_t rd = sizeof tab;
    nvs_get_blob(h, "unk2", tab, &rd);                   /* absent => tab reste a zero */
    bool present = false;
    for (int i = 0; i < cnt; i++) if (!memcmp(tab[i].src, src, 3)) { present = true; break; }
    bool stored = false;
    if (!present && cnt < IO_UNKNOWN_MAX) {
        memcpy(tab[cnt].src, src, 3);
        tab[cnt].len = (uint8_t)n;
        memcpy(tab[cnt].frame, frame, (size_t)n);
        tab[cnt].rssi = rssi;
        tab[cnt].ts = ts_s;
        cnt++;
        if (nvs_set_blob(h, "unk2", tab, sizeof tab) == ESP_OK &&
            nvs_set_u8(h, "unkc2", cnt) == ESP_OK) {
            nvs_commit(h);
            stored = true;
        }
    }
    nvs_close(h);
    return stored;
}

int io_store_unknown_json(char *out, size_t cap) {
    if (!out || cap < 3) { if (out && cap) out[0] = 0; return 0; }
    int p = 0;
    out[p++] = '[';
    int added = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        io_unk_t tab[IO_UNKNOWN_MAX];
        memset(tab, 0, sizeof tab);
        uint8_t cnt = 0;
        nvs_get_u8(h, "unkc2", &cnt);
        if (cnt > IO_UNKNOWN_MAX) cnt = IO_UNKNOWN_MAX;
        size_t rd = sizeof tab;
        nvs_get_blob(h, "unk2", tab, &rd);
        for (int i = 0; i < cnt; i++) {
            char fr[2 * UNK_FRAME_MAX + 1];
            int L = tab[i].len; if (L > UNK_FRAME_MAX) L = UNK_FRAME_MAX;
            for (int k = 0; k < L; k++) snprintf(fr + 2 * k, 3, "%02X", tab[i].frame[k]);
            int w = snprintf(out + p, (p < (int)cap) ? cap - p : 0,
                             "%s{\"src\":\"%02X%02X%02X\",\"frame\":\"%s\",\"rssi\":%d,\"t\":%u}",
                             added ? "," : "", tab[i].src[0], tab[i].src[1], tab[i].src[2], fr,
                             (int)tab[i].rssi, (unsigned)tab[i].ts);
            if (w < 0 || p + w >= (int)cap - 1) break;   /* troncature propre : garde la place du ] */
            p += w; added++;
        }
        nvs_close(h);
    }
    if (p < (int)cap - 1) out[p++] = ']';
    out[p] = 0;
    return added;
}
