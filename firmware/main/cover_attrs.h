/* Description d'un appareil pour le portail cloud (OpenCloudApp) : le message retenu
 * openprofalux/cover/<slug>/attrs, que le portail lit pour choisir la carte (type declare par un
 * plugin, positionnel ou non). Unite PURE, testee sur PC (test/cover-attrs). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Nom d'un dev_type_t tel que profiles.json le declare (cle de "types"), NULL si inconnu. */
const char *dev_type_name(uint8_t type);

/* Ecrit {"type":"<nom>","positional":true|false,"name":"<nom de l'appareil>"} dans out. Le nom est
 * celui que l'utilisateur a donne (accents compris, en UTF-8), echappe pour JSON : le portail
 * l'affiche tel quel, au lieu du slug des topics. name NULL ou vide : pas de champ name. Rend la
 * longueur, ou -1 si le type est inconnu ou si le tampon est trop petit (rien n'est alors ecrit). */
int cover_attrs_json(uint8_t type, bool positional, const char *name, char *out, size_t cap);
