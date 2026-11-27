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
#include "remote_battery.h"

#define SH_MAX_REMOTES 16
/* Nom du fichier d'une capture de rejeu : "rj" + 8 chiffres hexadecimaux + ".json", zero final compris. */
#define CFG_REPLAY_NAME_LEN 16

/* Protocole d'emission d'un volet VIRTUEL (qui GENERE sa trame). Profalux/KeeLoq est le
 * seul cable aujourd'hui, et vaut 0 : les volets deja persistes, sans ce champ, le
 * prennent par defaut. Les autres protocoles (Somfy RTS, issue #73, ou d'autres)
 * s'ajoutent ici et se cablent dans emit_virt (shutters.c). */
typedef enum {
    VOLET_PROTO_PROFALUX  = 0,   /* KeeLoq 0x067 : pfx_enrol_frame + compteur roulant */
    VOLET_PROTO_SOMFY_RTS = 1,   /* Somfy RTS : trame OOK 433,42 MHz (somfy_tx), code roulant */
} volet_proto_t;

#include "catalog.h"    /* types et profils du catalogue : la fiche les designe par leur nom */

/* Tri-etat d'une option d'emission PAR APPAREIL (bit de repetition, RMT). 0 = heriter du defaut
 * (profil, puis firmware) ; on force explicitement ensuite. La valeur 0 est la sentinelle "heriter"
 * pour qu'un volet fraichement memset() herite naturellement, et pour que la cle soit OMISE du JSON
 * quand on herite (sauvegardes existantes inchangees, issue #111). */
typedef enum {
    TXOVR_INHERIT = 0,   /* heriter du defaut du profil */
    TXOVR_OFF     = 1,   /* forcer desactive pour cet appareil */
    TXOVR_ON      = 2,   /* forcer active pour cet appareil */
} tx_override_t;

typedef struct {
    char id[SH_ID_LEN];
    /* Identifiant STABLE (cover_attrs.h, cover_uid_ok) : garde quand `id` change (renommage). Toujours
     * rempli une fois le modele charge (cfg_model_ensure_uids). */
    char uid[SH_ID_LEN];
    char serials[SH_MAX_SERIALS][SH_SERIAL_LEN];
    int  n_serials;
    char up[SH_BITS_LEN], down[SH_BITS_LEN], stop[SH_BITS_LEN];
    uint8_t up_btn, down_btn, stop_btn;   /* code bouton appris (pour la sync RX) */
    /* Volet VIRTUEL (identite 0x067 enrolee) : au lieu de rejouer up/down/stop captes,
     * on GENERE la trame a chaque appui (keeloq + compteur roulant). */
    bool     virt;
    uint32_t virt_serial;    /* serial 0x067 (contient le slot ; cle via pfx_key_for_serial) */
    uint16_t virt_counter;   /* compteur roulant courant */
    uint8_t  proto;          /* volet_proto_t : protocole d'emission (0 = Profalux, defaut) */
    /* Ce qu'on pilote : la CLE d'un type du catalogue (« volet », « portail »). Une fiche v0.2.16 portait
     * un numero (mode legacy), traduit a la lecture. Vide : type inconnu de ce catalogue. */
    char     type[CATALOG_ID_MAX];
    /* Le profil de l'appareil (catalogue) : un reglage d'emission laisse a 0 prend sa valeur. Vide :
     * appareil qu'on ne sait pas classer, qui garde le comportement d'avant les profils. */
    char     profile[CATALOG_ID_MAX];
    /* ── Parametres d'emission RESOLUS PAR APPAREIL (etage 2 de la pyramide, issue #111). ──
     * Chaque marque peut coexister sur une meme radio (Profalux 868425, Eveno/NeoSol 868330) : un
     * reglage global en casse une en reglant l'autre (bugs CAME datarate et Profalux/Eveno rmt/rpt
     * du 2026-11-01, plus l'incident client "ajouter de l'Eveno casse le Profalux enrole"). D'ou PAR
     * APPAREIL, avec sentinelle = heriter du defaut. Ne concernent que la generation KeeLoq
     * (clone/enrolement) ; le rejeu brut passe par radio_tx_ook_pulses et ne les lit pas. */
    uint32_t freq_khz;       /* porteuse de CET appareil (0 = heriter du defaut radio/profil) */
    uint16_t te_us;          /* TE d'emission KeeLoq (0 = heriter du defaut firmware, 400 us) */
    uint8_t  tx_rpt;         /* tx_override_t : bit de repetition pour cet appareil */
    uint8_t  tx_rmt;         /* tx_override_t : emission materielle RMT pour cet appareil */
    uint8_t  frame_bits;     /* Somfy RTS : format de trame (0 = defaut 56 ; 56 ou 80, le 80 ajoute une extension) */
    char     marque[SH_ID_LEN];  /* provenance : marque du profil choisi a l'ajout (affichage + resolution des defauts) */
    /* Centrale virtuelle : diffuse ouvrir/fermer/stop a un groupe de volets membres (CSV d'ids). */
    bool     central;
    char    *members;        /* "id1,id2,..." sur le tas, a sa taille ; NULL = aucun membre.
                              * Ne s'ecrit que par cfg_volet_set_members().
                              * Centrale : les volets a qui la commande est diffusee.
                              * Volet appris (ZONE) : ses SUIVEURS. Sa propre trame les fait
                              * bouger tous ; ils suivent ses mouvements sans rien emettre. */
    /* Rejeu BRUT par commande (code fixe OOK, agnostique) : JSON
     * {"f":freq_khz,"up":"<csv>","stop":"<csv>","down":"<csv>"} ; chaque csv = impulsions signees
     * (+us haut / -us bas). NULL = pas de rejeu brut. Heap, libere par cfg_volet_release. */
    char    *rejeu;
    uint32_t travel_up_ms, travel_down_ms;
    int   orientation;       /* azimut de la facade (0..359, -1 = non defini) : automatisations soleil HA */
    int   order;             /* rang d'affichage dans l'UI (croissant). -1 = pas encore range :
                              * l'UI place alors le volet a la fin, dans son ordre de creation. */
    float position;          /* 0..100 */
    /* ── Etat d'execution, NON persiste ── */
    /* Capture rangee en fichier (cfg_model_t.capture_dir) : son nom, et l'empreinte du contenu qui y est.
     * Un enregistrement ne reecrit le fichier que si la capture a change (chaque mouvement enregistre). */
    char     replay_file[CFG_REPLAY_NAME_LEN];
    uint32_t replay_hash;
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
    batt_state_t batt;       /* pile (bit VLOW des trames), recalcule apres chaque demarrage */
    uint32_t  batt_t;        /* derniere trame complete vue (epoch, 0 si SNTP pas encore synchro) */
} remote_t;

/* La config, la ou elle vit (tableaux de shutters.c, ou ceux d'un test). */
typedef struct {
    bool     *log_frames;                 /* ecoute permanente */
    remote_t *remotes;  int *nremotes;    /* SH_MAX_REMOTES places */
    volet_t  *volets;   int *nvolets;     /* SH_MAX_VOLETS places */
    /* Dossier des captures de rejeu (SPIFFS sur le boitier). NULL : elles restent dans le document NVS
     * de leur appareil. Une capture fait jusqu'a plusieurs Ko : en NVS (16 Ko, partages avec le Wi-Fi et
     * la calibration radio), deux d'entre elles ont suffi a remplir une Super. */
    const char *capture_dir;
} cfg_model_t;

/* ── Membres d'une centrale (ou suiveurs d'une zone) ──
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
/* Ce qu'emet un appareil : la valeur de sa fiche, sinon celle de son profil (p, NULL si aucun).
 * Sentinelles de sortie : freq_khz / te_us / frame_bits a 0 et rpt / rmt a -1 = ni la fiche ni le profil
 * n'en disent rien (l'emetteur garde alors son defaut, comme avant les profils). */
typedef struct { uint32_t freq_khz; uint16_t te_us; int8_t rpt, rmt; uint8_t frame_bits; } cfg_tx_t;
cfg_tx_t cfg_device_tx(const volet_t *v, const catalog_profile_t *p);
/* Migration d'une fiche qui vient de recevoir son profil : ecrit d'abord ce que l'appareil emet
 * AUJOURD'HUI (now : les defauts qu'il heritait avant, pour chaque reglage laisse a 0), puis remet a 0
 * ce qui egale le profil. Aucune emission ne change ; un appareil egal a son profil le suivra ensuite.
 * Renvoie true si la fiche a change. */
bool cfg_device_rebase_tx(volet_t *v, const catalog_profile_t *p, const cfg_tx_t *now);
/* Un reglage egal a celui du profil revient a 0 (heriter) : l'appareil suivra une correction du profil.
 * Sauf la porteuse d'un rejeu brut, dont le 0 veut dire « celle de la capture ».
 * Applique a la migration et a chaque ecriture des reglages (shutters_set_tx_params). */
void cfg_device_inherit_equal(volet_t *v, const catalog_profile_t *p);

/* Document de rejeu d'un appareil cree sans capture encore ("{}", sur le tas ; NULL sans memoire). */
char *cfg_replay_empty(void);
/* Fiche qui n'a rien a emettre : ni identite generee, ni rejeu, ni trame copiee (une centrale n'en est
 * pas une). Typiquement la coquille d'un appareil dont la capture n'a jamais ete enregistree. */
bool cfg_device_is_blank(const volet_t *v);
/* Ce que l'appareil dit de lui-meme : son encodeur. CAT_ENC_NONE pour une fiche vide, ou d'un protocole
 * que ce firmware ne connait pas (io, ecrit par une autre branche) : elle reste alors sans profil plutot
 * que de passer pour une copie Profalux. */
catalog_encoder_t cfg_device_encoder(const volet_t *v);
/* Donne son profil a un appareil : `wanted` s'il existe et convient a son encodeur et a sa methode,
 * sinon (s'il n'en a pas) celui de sa marque. Son type, s'il n'en a pas, est celui du profil. Une
 * centrale n'a pas de profil. Renvoie le profil de la fiche, ou NULL. */
const catalog_profile_t *cfg_device_assign_profile(volet_t *v, const char *wanted);
/* Profil d'une telecommande copiee (apprentissage, « Telecommandes captees ») : celui de sa marque, s'il
 * n'en a pas. Tout juste donne, la copie garde ce qu'emettait la radio (`radio` : porteuse reglee pour la
 * capter, par exemple 868330 kHz), pas le defaut du profil : fige, puis rendu au profil ce qui lui est
 * egal, comme a la migration (revue de la PR #156). Renvoie le profil de la fiche, ou NULL. */
const catalog_profile_t *cfg_device_assign_copy_profile(volet_t *v, const cfg_tx_t *radio);
/* Migration vers les profils (#134) : chaque fiche sans profil recoit le sien (cfg_device_assign_profile),
 * puis ses reglages sont rebases sur ce qu'elle emet aujourd'hui (keeloq, somfy : les defauts qu'elle
 * heritait). Une fiche vide ou inclassable reste telle quelle. Renvoie le nombre de fiches migrees. */
int cfg_model_migrate_profiles(const cfg_model_t *m, const cfg_tx_t *keeloq, const cfg_tx_t *somfy);

char *cfg_model_export(const cfg_model_t *m);

/* Taille d'une sauvegarde que le boitier sait restaurer : le corps de /api/restore et la copie d'avant
 * restauration sont lus ENTIERS en RAM (32 Ko libres sur un ATOM). On ne releve pas ce plafond ; on refuse
 * ce qui ferait deborder la sauvegarde : tout ce qui est sauvegarde se restaure (revue de la PR #156). */
#define CFG_BACKUP_MAX (24 * 1024)
/* Longueur exacte de cfg_model_export(m), calculee appareil par appareil, sans tenir l'export en RAM. */
size_t cfg_model_export_len(const cfg_model_t *m);
typedef enum {
    CFG_CAPTURE_OK          = 0,
    CFG_CAPTURE_ERR         = -1,   /* trame trop courte, plus de memoire */
    CFG_CAPTURE_BACKUP_FULL = -2,   /* la sauvegarde depasserait CFG_BACKUP_MAX : rien n'est change */
} cfg_capture_rc_t;
/* Range une trame captee (n impulsions : niveau lvl[i], duree us[i]) comme commande `cmd` du rejeu de v,
 * a la porteuse freq (kHz). `reserved` : ce que la sauvegarde porte hors de ce modele (table d'enrolement).
 * Si la sauvegarde depasserait alors CFG_BACKUP_MAX, la capture d'avant reste et rien n'est change. */
cfg_capture_rc_t cfg_device_store_capture(const cfg_model_t *m, volet_t *v, const char *cmd, uint32_t freq,
                                          const uint8_t *lvl, const uint16_t *us, int n, size_t reserved);
/* Ajoute au modele le contenu d'un tel document. Le modele doit avoir ete remis
 * a zero avant. Un identifiant de volet deja present est ignore. */
void  cfg_model_import(const cfg_model_t *m, cJSON *root);
/* Donne un identifiant stable a chaque volet qui n'en a pas (ou dont le sien est invalide, ou deja porte
 * par un volet place avant lui), sans toucher aux autres. Renvoie le nombre de volets servis. Appele
 * apres tout chargement, et avant toute ecriture : un volet tout juste cree le recoit la. */
int   cfg_model_ensure_uids(const cfg_model_t *m);
/* Est-ce une sauvegarde de config (format de cfg_model_export) ? Un objet avec un
 * tableau "volets", et "remotes" absent ou objet. L'export des trames, {} ou []
 * ne le sont pas : les restaurer remettait toute la config a zero. */
bool  cfg_model_is_backup(const cJSON *root);
/* Copie de la config courante (format de l'export) dans `path`, via cfg_write_file. */
bool  cfg_model_save_copy(const cfg_model_t *m, const char *path);

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

/* Ecrit `n` octets dans `path` sous un nom temporaire, renomme une fois complet :
 * une coupure de courant ne laisse jamais un fichier a moitie ecrit sous son vrai
 * nom (l'ancien reste intact). Renvoie false si rien n'a ete remplace. */
bool cfg_write_file(const char *path, const void *buf, size_t n);
/* Lit `path`, ou son temporaire complet si une coupure l'a laisse seul (voir
 * cfg_write_file), dans un tampon alloue termine par un zero. NULL si absent, vide
 * ou plus grand que `cap`. *n (peut etre NULL) recoit la taille. */
char *cfg_read_file(const char *path, size_t cap, size_t *n);
