#pragma once
/* Rangement de la config des volets en NVS : UNE CLE PAR VOLET.
 *
 * Pourquoi : l'ancienne config tenait dans UNE seule chaine NVS ("cfg"). Deux
 * limites en decoulaient :
 *   - une chaine NVS est plafonnee a 4000 octets (une page) quelle que soit la
 *     taille de la partition : ~10 volets au plus ;
 *   - la NVS ecrit la nouvelle version AVANT d'effacer l'ancienne : chaque
 *     mouvement de volet exigeait ~2,6 Ko libres d'un seul tenant. Sur la NVS de
 *     16 Ko, partagee avec la pile Wi-Fi et la calibration radio, ils
 *     manquaient : TOUTES les sauvegardes echouaient en silence.
 *
 * Desormais : "hdr" (ecoute permanente + telecommandes), "v0".."vN" (un document
 * JSON par volet, ~400 o) et "nv" (nombre de volets, ecrit EN DERNIER). La NVS ne
 * reecrit pas une valeur identique : quand un volet bouge, seule sa cle change.
 *
 * Coherence en cas de coupure : chaque cle est atomique en NVS, et "nv" n'est
 * ecrit que si tous les documents l'ont ete. Au pire, un volet garde son etat
 * precedent ; la config n'est jamais perdue.
 *
 * Ce module ne depend que de la NVS : il se compile aussi pour le PC (cible
 * linux d'ESP-IDF), ce qui permet de le tester sur une NVS de 16 Ko emulee.
 */
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"

#define CFG_STORE_MAX_DOCS 24      /* = SH_MAX_VOLETS */

typedef enum {
    CFG_LAYOUT_NONE,               /* rien en NVS (boitier neuf) */
    CFG_LAYOUT_SPLIT,              /* une cle par volet (ce format) */
    CFG_LAYOUT_LEGACY,             /* ancienne chaine unique "cfg" */
} cfg_layout_t;

#define CFG_DOC_HDR    (-1)        /* document d'en-tete (hors volets) */
#define CFG_DOC_LEGACY (-2)        /* ancienne config complete, a migrer */

/* Ecriture "en flux" : un document a la fois, pour ne jamais tenir toute la
 * config en RAM. begin -> put_hdr -> put_volet (x n) -> end. */
typedef struct {
    nvs_handle_t h;
    bool         open;
    esp_err_t    err;              /* premiere erreur rencontree */
    int          n;                /* volets ecrits */
    char         failed_key[8];    /* cle de la premiere erreur (diagnostic) */
} cfg_store_tx_t;

esp_err_t cfg_store_begin(cfg_store_tx_t *tx);
void      cfg_store_put_hdr(cfg_store_tx_t *tx, const char *doc);
void      cfg_store_put_volet(cfg_store_tx_t *tx, const char *doc);
/* Ecrit "nv" si tout a reussi, efface les cles de volets en trop, ferme. */
esp_err_t cfg_store_end(cfg_store_tx_t *tx);

/* Lit la config : cb(ctx, CFG_DOC_HDR, ...) puis cb(ctx, i, ...) par volet, ou
 * cb(ctx, CFG_DOC_LEGACY, ...) une seule fois pour l'ancien format. */
typedef void (*cfg_store_doc_cb)(void *ctx, int index, const char *doc);
cfg_layout_t cfg_store_load(cfg_store_doc_cb cb, void *ctx);

/* Efface l'ancienne chaine "cfg". A n'appeler qu'APRES une ecriture reussie au
 * nouveau format. Sans effet si elle n'existe pas. */
esp_err_t cfg_store_drop_legacy(void);

/* Ancien dataset de trames ("framesv2", jusqu'a 4 Ko) : il migre vers SPIFFS.
 * read -> l'appelant l'ecrit dans son fichier -> drop seulement si c'est fait. */
size_t    cfg_store_read_legacy_frames(void *buf, size_t cap);
esp_err_t cfg_store_drop_legacy_frames(void);
