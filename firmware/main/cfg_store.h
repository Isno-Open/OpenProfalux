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
 * cfg_store ne lit pas le JSON : les documents sont produits et relus par
 * cfg_model.c. Les deux modules ne dependent que de la NVS, de cJSON et de
 * stdio : ils se compilent aussi pour le PC (cible linux d'ESP-IDF). Leur banc
 * de test, sur une NVS de 16 Ko emulee avec coupures de courant simulees, est
 * dans firmware/test/cfg_store.
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
 * cb(ctx, CFG_DOC_LEGACY, ...) une seule fois pour l'ancien format.
 *
 * Si l'ancienne chaine "cfg" coexiste avec les cles par volet (migration
 * interrompue, ou retour a un ancien firmware), elle fait foi des qu'elle
 * contient au moins un volet : voir cfg_store.c. cfg_store ne lit pas le JSON,
 * c'est legacy_has_volets() qui le lui dit (NULL : "cfg" fait toujours foi). */
typedef void (*cfg_store_doc_cb)(void *ctx, int index, const char *doc);
typedef bool (*cfg_store_legacy_check)(const char *doc);
cfg_layout_t cfg_store_load(cfg_store_doc_cb cb, void *ctx, cfg_store_legacy_check legacy_has_volets);

/* Ancien dataset de trames ("framesv2", jusqu'a 4 Ko) : il migre vers SPIFFS.
 * Renvoie sa taille (0 s'il n'existe plus). L'appelant l'ecrit dans son fichier
 * et l'indique a cfg_store_finish_boot(), qui l'efface alors de la NVS. */
size_t    cfg_store_read_legacy_frames(void *buf, size_t cap);

/* A appeler une fois au demarrage, apres cfg_store_load() : termine le passage
 * au nouveau rangement, dans l'ORDRE qu'exige une NVS de 16 Ko saturee.
 *  1. si frames_stored (le dataset est desormais en fichier, hors NVS), son
 *     ancienne copie est effacee de la NVS ;
 *  2. si lay == CFG_LAYOUT_LEGACY, save() ecrit la config au nouveau format ;
 *     l'ancienne cle n'est effacee qu'apres une ecriture reussie.
 * Une coupure a n'importe quel moment laisse une config lisible. Renvoie
 * l'erreur de save() si la migration n'a pas pu se faire (ancien format
 * conserve, retente au prochain demarrage). */
esp_err_t cfg_store_finish_boot(cfg_layout_t lay, bool frames_stored, esp_err_t (*save)(void));
