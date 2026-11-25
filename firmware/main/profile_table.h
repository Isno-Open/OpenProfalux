/*
 * profile_table.h : les profils que le firmware connait (issue #134, etape 2).
 *
 * La table PROFILE_TABLE est ENGENDREE a la construction depuis web/profiles.json par
 * tools/gen_profiles.py (profiles_gen.c, dans le dossier de construction) : les profils vivent dans
 * profiles.json et nulle part ailleurs. Ici, seulement ce qui est du CODE : les moteurs et les methodes
 * d'ajout que ce firmware sait executer, et la facon de retrouver le profil d'un appareil.
 *
 * Module PUR (aucune dependance ESP-IDF) : teste sur l'hote, contre le vrai catalogue
 * (firmware/test/profile-table).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Un profil, tel que la construction l'engendre. Le moteur et la methode restent des chaines : c'est
 * ce module qui les reconnait (profile_engine_of, profile_add_of), pas le generateur. */
typedef struct {
    const char *id;        /* identifiant stable, grave dans la fiche de l'appareil */
    const char *brand;     /* marque, gravee dans la fiche (champ marque) */
    uint8_t     type_code; /* dev_type_t du type du profil */
    const char *add;       /* methode d'ajout : "copy" ou "enrol" */
    const char *engine;    /* moteur : "rejeu-brut", "keeloq", "somfy-rts"... */
    const char *band;      /* bande radio : "868", "433" */
    uint32_t    freq_khz;  /* porteuse par defaut du profil (0 = celle de la radio) */
    /* Ce que le moteur emet (lot 2b) : 0 ou -1 = non declare par le profil. */
    uint8_t     frames_per_press;   /* trames par appui (tx.frames_per_press) */
    int8_t      code_up, code_stop, code_down;   /* code de bouton dans la trame du moteur (frames.<bouton>.code) */
    uint8_t     repeats;            /* rejeu : repetitions de la trame captee (tx.repeats) */
    uint32_t    gap_us;             /* rejeu : silence entre deux repetitions (tx.gap_us) */
    int16_t     io_man_id;          /* io-homecontrol : code fabricant du transfert de cle (io.man_id), -1 sinon */
    bool        has_enrol;          /* trame d'apprentissage declaree (enrol) */
    uint8_t     enrol_code;         /* code de bouton de la trame d'apprentissage */
    uint8_t     enrol_frames_per_burst;
    uint8_t     enrol_bursts;
    uint16_t    enrol_counter_start;   /* compteur minimal de la trame d'apprentissage */
} profile_def_t;

extern const profile_def_t PROFILE_TABLE[];
extern const int PROFILE_TABLE_COUNT;

/* Un type d'appareil (profiles.json « types »), engendre de meme : ce que le firmware en a besoin pour s'annoncer. */
typedef struct {
    const char *key;              /* cle du type : "volet", "portail" (valeur du fil, MQTT et portail cloud) */
    uint8_t     code;             /* dev_type_t */
    bool        positional;       /* a une position (temps de course) */
    const char *ha_device_class;  /* classe de l'appareil dans Home Assistant : "shutter", "gate"... */
} type_def_t;

extern const type_def_t TYPE_TABLE[];
extern const int TYPE_TABLE_COUNT;

/* Le type de code `code`, ou NULL. */
const type_def_t *type_find(uint8_t code);

/* Les moteurs que ce firmware execute. PROFILE_ENGINE_UNKNOWN : un profil d'une version plus recente,
 * dont le moteur n'est pas ici. */
typedef enum {
    PROFILE_ENGINE_UNKNOWN = 0,
    PROFILE_ENGINE_REPLAY,   /* "rejeu-brut" : rejoue la capture telle quelle (code fixe) */
    PROFILE_ENGINE_KEELOQ,   /* "keeloq"     : code roulant KeeLoq (Profalux et sa famille) */
    PROFILE_ENGINE_SOMFY,    /* "somfy-rts"  : Somfy RTS */
    PROFILE_ENGINE_IO,       /* "io-homecontrol" : io-homecontrol 1W (Velux), #87, #118 */
} profile_engine_t;

typedef enum {
    PROFILE_ADD_UNKNOWN = 0,
    PROFILE_ADD_COPY,        /* "copy"  : on reprend la telecommande d'origine (rejeu ou clonage) */
    PROFILE_ADD_ENROL,       /* "enrol" : le boitier devient une nouvelle telecommande */
} profile_add_t;

profile_engine_t profile_engine_of(const profile_def_t *p);
profile_add_t    profile_add_of(const profile_def_t *p);

/* Le profil d'identifiant `id`, ou NULL. */
const profile_def_t *profile_find(const char *id);

/* Le profil d'un appareil qui n'en porte pas encore (fiche anterieure a l'etape 2) : celui de sa marque
 * et de sa methode, deduite de la fiche comme le fait l'interface (profiles.ts, methodOf) :
 *   virtuel -> enrol + le moteur de son protocole (virt_engine : keeloq, somfy-rts, io-homecontrol) ;
 *   rejeu brut -> copy + rejeu-brut ; sinon -> copy + keeloq (clone).
 * Sans marque, celle du protocole (Somfy RTS, Velux, CAME pour un rejeu, Profalux sinon). Si aucun profil de la
 * marque n'a cette methode, le premier de la methode. NULL si aucun profil ne convient. virt_engine est ignore
 * pour un appareil qui n'est pas virtuel. */
const profile_def_t *profile_resolve(const char *brand, bool virt, profile_engine_t virt_engine, bool replay);

/* Le profil a garder dans la fiche d'un appareil : `current` (l'id deja grave) s'il est connu et reste
 * coherent avec la fiche (meme methode, et meme marque quand la fiche en porte une), sinon celui que
 * retrouve profile_resolve. Un profil donne explicitement est donc garde, et une fiche dont la marque ou
 * la methode change suit son nouveau profil. */
const profile_def_t *profile_for_device(const char *current, const char *brand, bool virt, profile_engine_t virt_engine, bool replay);

/* Ce que le moteur du profil exige pour emettre, verifie par le banc contre le vrai catalogue (et journalise au
 * demarrage par le firmware) : KeeLoq, des trames par appui, et en enrolement les codes de bouton et la trame
 * d'apprentissage ; rejeu, ses repetitions et son silence. Rend true si le profil est complet, sinon false et
 * la raison dans *why. */
bool profile_engine_ready(const profile_def_t *p, const char **why);

/* Commandes qui ont un code de bouton dans un profil. */
typedef enum { PROFILE_CMD_UP, PROFILE_CMD_STOP, PROFILE_CMD_DOWN } profile_cmd_t;
/* Code du bouton `cmd` dans la trame du moteur, ou -1 si le profil n'en declare pas. */
int profile_button_code(const profile_def_t *p, profile_cmd_t cmd);
