/* Journal du firmware en memoire, pour l'interface web. Voir log_ring.h. */
#include "log_ring.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static char s_ring[LOG_RING_SIZE];
static size_t s_head;        /* prochaine ecriture */
static bool s_full;          /* l'anneau a deja fait le tour */
static unsigned s_dropped;   /* messages non conserves (journal occupe a ce moment-la) */
static char s_line[256];     /* mise en forme d'un message, sous s_mx */
static SemaphoreHandle_t s_mx;
static vprintf_like_t s_prev;

/* Ajoute n octets, sans les sequences de couleur ANSI (ESC [ ... m) du port serie. */
static void ring_put(const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\x1b') {                       /* saute "ESC [ ... lettre" */
            while (i < n && !((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= 'a' && p[i] <= 'z'))) i++;
            continue;
        }
        s_ring[s_head++] = p[i];
        if (s_head == LOG_RING_SIZE) { s_head = 0; s_full = true; }
    }
}

/* Appelee pour chaque message ESP_LOGx, depuis n'importe quelle tache. Elle ne doit
 * jamais faire attendre l'appelant : si le journal est occupe (un autre message, ou
 * une lecture par /api/log), le message va seulement au port serie, et le trou est
 * signale dans le journal au message suivant. */
static int log_vprintf(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int r = s_prev ? s_prev(fmt, ap) : vprintf(fmt, ap);
    if (xPortInIsrContext()) { s_dropped++; va_end(ap2); return r; }   /* pas de mutex en interruption */
    if (xSemaphoreTake(s_mx, 0) == pdTRUE) {
        if (s_dropped) {
            int k = snprintf(s_line, sizeof(s_line), "... %u message(s) non conserve(s) ici\n", s_dropped);
            ring_put(s_line, (size_t)k);
            s_dropped = 0;
        }
        int k = vsnprintf(s_line, sizeof(s_line), fmt, ap2);
        if (k > 0) ring_put(s_line, (size_t)k < sizeof(s_line) ? (size_t)k : sizeof(s_line) - 1);
        xSemaphoreGive(s_mx);
    } else {
        s_dropped++;
    }
    va_end(ap2);
    return r;
}

void log_ring_init(void) {
    if (s_mx) return;
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return;   /* pas de journal web, le port serie reste */
    s_prev = esp_log_set_vprintf(log_vprintf);
}

size_t log_ring_copy(char *out, size_t cap) {
    if (!s_mx || !cap || xSemaphoreTake(s_mx, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t len = s_full ? LOG_RING_SIZE : s_head;
    size_t start = s_full ? s_head : 0;
    size_t skip = 0;
    if (s_full) {   /* le plus ancien message est coupe : on repart a la ligne suivante */
        while (skip < len && s_ring[(start + skip) % LOG_RING_SIZE] != '\n') skip++;
        if (skip < len) skip++;
    }
    size_t n = len - skip;
    if (n > cap) { skip += n - cap; n = cap; }
    for (size_t i = 0; i < n; i++) out[i] = s_ring[(start + skip + i) % LOG_RING_SIZE];
    xSemaphoreGive(s_mx);
    return n;
}
