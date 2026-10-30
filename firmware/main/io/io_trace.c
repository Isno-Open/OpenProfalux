#include "io_trace.h"
#include "esp_timer.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

/* Journal borne : au-dela on arrete d'ajouter (une instance d'enrolement fait ~20 trames). */
#define TRACE_MAX   64
#define RAW_MAX     32
#define NS          "iohc"   /* meme namespace NVS que io_store */

typedef struct {
    int32_t ms;        /* instant relatif au reset */
    char    dir;       /* 'R' recue, 'T' emise */
    int8_t  rssi;      /* dBm, pour 'R' */
    uint8_t n;         /* octets gardes (<= RAW_MAX) */
    uint8_t cmd;       /* octet de commande (octet 8), 0 si trame trop courte */
    uint8_t raw[RAW_MAX];
} entry_t;

static entry_t s_log[TRACE_MAX];
static int     s_n = 0;
static int64_t s_start_us = 0;
static bool    s_loaded = false;   /* le journal RAM reflete-t-il deja la NVS ? */

/* Recharge le journal depuis la NVS (survit au reboot). Appele paresseusement au 1er acces. */
static void load_from_nvs(void) {
    s_loaded = true;
    s_n = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t cnt = 0;
    nvs_get_u8(h, "trcc", &cnt);
    if (cnt > TRACE_MAX) cnt = TRACE_MAX;
    size_t rd = sizeof s_log;
    if (nvs_get_blob(h, "trc", s_log, &rd) == ESP_OK) s_n = cnt;
    nvs_close(h);
}

/* Ecrit le journal RAM en NVS. Appele a la FIN d'une capture / d'un enrolement, jamais par trame
 * (un nvs_commit prend ~10-20 ms et ferait rater des trames RX dans la boucle de capture). */
void io_trace_persist(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "trc", s_log, sizeof s_log);
    nvs_set_u8(h, "trcc", (uint8_t)(s_n < 0 ? 0 : (s_n > TRACE_MAX ? TRACE_MAX : s_n)));
    nvs_commit(h);
    nvs_close(h);
}

void io_trace_reset(void) {
    s_n = 0;
    s_loaded = true;   /* on repart d'un journal vide, pas besoin de recharger la NVS */
    s_start_us = esp_timer_get_time();
    io_trace_persist();   /* efface aussi la NVS : pas de vieux journal qui traine */
}

void io_trace_add(char dir, int8_t rssi, const uint8_t *bytes, int n) {
    if (!s_loaded) load_from_nvs();
    if (s_n >= TRACE_MAX || !bytes || n <= 0) return;
    if (s_start_us == 0) s_start_us = esp_timer_get_time();
    entry_t *e = &s_log[s_n++];
    e->ms = (int32_t)((esp_timer_get_time() - s_start_us) / 1000);
    e->dir = dir;
    e->rssi = rssi;
    int m = n < RAW_MAX ? n : RAW_MAX;
    e->n = (uint8_t)m;
    memcpy(e->raw, bytes, (size_t)m);
    e->cmd = (n > 8) ? bytes[8] : 0;
}

void io_trace_mark(const char *label) {
    if (!s_loaded) load_from_nvs();
    if (s_n >= TRACE_MAX || !label) return;
    if (s_start_us == 0) s_start_us = esp_timer_get_time();
    entry_t *e = &s_log[s_n++];
    e->ms = (int32_t)((esp_timer_get_time() - s_start_us) / 1000);
    e->dir = 'M';
    e->rssi = 0;
    strncpy((char *)e->raw, label, RAW_MAX - 1);
    e->raw[RAW_MAX - 1] = 0;
    e->n = (uint8_t)strlen((char *)e->raw);
    e->cmd = 0;
}

int io_trace_count(void) {
    if (!s_loaded) load_from_nvs();
    return s_n;
}

int io_trace_dump(char *out, size_t cap) {
    if (!s_loaded) load_from_nvs();
    int w = 0;
    for (int i = 0; i < s_n && (size_t)w < cap; i++) {
        entry_t *e = &s_log[i];
        if (e->dir == 'M') {   /* marqueur de section (nom d'etape) */
            w += snprintf(out + w, cap - (size_t)w, "\n===== %.*s =====\n", (int)e->n, (char *)e->raw);
            continue;
        }
        char hx[2 * RAW_MAX + 1];
        for (int j = 0; j < e->n; j++) snprintf(hx + 2 * j, 3, "%02X", e->raw[j]);
        hx[2 * e->n] = '\0';
        if (e->dir == 'R')
            w += snprintf(out + w, cap - (size_t)w, "t+%6ldms RX  RSSI %4d  cmd=0x%02X  %s\n",
                          (long)e->ms, (int)e->rssi, e->cmd, hx);
        else
            w += snprintf(out + w, cap - (size_t)w, "t+%6ldms TX            cmd=0x%02X  %s\n",
                          (long)e->ms, e->cmd, hx);
    }
    return w;
}
