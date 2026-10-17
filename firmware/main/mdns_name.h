#pragma once
/* mdns_name — nom d'hote mDNS du boitier (<nom>.local).
 *
 * Deux boitiers sur un meme reseau ne doivent pas se disputer le meme nom. Regle :
 *   - nom de boitier choisi par l'utilisateur (cfg/device) -> ce nom, nettoye ;
 *   - sinon, le nom FIGE au premier demarrage avec ce firmware (cle NVS) : openprofalux pour un
 *     boitier deja installe (adresse inchangee), openprofalux-xxxx pour un boitier neuf (xxxx =
 *     2 derniers octets de la MAC du point d'acces, comme son SSID OpenProfalux_XXXX).
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/mdns-name. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MDNS_NAME_MAX 32   /* longueur max du nom, sans le NUL */

/* Nettoie un nom en nom d'hote : minuscules, a-z et 0-9 gardes, tout autre caractere devient un
 * tiret, pas de tiret en tete, en queue ni double, tronque a MDNS_NAME_MAX. cap >= 2.
 * Rend la longueur (0 si aucun caractere valide). */
size_t mdns_name_sanitize(const char *in, char *out, size_t cap);

/* Nom par defaut d'un boitier neuf : "openprofalux-xxxx" (mac[4], mac[5] en hexa minuscule). */
void mdns_name_default(const uint8_t mac[6], char *out, size_t cap);

/* true si le nom de boitier est un nom par defaut (vide, "openprofalux", "volet_test", sans tenir
 * compte des majuscules) : il ne vaut pas choix de l'utilisateur. */
bool mdns_name_is_default_device(const char *device);

/* Nom a figer au premier demarrage : "openprofalux" si le boitier etait deja installe (un Wi-Fi
 * enregistre), sinon le nom par defaut a suffixe. */
void mdns_name_first_boot(bool already_installed, const uint8_t mac[6], char *out, size_t cap);
