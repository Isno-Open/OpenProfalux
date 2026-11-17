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

#define MEMTRACE_LOG_MIN 64    /* taille ecrite une par une dans le journal */
#define MEMTRACE_LINE_GAP_MS 30   /* entre deux lignes ecrites */
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

#define MEMWATCH_LOW   8192   /* sous ce libre, on fige ce qui est tenu (build de diagnostic seulement) */
#define MEMWATCH_CALM  20480  /* au-dessus, la memoire est au repos : on repart d'une liste vide */
#define MEMWATCH_RESTART_US (2LL * 1000000)   /* repartir a vide toutes les 2 s au repos */
#define MEMWATCH_CHECK_US 2000   /* le libre est regarde toutes les 2 ms pour figer AU creux */
#define MEMWATCH_STEP  1024   /* une sonde a chaque nouveau minimum plus bas d'au moins 1 Ko */

/* Trace du creux : en mode « tenu » (HEAP_TRACE_LEAKS), la liste ne garde que les allocations
 * pas encore liberees, et quand elle est pleine c'est la plus ancienne qui sort. Relancee a vide
 * au repos, elle contient donc, au moment ou le libre passe sous MEMWATCH_LOW, exactement ce qui
 * a ete pris depuis le repos et est encore tenu au fond du creux. La trace « tout » d'une seconde
 * prise APRES le passage du seuil (version precedente) montrait ce qui passait, pas ce qui tenait :
 * pendant le chargement de l'interface, cloud actif (creux a 3,6 Ko, mesure du 2026-11-16 sur
 * l'ATOM), il faut savoir qui garde les 10 Ko. */
static esp_timer_handle_t s_dip_timer;
static volatile bool s_dip_armed;
static volatile uint32_t s_dip_free;

static void dip_check_cb(void *arg) {
    (void)arg;
    if (!s_dip_armed) return;
    uint32_t f = esp_get_free_heap_size();
    if (f >= MEMWATCH_LOW) return;
    heap_trace_stop();
    s_dip_armed = false;
    s_dip_free = f;
    s_why = "creux-tenu";
    s_active = false;
    s_ready = true;
}

static void dip_arm(void) {
    if (!s_init) {
        const esp_timer_create_args_t ta = { .callback = stop_cb, .name = "memtrace" };
        if (heap_trace_init_standalone(s_rec, MEMTRACE_RECORDS) != ESP_OK
            || esp_timer_create(&ta, &s_stop) != ESP_OK) return;
        s_init = true;
    }
    if (!s_dip_timer) {
        const esp_timer_create_args_t ta = { .callback = dip_check_cb, .name = "memdip" };
        if (esp_timer_create(&ta, &s_dip_timer) != ESP_OK) return;
        esp_timer_start_periodic(s_dip_timer, MEMWATCH_CHECK_US);
    }
    taskENTER_CRITICAL(&s_mux);
    bool busy = s_ready || (s_active && !s_dip_armed);   /* une fenetre ordinaire est en cours */
    if (!busy) { s_active = true; s_dip_armed = false; }
    taskEXIT_CRITICAL(&s_mux);
    if (busy) return;
    heap_trace_stop();
    heap_trace_start(HEAP_TRACE_LEAKS);   /* repart d'une liste vide */
    s_dip_armed = true;
}

void memprobe_watch(void) {
    static uint32_t last_min = UINT32_MAX;
    uint32_t m = esp_get_minimum_free_heap_size();
    if (m + MEMWATCH_STEP <= last_min) { last_min = m; memprobe("nouveau-min"); }
    static int64_t last_us = -MEMWATCH_RESTART_US;
    int64_t now = esp_timer_get_time();
    if (esp_get_free_heap_size() >= MEMWATCH_CALM && now - last_us >= MEMWATCH_RESTART_US) {
        last_us = now;
        dip_arm();
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
    heap_trace_summary_t sum = { 0 };
    heap_trace_summary(&sum);
    ESP_LOGW(TAG, "MEMTRACE debut %s : %u enregistrement(s), libre au gel %u, debordement %d", s_why ? s_why : "?",
             (unsigned)n, (unsigned)s_dip_free, (int)sum.has_overflowed);
    for (size_t i = 0; i < n; i++) {
        heap_trace_record_t r;
        if (heap_trace_get(i, &r) != ESP_OK) break;
        if (r.size < MEMTRACE_LOG_MIN) { small_n++; small_b += r.size; continue; }
        big_b += r.size;
#if CONFIG_HEAP_TRACING_STACK_DEPTH > 0
        char who[16 * CONFIG_HEAP_TRACING_STACK_DEPTH + 1] = "";
        size_t w = 0;
        for (int k = 0; k < CONFIG_HEAP_TRACING_STACK_DEPTH && r.alloced_by[k] && w < sizeof(who); k++)
            w += snprintf(who + w, sizeof(who) - w, "%s%p", k ? ":" : "", r.alloced_by[k]);
        ESP_LOGW(TAG, "%u o caller %s%s", (unsigned)r.size, who, r.freed_by[0] ? " freed" : "");
#else   /* RISC-V (C3, C6...) : pas d'appelants, la profondeur est forcee a 0 */
        ESP_LOGW(TAG, "%u o a %p", (unsigned)r.size, r.address);
#endif
        vTaskDelay(pdMS_TO_TICKS(MEMTRACE_LINE_GAP_MS));   /* le journal web (8 Ko) a le temps d'etre lu */
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

/* Marge de pile de chaque tache, pour regler les piles sur la mesure et non sur une
 * estimation. Build de diagnostic seulement (test/heapdiag/sdkconfig.stacks) : la liste
 * des taches demande CONFIG_FREERTOS_USE_TRACE_FACILITY. */
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
#include <stdlib.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define STACKS_EVERY_US (120LL * 1000000)   /* toutes les 2 min : couvre GitHub, volets, OTA */
void memprobe_stacks(void) {
    static int64_t next = 0;
    int64_t now = esp_timer_get_time();
    if (now < next) return;
    next = now + STACKS_EVERY_US;
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *st = malloc(n * sizeof(*st));
    if (!st) return;
    n = uxTaskGetSystemState(st, n, NULL);
    for (UBaseType_t i = 0; i < n; i++)   /* usStackHighWaterMark : octets jamais touches */
        ESP_LOGW("memprobe", "PILE %s marge_min=%u", st[i].pcTaskName, (unsigned)st[i].usStackHighWaterMark);
    free(st);
}
#else
void memprobe_stacks(void) {}
#endif
