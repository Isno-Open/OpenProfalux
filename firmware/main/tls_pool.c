/* Reserve de memoire du TLS : voir tls_pool.h. En CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC, ESP-IDF ne
 * branche RIEN : mbedTLS garde calloc/free tant que le firmware n'appelle pas
 * mbedtls_platform_set_calloc_free (tls_pool_init). Constate le 2026-11-13 : deux fonctions
 * esp_mbedtls_mem_calloc/_free definies ici finissaient dans les sections ecartees du lien, la
 * reserve aurait ete prise pour rien. */
#include "tls_pool.h"

/* Taille de la reserve : un BESOIN du firmware (mTLS du cloud, RSA 2048 + paquet d'autorites,
 * tampons dynamiques de mbedTLS), pas une propriete de la carte. Mesure le 2026-11-13 sur
 * l'ATOM en 0.2.16-cloudtest3 avec une reserve de 24 Ko : 4 364 o libres au plus bas, soit
 * 20,2 Ko de pic (poignee de main du cloud, connexion GitHub ensuite), et 0 debordement.
 * 21 Ko = ce pic et un peu de marge ; tls_min et tls_spills, publies avec le battement,
 * disent s'il faut la revoir. */
#define TLS_POOL_BYTES (21 * 1024)
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>
#include <esp_log.h>
#include <mbedtls/platform.h>

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

void tls_pool_init(void) {
    if (s_heap) return;
    /* Memoire interne (le TLS y touche octet par octet) prise d'un seul bloc, avant que le reste
     * du demarrage ne la morcelle. */
    uint8_t *base = heap_caps_malloc(TLS_POOL_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!base) { ESP_LOGE(TAG, "reserve de %d o impossible : TLS dans la memoire generale", TLS_POOL_BYTES); return; }
    multi_heap_handle_t h = multi_heap_register(base, TLS_POOL_BYTES);
    if (!h) { heap_caps_free(base); ESP_LOGE(TAG, "reserve non initialisee"); return; }
    s_base = base; s_size = TLS_POOL_BYTES; s_heap = h;
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
