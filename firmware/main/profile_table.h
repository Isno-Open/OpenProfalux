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
} profile_def_t;

extern const profile_def_t PROFILE_TABLE[];
extern const int PROFILE_TABLE_COUNT;

/* Les moteurs que ce firmware execute. PROFILE_ENGINE_UNKNOWN : un profil d'une version plus recente,
 * dont le moteur n'est pas ici. */
typedef enum {
    PROFILE_ENGINE_UNKNOWN = 0,
    PROFILE_ENGINE_REPLAY,   /* "rejeu-brut" : rejoue la capture telle quelle (code fixe) */
    PROFILE_ENGINE_KEELOQ,   /* "keeloq"     : code roulant KeeLoq (Profalux et sa famille) */
    PROFILE_ENGINE_SOMFY,    /* "somfy-rts"  : Somfy RTS */
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
 *   virtuel Somfy -> enrol + somfy-rts ; virtuel -> enrol + keeloq ;
 *   rejeu brut -> copy + rejeu-brut ; sinon -> copy + keeloq (clone).
 * Sans marque, celle du protocole (Somfy RTS, CAME pour un rejeu, Profalux sinon). Si aucun profil de la
 * marque n'a cette methode, le premier de la methode. NULL si aucun profil ne convient. */
const profile_def_t *profile_resolve(const char *marque, bool virt, bool somfy, bool rejeu);

/* Le profil a garder dans la fiche d'un appareil : `current` (l'id deja grave) s'il est connu et reste
 * coherent avec la fiche (meme methode, et meme marque quand la fiche en porte une), sinon celui que
 * retrouve profile_resolve. Un profil donne explicitement est donc garde, et une fiche dont la marque ou
 * la methode change suit son nouveau profil. */
const profile_def_t *profile_for_device(const char *current, const char *marque, bool virt, bool somfy, bool rejeu);
