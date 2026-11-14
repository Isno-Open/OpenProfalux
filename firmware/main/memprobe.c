/* Sondes memoire de diagnostic : voir memprobe.h. */
#include "memprobe.h"
#include <sdkconfig.h>

#if CONFIG_HEAP_TRACING_STANDALONE
#include <stdbool.h>
#include <stdio.h>
#include <esp_heap_caps.h>
#include <esp_heap_trace.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define MEMTRACE_LOG_MIN 256   /* taille ecrite une par une dans le journal */
#define MEMTRACE_RECORDS 100   /* ~ 100 x 70 o statiques (6 appelants) : 300 x 4 niveaux prenaient ~17 Ko au tas et faussaient la mesure */
static heap_trace_record_t s_rec[MEMTRACE_RECORDS];
static bool s_init;
static volatile bool s_active, s_ready;
static const char *volatile s_why;
static esp_timer_handle_t s_stop;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Par ESP_LOG : la console ET le journal du firmware (/api/log), lisible sans cable serie. */
static const char *TAG = "memprobe";
void memprobe(const char *label) {
    ESP_LOGW(TAG, "MEMPROBE %s t=%u libre=%u bloc=%u min=%u", label,
                   (unsigned)(esp_timer_get_time() / 1000),
                   (unsigned)esp_get_free_heap_size(),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                   (unsigned)esp_get_minimum_free_heap_size());
}

static void stop_cb(void *arg) {
    (void)arg;
    heap_trace_stop();
    s_active = false;
    s_ready = true;   /* l'ecriture (longue) se fait dans la boucle de fond, pas ici */
}

void memtrace_window(const char *why, uint32_t ms, bool leaks) {
    taskENTER_CRITICAL(&s_mux);
    bool busy = s_active || s_ready;
    if (!busy) s_active = true;
    taskEXIT_CRITICAL(&s_mux);
    if (busy) return;   /* une fenetre a la fois */
    if (!s_init) {
        const esp_timer_create_args_t ta = { .callback = stop_cb, .name = "memtrace" };
        if (heap_trace_init_standalone(s_rec, MEMTRACE_RECORDS) != ESP_OK
            || esp_timer_create(&ta, &s_stop) != ESP_OK) { s_active = false; return; }
        s_init = true;
    }
    s_why = why;
    memprobe(why);
    heap_trace_start(leaks ? HEAP_TRACE_LEAKS : HEAP_TRACE_ALL);
    esp_timer_start_once(s_stop, (uint64_t)ms * 1000);
}

#define MEMWATCH_LOW   8192   /* sous ce libre, on trace qui alloue (build de diagnostic seulement) */
#define MEMWATCH_GAP_US (30LL * 1000000)   /* une trace de creux toutes les 30 s au plus */
#define MEMWATCH_STEP  1024   /* une sonde a chaque nouveau minimum plus bas d'au moins 1 Ko */
void memprobe_watch(void) {
    static uint32_t last_min = UINT32_MAX;
    uint32_t m = esp_get_minimum_free_heap_size();
    if (m + MEMWATCH_STEP <= last_min) { last_min = m; memprobe("nouveau-min"); }
    /* Une trace de creux toutes les MEMWATCH_GAP_US au plus : tracer et imprimer coute de la
     * memoire, et a chaque seconde le detecteur entretenait lui-meme le creux (mesure du
     * 2026-11-14 sur l'ATOM : 7,5 Ko libres en permanence, web et ping muets). */
    static int64_t last_us = -MEMWATCH_GAP_US;
    int64_t now = esp_timer_get_time();
    if (esp_get_free_heap_size() < MEMWATCH_LOW && now - last_us >= MEMWATCH_GAP_US) {
        last_us = now;
        memtrace_window("creux", 1000, false);
    }
}

void memtrace_poll(void) {
    if (!s_ready) return;
    /* Pas heap_trace_dump : il imprime tout en section critique, et 100 enregistrements a
     * 115 200 bauds tiennent les interruptions plus d'une seconde (chien de garde, mesure le
     * 2026-11-13 sur l'ATOM). Ici, une copie courte par enregistrement, puis l'impression. */
    /* Le journal du firmware ne garde que LOG_RING_SIZE octets : seules les allocations d'au moins
     * MEMTRACE_LOG_MIN octets y sont ecrites une par une, le reste en total. */
    size_t n = heap_trace_get_count(), small_n = 0, small_b = 0, big_b = 0;
    ESP_LOGW(TAG, "MEMTRACE debut %s : %u enregistrement(s)", s_why ? s_why : "?", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        heap_trace_record_t r;
        if (heap_trace_get(i, &r) != ESP_OK) break;
        if (r.size < MEMTRACE_LOG_MIN) { small_n++; small_b += r.size; continue; }
        big_b += r.size;
        char who[16 * CONFIG_HEAP_TRACING_STACK_DEPTH] = "";
        size_t w = 0;
        for (int k = 0; k < CONFIG_HEAP_TRACING_STACK_DEPTH && r.alloced_by[k] && w < sizeof(who); k++)
            w += snprintf(who + w, sizeof(who) - w, "%s%p", k ? ":" : "", r.alloced_by[k]);
        ESP_LOGW(TAG, "%u o caller %s%s", (unsigned)r.size, who, r.freed_by[0] ? " freed" : "");
        vTaskDelay(1);   /* laisse respirer la console et les autres taches */
    }
    ESP_LOGW(TAG, "MEMTRACE fin %s : %u o en allocations >= %u o, %u petites (%u o)", s_why ? s_why : "?",
             (unsigned)big_b, (unsigned)MEMTRACE_LOG_MIN, (unsigned)small_n, (unsigned)small_b);
    memprobe("apres-trace");
    s_ready = false;
}

#else
void memprobe(const char *label) { (void)label; }
void memtrace_window(const char *why, uint32_t ms, bool leaks) { (void)why; (void)ms; (void)leaks; }
void memtrace_poll(void) {}
void memprobe_watch(void) {}
#endif
