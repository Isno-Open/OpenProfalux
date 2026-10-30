/* capture_journal : voir capture_journal.h. Journal commun io + raw-ook, RAM + NVS, marqueurs.
 * Stockage : un tableau d'entrees (meta) + un pool d'octets partage (data). Une entree pointe
 * dans le pool (off, len). io -> octets de la trame ; raw-ook -> impulsions en int16 signe
 * (+us si niveau 1, -us si niveau 0) ; marqueur -> texte du label. */
#include "capture_journal.h"
#include "esp_timer.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

#define NS          "iohc"
#define MAX_ENTRIES 80
#define POOL_BYTES  6144
#define IO_BAND_MHZ 868
#define IO_FREQ_KHZ 868950u
#define MARK_TYPE   0xFF
#define RAW_HEXMAX  32

typedef struct {
    int32_t  t_ms;
    uint32_t freq_khz;
    uint16_t band_mhz;
    uint16_t off;      /* offset dans le pool */
    uint16_t len;      /* octets occupes dans le pool (raw-ook : 2 * nb impulsions) */
    uint8_t  radio;
    uint8_t  type;     /* cap_type_t, ou MARK_TYPE */
    int8_t   rssi;
    char     dir;      /* 'R','T','M' */
    int16_t  cmd;      /* -1 si N/A */
} cj_entry_t;

static cj_entry_t s_log[MAX_ENTRIES];
static uint8_t    s_pool[POOL_BYTES];
static int        s_n = 0;
static int        s_pool_used = 0;
static int64_t    s_start_us = 0;
static int        s_loaded = 0;

static void load_from_nvs(void) {
    s_loaded = 1; s_n = 0; s_pool_used = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint16_t cnt = 0, used = 0;
    nvs_get_u16(h, "cjn", &cnt);
    nvs_get_u16(h, "cju", &used);
    size_t sz = sizeof s_log;
    if (nvs_get_blob(h, "cje", s_log, &sz) == ESP_OK) {
        sz = sizeof s_pool;
        nvs_get_blob(h, "cjp", s_pool, &sz);
        s_n = cnt > MAX_ENTRIES ? MAX_ENTRIES : cnt;
        s_pool_used = used > POOL_BYTES ? POOL_BYTES : used;
    }
    nvs_close(h);
}

void capture_journal_persist(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cje", s_log, sizeof s_log);
    nvs_set_blob(h, "cjp", s_pool, sizeof s_pool);
    nvs_set_u16(h, "cjn", (uint16_t)(s_n < 0 ? 0 : s_n));
    nvs_set_u16(h, "cju", (uint16_t)(s_pool_used < 0 ? 0 : s_pool_used));
    nvs_commit(h);
    nvs_close(h);
}

void capture_journal_reset(void) {
    s_n = 0; s_pool_used = 0; s_loaded = 1;
    s_start_us = esp_timer_get_time();
    capture_journal_persist();
}

/* Reserve une nouvelle entree (chrono relatif). NULL si plein. */
static cj_entry_t *new_entry(void) {
    if (!s_loaded) load_from_nvs();
    if (s_n >= MAX_ENTRIES) return NULL;
    if (s_start_us == 0) s_start_us = esp_timer_get_time();
    cj_entry_t *e = &s_log[s_n++];
    memset(e, 0, sizeof *e);
    e->t_ms = (int32_t)((esp_timer_get_time() - s_start_us) / 1000);
    e->cmd = -1;
    return e;
}

/* Copie len octets dans le pool, rend l'offset ou -1 si plus de place. */
static int pool_put(const void *src, int len) {
    if (len < 0 || s_pool_used + len > POOL_BYTES) return -1;
    int off = s_pool_used;
    if (src && len) memcpy(s_pool + off, src, (size_t)len);
    s_pool_used += len;
    return off;
}

void capture_journal_mark(const char *label) {
    if (!label) return;
    int n = (int)strlen(label); if (n > 40) n = 40;
    cj_entry_t *e = new_entry(); if (!e) return;
    int off = pool_put(label, n);
    if (off < 0) { s_n--; return; }
    e->type = MARK_TYPE; e->dir = 'M'; e->off = (uint16_t)off; e->len = (uint16_t)n;
}

void capture_journal_add_io(char dir, uint8_t radio, int8_t rssi, int cmd,
                            const uint8_t *bytes, int n) {
    if (!bytes || n <= 0) return;
    if (n > RAW_HEXMAX) n = RAW_HEXMAX;
    cj_entry_t *e = new_entry(); if (!e) return;
    int off = pool_put(bytes, n);
    if (off < 0) { s_n--; return; }
    e->type = CAP_IO; e->dir = dir; e->radio = radio; e->rssi = rssi;
    e->cmd = (int16_t)cmd; e->band_mhz = IO_BAND_MHZ; e->freq_khz = IO_FREQ_KHZ;
    e->off = (uint16_t)off; e->len = (uint16_t)n;
}

int capture_journal_add_rawook(uint8_t radio, uint16_t band_mhz, uint32_t freq_khz, int8_t rssi,
                               const uint8_t *levels, const uint16_t *us, uint16_t n) {
    if (!levels || !us || n == 0) return -1;
    cj_entry_t *e = new_entry(); if (!e) return -1;
    int bytes = (int)n * 2;   /* une impulsion = un int16 signe */
    if (s_pool_used + bytes > POOL_BYTES) { s_n--; return -1; }
    int off = s_pool_used;
    for (uint16_t i = 0; i < n; i++) {
        int32_t d = us[i]; if (d > 32767) d = 32767;
        int16_t v = levels[i] ? (int16_t)d : (int16_t)(-d);
        memcpy(s_pool + off + (int)i * 2, &v, 2);
    }
    s_pool_used += bytes;
    e->type = CAP_RAW_OOK; e->dir = 'R'; e->radio = radio; e->rssi = rssi;
    e->band_mhz = band_mhz; e->freq_khz = freq_khz;
    e->off = (uint16_t)off; e->len = (uint16_t)bytes;
    return 0;
}

int capture_journal_count(void) { if (!s_loaded) load_from_nvs(); return s_n; }

int capture_journal_dump(char *out, size_t cap) {
    if (!s_loaded) load_from_nvs();
    int w = 0;
    for (int i = 0; i < s_n && (size_t)w < cap; i++) {
        cj_entry_t *e = &s_log[i];
        if (e->type == MARK_TYPE) {
            w += snprintf(out + w, cap - (size_t)w, "\n===== %.*s =====\n",
                          (int)e->len, (char *)(s_pool + e->off));
            continue;
        }
        if (e->type == CAP_RAW_OOK) {
            int np = e->len / 2;
            w += snprintf(out + w, cap - (size_t)w,
                          "t+%6ldms RAW radio%u band%u %lukHz RSSI%4d %d imp : ",
                          (long)e->t_ms, e->radio, e->band_mhz, (unsigned long)e->freq_khz,
                          (int)e->rssi, np);
            for (int j = 0; j < np && (size_t)w < cap; j++) {
                int16_t v; memcpy(&v, s_pool + e->off + j * 2, 2);
                int lvl = v >= 0 ? 1 : 0, us = v >= 0 ? v : -v;
                w += snprintf(out + w, cap - (size_t)w, "[%d,%d]", lvl, us);
            }
            w += snprintf(out + w, cap - (size_t)w, "\n");
            continue;
        }
        /* io : hex de la trame */
        char hx[2 * RAW_HEXMAX + 1];
        int m = e->len; if (m > RAW_HEXMAX) m = RAW_HEXMAX;
        for (int j = 0; j < m; j++) snprintf(hx + 2 * j, 3, "%02X", s_pool[e->off + j]);
        hx[2 * m] = '\0';
        w += snprintf(out + w, cap - (size_t)w,
                      "t+%6ldms %s band%u RSSI %4d cmd=0x%02X  %s\n",
                      (long)e->t_ms, e->dir == 'T' ? "TX" : "RX", e->band_mhz,
                      (int)e->rssi, (e->cmd < 0 ? 0 : e->cmd) & 0xFF, hx);
    }
    return w;
}
