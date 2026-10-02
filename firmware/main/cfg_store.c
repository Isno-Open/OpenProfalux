/* Rangement de la config des volets en NVS, une cle par volet : voir cfg_store.h. */
#include "cfg_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "cfg_store";
#define NS "shutters"

static void volet_key(char *k, size_t sz, int i) { snprintf(k, sz, "v%d", i); }

/* Garde la PREMIERE erreur : c'est elle qui explique les suivantes. */
static void fail(cfg_store_tx_t *tx, const char *key, esp_err_t e) {
    if (tx->err != ESP_OK) return;
    tx->err = e;
    snprintf(tx->failed_key, sizeof(tx->failed_key), "%s", key);
}

esp_err_t cfg_store_begin(cfg_store_tx_t *tx) {
    memset(tx, 0, sizeof(*tx));
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &tx->h);
    if (e != ESP_OK) { fail(tx, "(open)", e); return e; }
    tx->open = true;
    return ESP_OK;
}

/* doc == NULL : l'appelant n'a pas pu le produire (memoire). Compte comme un
 * echec, pour que "nv" ne valide jamais une config incomplete. */
void cfg_store_put_hdr(cfg_store_tx_t *tx, const char *doc) {
    if (!tx->open) return;
    esp_err_t e = doc ? nvs_set_str(tx->h, "hdr", doc) : ESP_ERR_NO_MEM;
    if (e != ESP_OK) fail(tx, "hdr", e);
}

/* On continue apres une erreur : les autres volets sont quand meme sauvegardes.
 * Seul "nv" n'est pas mis a jour (voir cfg_store_end). */
void cfg_store_put_volet(cfg_store_tx_t *tx, const char *doc) {
    if (!tx->open) return;
    char k[8]; volet_key(k, sizeof(k), tx->n);
    if (tx->n >= CFG_STORE_MAX_DOCS) { fail(tx, k, ESP_ERR_INVALID_SIZE); return; }
    tx->n++;
    esp_err_t e = doc ? nvs_set_str(tx->h, k, doc) : ESP_ERR_NO_MEM;
    if (e != ESP_OK) fail(tx, k, e);
}

esp_err_t cfg_store_end(cfg_store_tx_t *tx) {
    if (!tx->open) return tx->err;
    /* "nv" EN DERNIER, et seulement si tout est passe : un ajout de volet qui
     * echoue n'est donc jamais annonce, et la lecture reste coherente. */
    if (tx->err == ESP_OK) {
        esp_err_t e = nvs_set_u8(tx->h, "nv", (uint8_t)tx->n);
        if (e != ESP_OK) fail(tx, "nv", e);
    }
    esp_err_t c = nvs_commit(tx->h);
    if (c != ESP_OK) fail(tx, "(commit)", c);
    if (tx->err == ESP_OK) {
        /* Volets supprimes : leurs cles ne sont plus lues, on rend la place.
         * Une cle absente renvoie ESP_ERR_NVS_NOT_FOUND, attendu et ignore. */
        for (int i = tx->n; i < CFG_STORE_MAX_DOCS; i++) {
            char k[8]; volet_key(k, sizeof(k), i);
            nvs_erase_key(tx->h, k);
        }
        nvs_commit(tx->h);
    }
    nvs_close(tx->h);
    tx->open = false;
    return tx->err;
}

static char *get_str_alloc(nvs_handle_t h, const char *key) {
    size_t sz = 0;
    if (nvs_get_str(h, key, NULL, &sz) != ESP_OK || sz == 0) return NULL;
    char *s = malloc(sz);
    if (!s) return NULL;
    if (nvs_get_str(h, key, s, &sz) != ESP_OK) { free(s); return NULL; }
    return s;
}

cfg_layout_t cfg_store_load(cfg_store_doc_cb cb, void *ctx, cfg_store_legacy_check legacy_has_volets) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return CFG_LAYOUT_NONE;
    cfg_layout_t lay = CFG_LAYOUT_NONE;
    uint8_t nv = 0;
    bool split = nvs_get_u8(h, "nv", &nv) == ESP_OK;
    char *legacy = get_str_alloc(h, "cfg");
    /* L'ancienne chaine "cfg" et les cles par volet ne coexistent que dans deux
     * cas, et il faut choisir laquelle fait foi :
     *  - migration interrompue avant l'effacement de "cfg" : les deux disent la
     *    meme chose, ou les cles par volet sont incompletes -> "cfg" ;
     *  - retour a un ancien firmware, qui reecrit "cfg" sans toucher aux cles
     *    par volet. S'il a pu enregistrer sa config, les cles par volet sont
     *    PERIMEES -> "cfg". Mais sur une NVS de 16 Ko, ces cles lui prennent la
     *    place : il n'arrive a ecrire qu'une config VIDE (constate sur boitier).
     *    La preferer effacerait les volets ; on garde alors les cles par volet.
     * D'ou la regle : "cfg" fait foi si elle contient au moins un volet, ou
     * s'il n'y a rien d'autre. finish_boot() la reecrit puis l'efface. */
    bool use_legacy = legacy && !(split && nv > 0 && legacy_has_volets && !legacy_has_volets(legacy));
    if (use_legacy) {
        lay = CFG_LAYOUT_LEGACY;
        cb(ctx, CFG_DOC_LEGACY, legacy);
    } else if (split) {
        lay = CFG_LAYOUT_SPLIT;
        char *d = get_str_alloc(h, "hdr");
        if (d) { cb(ctx, CFG_DOC_HDR, d); free(d); }
        for (int i = 0; i < nv && i < CFG_STORE_MAX_DOCS; i++) {
            char k[8]; volet_key(k, sizeof(k), i);
            d = get_str_alloc(h, k);
            if (d) { cb(ctx, i, d); free(d); }
            else ESP_LOGW(TAG, "%s annonce par nv=%u mais illisible", k, nv);
        }
    }
    free(legacy);
    nvs_close(h);
    return lay;
}

static esp_err_t erase_one(const char *key) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_erase_key(h, key);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

esp_err_t cfg_store_finish_boot(cfg_layout_t lay, bool frames_stored, esp_err_t (*save)(void)) {
    /* 1. Le dataset d'abord : sur une NVS de 16 Ko saturee, c'est lui qui rend
     *    la place dont l'etape 2 a besoin. Sans effet s'il est deja parti. */
    if (frames_stored && erase_one("framesv2") != ESP_OK)
        ESP_LOGW(TAG, "ancien dataset de trames non efface");
    /* 2. Ancien format : ecrire le nouveau, et n'effacer l'ancien qu'APRES. Un
     *    echec garde l'ancien format, retente au prochain demarrage. */
    if (lay == CFG_LAYOUT_LEGACY) {
        esp_err_t e = save();
        if (e != ESP_OK) return e;
    }
    if (lay != CFG_LAYOUT_NONE && erase_one("cfg") != ESP_OK)
        ESP_LOGW(TAG, "ancienne cle de config non effacee");
    return ESP_OK;
}

size_t cfg_store_read_legacy_frames(void *buf, size_t cap) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t sz = cap;
    esp_err_t e = nvs_get_blob(h, "framesv2", buf, &sz);
    nvs_close(h);
    return e == ESP_OK ? sz : 0;
}
