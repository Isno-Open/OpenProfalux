/* Reserve de memoire du TLS : voir tls_pool.h. En CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC, ESP-IDF ne
 * branche RIEN : mbedTLS garde calloc/free tant que le firmware n'appelle pas
 * mbedtls_platform_set_calloc_free (tls_pool_init). Constate le 2026-11-13 : deux fonctions
 * esp_mbedtls_mem_calloc/_free definies ici finissaient dans les sections ecartees du lien, la
 * reserve aurait ete prise pour rien. */
#include "tls_pool.h"

/* Taille de la reserve : tls_pool_need (tls_budget.c, unite pure testee sur PC). */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>
#include <esp_log.h>
#include <mbedtls/platform.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "tls_pool";

static uint8_t *s_base = NULL;          /* zone reservee, NULL = pas de reserve */
static size_t s_size = 0;
static multi_heap_handle_t s_heap = NULL;
static volatile uint32_t s_spills = 0;


static bool in_pool(const void *p) {
    return s_base && (const uint8_t *)p >= s_base && (const uint8_t *)p < s_base + s_size;
}

/* Appelee par mbedTLS pour toute allocation (contextes, certificats, tampons). */
static void *tls_calloc(size_t n, size_t size) {
    if (n && size > SIZE_MAX / n) return NULL;   /* n*size deborde : refus, comme calloc */
    size_t bytes = n * size;
    if (s_heap) {
        void *p = multi_heap_malloc(s_heap, bytes);   /* multi_heap a son propre verrou */
        if (p) { memset(p, 0, bytes); return p; }
        s_spills++;
    }
    return calloc(n, size);   /* hors reserve : comme le mode par defaut de mbedTLS */
}

static void tls_free(void *ptr) {
    if (!ptr) return;
    if (in_pool(ptr)) multi_heap_free(s_heap, ptr);
    else free(ptr);
}

void tls_pool_init(size_t bytes) {
    if (s_heap) return;
    if (!bytes) { ESP_LOGI(TAG, "aucune connexion TLS attendue : pas de reserve"); return; }
    /* Memoire interne (le TLS y touche octet par octet) prise d'un seul bloc, avant que le reste
     * du demarrage ne la morcelle. */
    uint8_t *base = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!base) { ESP_LOGE(TAG, "reserve de %u o impossible : TLS dans la memoire generale", (unsigned)bytes); return; }
    multi_heap_handle_t h = multi_heap_register(base, bytes);
    if (!h) { heap_caps_free(base); ESP_LOGE(TAG, "reserve non initialisee"); return; }
    s_base = base; s_size = bytes; s_heap = h;
    /* A partir d'ici, toute allocation de mbedTLS passe par la reserve. */
    if (mbedtls_platform_set_calloc_free(tls_calloc, tls_free) != 0) {
        ESP_LOGE(TAG, "allocateur TLS non branche : reserve inutilisee");
        return;
    }
    ESP_LOGI(TAG, "reserve TLS : %u o", (unsigned)s_size);
}

void tls_pool_stats(tls_pool_stats_t *out) {
    memset(out, 0, sizeof(*out));
    if (!s_heap) return;
    out->size = (uint32_t)s_size;
    out->free_now = (uint32_t)multi_heap_free_size(s_heap);
    out->free_min = (uint32_t)multi_heap_minimum_free_size(s_heap);
    out->spills = s_spills;
}

/* ── Tour de poignee de main (voir tls_pool.h) ── */
static SemaphoreHandle_t s_turn = NULL;
static portMUX_TYPE s_turn_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_turn_misses = 0;

static SemaphoreHandle_t turn(void) {
    if (s_turn) return s_turn;
    SemaphoreHandle_t t = xSemaphoreCreateBinary();
    if (!t) return NULL;
    xSemaphoreGive(t);
    taskENTER_CRITICAL(&s_turn_mux);
    bool won = s_turn == NULL;
    if (won) s_turn = t;
    taskEXIT_CRITICAL(&s_turn_mux);
    if (!won) vSemaphoreDelete(t);
    return s_turn;
}

bool tls_turn_take(uint32_t wait_ms) {
    SemaphoreHandle_t t = turn();
    if (t && xSemaphoreTake(t, pdMS_TO_TICKS(wait_ms)) == pdTRUE) return true;
    s_turn_misses++;
    ESP_LOGW(TAG, "poignee de main hors tour (attente > %u ms), %u depuis le demarrage",
             (unsigned)wait_ms, (unsigned)s_turn_misses);
    return false;
}

void tls_turn_give(void) { if (s_turn) xSemaphoreGive(s_turn); }

uint32_t tls_turn_misses(void) { return s_turn_misses; }
