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

/* Ecrit {"type":"<nom>","positional":true|false,"name":"<nom de l'appareil>",
 * "travel_up_s":N,"travel_down_s":N} dans out. Le nom est celui que l'utilisateur a donne (accents
 * compris, en UTF-8), echappe pour JSON : le portail l'affiche tel quel, au lieu du slug des topics ;
 * name NULL ou vide : pas de champ name. Les temps de course (en secondes arrondies) ne partent que si
 * le volet est etalonne (les deux dans les bornes). Rend la longueur, ou -1 si le type est inconnu ou
 * si le tampon est trop petit (rien n'est alors ecrit). */
int cover_attrs_json(uint8_t type, bool positional, const char *name, uint32_t up_ms, uint32_t down_ms,
                     char *out, size_t cap);

/* Ecrit s en chaine JSON (guillemets compris, caracteres speciaux echappes, UTF-8 intact) dans out.
 * Rend la longueur, ou -1 si ca ne tient pas (out vide). Sert aux textes choisis par l'utilisateur
 * (nom d'un appareil, SSID). */
int json_string(const char *s, char *out, size_t cap);
