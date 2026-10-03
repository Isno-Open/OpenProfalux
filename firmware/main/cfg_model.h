#pragma once
/* Config persistante des volets : le modele en memoire et sa serialisation JSON.
 *
 * Ce code vivait dans shutters.c. Il en est sorti pour pouvoir etre teste sur PC
 * (cible linux d'ESP-IDF, voir firmware/test/cfg_store) : il ne depend que de
 * cJSON, de la NVS (via cfg_store) et de stdio. Ni FreeRTOS, ni radio, ni MQTT.
 *
 * Le FORMAT JSON est inchange : c'est celui de l'export (Systeme > Sauvegarde)
 * et de l'ancienne cle NVS unique. La NVS range un document par volet
 * (cfg_store.h) ; les sauvegardes restent compatibles dans les deux sens.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"
#include "cfg_store.h"
#include "shutters.h"

#define SH_MAX_REMOTES 16

typedef struct {
    char id[SH_ID_LEN];
    char serials[SH_MAX_SERIALS][SH_SERIAL_LEN];
    int  n_serials;
    char up[SH_BITS_LEN], down[SH_BITS_LEN], stop[SH_BITS_LEN];
    uint8_t up_btn, down_btn, stop_btn;   /* code bouton appris (pour la sync RX) */
    /* Volet VIRTUEL (identite 0x067 enrolee) : au lieu de rejouer up/down/stop captes,
     * on GENERE la trame a chaque appui (keeloq + compteur roulant). */
    bool     virt;
    uint32_t virt_serial;    /* serial 0x067 (contient le slot ; cle via pfx_key_for_serial) */
    uint16_t virt_counter;   /* compteur roulant courant */
    uint16_t virt_te;        /* TE d'emission du modele (415 FranciaFlex, 455 Profalux) */
    /* Centrale virtuelle : diffuse ouvrir/fermer/stop a un groupe de volets membres (CSV d'ids). */
    bool     central;
    char    *members;        /* "id1,id2,..." sur le tas, a sa taille ; NULL = aucun membre.
                              * Ne s'ecrit que par cfg_volet_set_members(). */
    uint32_t travel_up_ms, travel_down_ms;
    int   orientation;       /* azimut de la facade (0..359, -1 = non defini) : automatisations soleil HA */
    int   order;             /* rang d'affichage dans l'UI (croissant). -1 = pas encore range :
                              * l'UI place alors le volet a la fin, dans son ordre de creation. */
    float position;          /* 0..100 */
    /* ── Etat d'execution, NON persiste ── */
    int   dir;               /* -1 down, 0 stop, +1 up */
    int   target;            /* -1 = aucun, sinon 0..100 */
    bool  own_move;          /* true = notre commande (on stream) ; false = externe (on suit sans emettre) */
    int64_t last_tick_us;
    int   pub_pos;           /* derniere position publiee MQTT (evite le spam) */
    int   pub_dir;           /* derniere direction publiee MQTT */
} volet_t;

/* dframe_t (trame distincte du dataset slide : hop brut + bouton + t) est defini dans shutters.h. */
typedef struct {
    char serial[SH_SERIAL_LEN]; char name[SH_ID_LEN];
    /* ── Etat d'execution, NON persiste ici (le dataset a son propre fichier) ── */
    uint32_t  last_hop;      /* fast-path : dernier hop (evite le scan sur les maintiens) */
    dframe_t *hops;          /* SET des trames distinctes (dedup par hop) : hop + bouton + t */
    uint16_t  nhops;         /* = nb de trames DISTINCTES loggees */
    uint16_t  caphops;       /* capacite allouee du set */
} remote_t;

/* La config, la ou elle vit (tableaux de shutters.c, ou ceux d'un test). */
typedef struct {
    bool     *log_frames;                 /* ecoute permanente */
    remote_t *remotes;  int *nremotes;    /* SH_MAX_REMOTES places */
    volet_t  *volets;   int *nvolets;     /* SH_MAX_VOLETS places */
} cfg_model_t;

/* ── Membres d'une centrale ──
 * La liste vit sur le tas, et seulement pour une centrale : un tampon fixe dans
 * volet_t se payait sur chacun des SH_MAX_VOLETS volets, et a 384 octets il
 * tronquait en silence les listes longues (les volets coupes ne recevaient plus
 * rien). Tout volet_t se libere par cfg_volet_release(), ou cfg_model_remove(). */

/* Remplace la liste (NULL ou "" : aucun membre). Renvoie -1, liste inchangee,
 * si elle depasse SH_MEMBERS_LEN - 1 caracteres ou si la memoire manque. */
int  cfg_volet_set_members(volet_t *v, const char *csv);
/* Libere ce que possede le volet (sa liste de membres). */
void cfg_volet_release(volet_t *v);
/* Retire le volet idx du tableau du modele, en liberant ce qu'il possede. */
void cfg_model_remove(const cfg_model_t *m, int idx);
/* Parcourt une liste de membres, sans la copier ni la modifier :
 *   for (p = cfg_members_next(csv, id); p; p = cfg_members_next(p, id)) ...
 * id recoit le membre suivant (espaces de tete ignores, elements vides sautes),
 * ou "" s'il est plus long qu'un identifiant : il ne designe alors aucun volet. */
const char *cfg_members_next(const char *p, char id[SH_ID_LEN]);

/* Toute la config (telecommandes + noms + trames de reference + calibration) en
 * UN document : l'export, et l'ancienne cle NVS unique. A liberer par l'appelant. */
char *cfg_model_export(const cfg_model_t *m);
/* Ajoute au modele le contenu d'un tel document. Le modele doit avoir ete remis
 * a zero avant. Un identifiant de volet deja present est ignore. */
void  cfg_model_import(const cfg_model_t *m, cJSON *root);

/* Ecrit la config en NVS, un document par volet. En cas d'echec, failed_key
 * (8 octets, peut etre NULL) recoit la cle fautive. */
esp_err_t    cfg_model_save(const cfg_model_t *m, char *failed_key);
/* Lit la config depuis la NVS, quel que soit son format. Le modele doit avoir
 * ete remis a zero avant. */
cfg_layout_t cfg_model_load(const cfg_model_t *m);

/* Dataset de trames : lu dans `path` s'il existe ; sinon repris de son ancienne
 * copie NVS et range dans `path`. *sz recoit la taille lue. Renvoie true si le
 * dataset est en fichier : sa copie NVS peut alors etre effacee
 * (cfg_store_finish_boot). Le fichier est ecrit sous un nom temporaire puis
 * renomme : s'il ne peut pas etre ecrit, ou si le courant est coupe pendant
 * l'ecriture, le dataset est quand meme rendu et sa copie NVS conservee. */
bool cfg_frames_load(const char *path, void *buf, size_t cap, size_t *sz);
