/* Le catalogue d'OpenProfalux (web/profiles.json, modele de docs/PROFILES.md), lu au demarrage.
 *
 * Le firmware n'ecrit plus en dur les donnees d'un protocole : codes des commandes, trames par
 * appui, salves de l'enrolement, modeles reconnus. Il les lit ici. Le codage sur l'air (preambule,
 * bits, chiffrement, timings d'une norme) reste dans chaque encodeur : c'est du code.
 *
 * Unite PURE : catalog_load() prend le texte, sans flash ni NVS. main.c lui donne le fichier
 * embarque ; le banc firmware/test/catalog lui donne le vrai profiles.json. Si la lecture echoue,
 * catalog_code() et catalog_protocol() ne rendent rien, et les emetteurs refusent d'emettre : pas
 * de valeur de secours, qui serait une constante en dur de plus. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Commandes d'un appareil (catalogue « commands »). */
typedef enum { CAT_CMD_UP, CAT_CMD_STOP, CAT_CMD_DOWN, CAT_CMD_PROG, CAT_CMD_COUNT } catalog_cmd_t;
/* Encodeurs de ce firmware (catalogue « encoder ») : un protocole au plus par encodeur, tant que la
 * fiche d'un appareil ne designe pas son profil (#134, etape 3). */
typedef enum { CAT_ENC_KEELOQ, CAT_ENC_SOMFY_RTS, CAT_ENC_RAW_REPLAY, CAT_ENC_COUNT } catalog_encoder_t;

#define CATALOG_NO_CODE      (-1)
#define CATALOG_MODELS_MAX   16
#define CATALOG_NAME_MAX     48
#define CATALOG_ID_MAX       32
#define CATALOG_ERR_MAX      96
#define CATALOG_TYPES_MAX    8
#define CATALOG_PROFILES_MAX 24
#define CATALOG_NO_LEGACY    (-1)

typedef struct {
    bool     present;
    int8_t   code[CAT_CMD_COUNT];   /* code de chaque commande ; CATALOG_NO_CODE si le protocole ne l'a pas */
    uint8_t  press_frames;          /* trames emises pour un appui */
    uint32_t press_gap_us;          /* silence entre deux trames ; 0 = celui de l'encodeur */
    struct {
        int8_t   code;              /* code de la trame d'enrolement (KeeLoq) ; CATALOG_NO_CODE sinon */
        int8_t   command;           /* commande d'enrolement (Somfy PROG) ; -1 sinon */
        uint8_t  frames_per_burst;
        uint8_t  bursts;
        uint16_t burst_gap_ms;
    } enrol;
} catalog_protocol_t;

typedef struct {
    char name[CATALOG_NAME_MAX];    /* modele de telecommande que l'etape identify reconnait */
    char profile[CATALOG_ID_MAX];   /* le profil qui le reconnait */
} catalog_model_t;

/* Un type d'appareil (catalogue « types »). */
typedef struct {
    char key[CATALOG_ID_MAX];       /* la cle, ecrite dans la fiche (« volet », « portail ») */
    int  legacy;                    /* mode legacy : numero des fiches v0.2.16, CATALOG_NO_LEGACY sinon */
    bool positional;
} catalog_type_t;

/* Un profil RESOLU (heritage applique) : ce qu'une fiche herite quand elle laisse un reglage a 0. */
typedef struct {
    char              id[CATALOG_ID_MAX];
    char              brand[CATALOG_NAME_MAX];   /* vide si le profil n'a pas de marque */
    char              type[CATALOG_ID_MAX];      /* son premier type */
    catalog_encoder_t encoder;                   /* celui de son protocole */
    bool              enrol;                     /* add = enrol (sinon copy) */
    uint32_t          freq_khz;
    uint16_t          te_us;                     /* 0 = le profil n'en dit rien */
    int8_t            repeat_bit, rmt;           /* -1 = le profil n'en dit rien */
    uint8_t           frame_bits;                /* 0 = le profil n'en dit rien */
} catalog_profile_t;

/* Lit le catalogue. 0 si tout est lu ; -1 sinon, avec la raison dans err (et le catalogue vide). */
int catalog_load(const char *json, char *err, size_t errlen);
/* Le protocole d'un encodeur, NULL si le catalogue n'est pas lu ou ne le declare pas. */
const catalog_protocol_t *catalog_protocol(catalog_encoder_t enc);
/* Code d'une commande pour un encodeur, CATALOG_NO_CODE si aucun. */
int catalog_code(catalog_encoder_t enc, catalog_cmd_t cmd);
/* Commande d'un nom (« up », « stop »...), -1 si inconnu. */
int catalog_cmd_of(const char *name);
/* Modeles reconnus, dans l'ordre du catalogue. L'ordre est un CONTRAT : l'identite d'enrolement
 * garde en NVS le numero de son modele. On ajoute a la fin, on ne reordonne pas. */
int                    catalog_model_count(void);
const catalog_model_t *catalog_model(int i);   /* NULL hors borne */

/* Le type d'une cle, NULL si le catalogue ne le declare pas. */
const catalog_type_t *catalog_type(const char *key);
/* Mode legacy : la cle du type d'un ancien numero, NULL si aucun type ne le porte. */
const char *catalog_type_of_legacy(int code);
/* Le profil d'un identifiant, NULL s'il n'existe pas. */
const catalog_profile_t *catalog_profile(const char *id);
/* Le profil d'une fiche ecrite avant les profils (v0.2.16), d'apres ce qu'elle dit d'elle-meme : son
 * encodeur, enrolee ou copiee, et sa marque. Parmi les profils de cet encodeur et de cette methode, le
 * premier de cette marque, sinon le premier tout court ; NULL s'il n'y en a aucun. Meme regle que
 * l'interface d'avant (profileOf : marque et mecanisme). */
const catalog_profile_t *catalog_profile_infer(catalog_encoder_t enc, bool enrol, const char *brand);
