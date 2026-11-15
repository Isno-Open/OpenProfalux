/* Description d'un appareil pour le portail cloud (OpenCloudApp) : le message retenu
 * openprofalux/cover/<slug>/attrs, que le portail lit pour choisir la carte (type declare par un
 * plugin, positionnel ou non). Unite PURE, testee sur PC (test/cover-attrs). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Nom d'un dev_type_t tel que profiles.json le declare (cle de "types"), NULL si inconnu. */
const char *dev_type_name(uint8_t type);

/* Bornes d'un temps de course (montee ou descente), en millisecondes. MIROIR de profiles.json
 * types.volet.travel_s (min, max en secondes), d'ou le portail tire les bornes de son formulaire :
 * le banc test/cover-attrs refuse toute divergence. */
#define COVER_TRAVEL_MIN_MS 1000u
#define COVER_TRAVEL_MAX_MS 180000u
/* Temps de course acceptable (etalonnage) ? 0 ou hors bornes : non. */
bool cover_travel_ok(uint32_t ms);

/* Nombre maximal d'appareils d'un boitier (rang dans la liste : 1..COVER_DEVICES_MAX). MIROIR de
 * SH_MAX_VOLETS et de profiles.json devices_max : le banc test/cover-attrs refuse toute divergence. */
#define COVER_DEVICES_MAX 24
/* Orientation d'une facade : azimut en degres, 0 (nord) a 359 ; -1 = non definie. */
#define COVER_ORIENTATION_MAX 359

/* Role d'un appareil : simple, CENTRALE (commande diffusee a ses membres) ou ZONE (telecommande de
 * piece, ses suiveurs reproduisent ses mouvements). Noms MIROIR des cles de profiles.json "roles" : le
 * banc test/cover-attrs refuse toute divergence. */
typedef enum { COVER_ROLE_SIMPLE = 0, COVER_ROLE_CENTRAL, COVER_ROLE_ZONE, COVER_ROLE_COUNT } cover_role_t;
/* Nom d'un role tel que profiles.json le declare, NULL pour un appareil simple ou un role inconnu. */
const char *cover_role_name(cover_role_t role);

/* Description d'un appareil pour le portail. name NULL ou vide : pas de champ name ; temps de course
 * publies seulement si les deux sont dans les bornes (etalonne) ; rank publie si >= 1 ; orientation
 * publiee si 0..359. */
typedef struct {
    uint8_t     type;
    bool        positional;
    const char *name;
    uint32_t    up_ms, down_ms;
    int         rank;          /* 1..COVER_DEVICES_MAX, 0 = non range */
    int         orientation;   /* 0..359, -1 = non definie */
    cover_role_t role;
    const char  *members;      /* centrale ou zone : SLUGS de ses membres separes par des virgules (NULL = aucun) */
} cover_attrs_t;

/* Ecrit {"type":"<nom>","positional":true|false,"name":"...","travel_up_s":N,"travel_down_s":N,
 * "rank":N,"orientation":N,"role":"central|zone","members":["slug",...]} dans out (role et members
 * seulement pour une centrale ou une zone, members toujours present alors, meme vide) (champs facultatifs selon cover_attrs_t). Le nom est celui que
 * l'utilisateur a donne (accents compris, en UTF-8), echappe pour JSON. Rend la longueur, ou -1 si le
 * type est inconnu ou si le tampon est trop petit (rien n'est alors ecrit). */
int cover_attrs_json(const cover_attrs_t *a, char *out, size_t cap);

/* Ecrit s en chaine JSON (guillemets compris, caracteres speciaux echappes, UTF-8 intact) dans out.
 * Rend la longueur, ou -1 si ca ne tient pas (out vide). Sert aux textes choisis par l'utilisateur
 * (nom d'un appareil, SSID). */
int json_string(const char *s, char *out, size_t cap);
