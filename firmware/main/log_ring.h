#pragma once
/* Journal du firmware consultable depuis l'interface web (Systeme > Journal).
 *
 * Les memes messages que le port serie (ESP_LOGx), gardes dans un anneau de
 * LOG_RING_SIZE octets : les plus anciens s'effacent. Sans cable USB, c'est la
 * seule facon de voir pourquoi une mise a jour, une connexion MQTT ou une
 * capture radio a echoue. Les messages d'avant log_ring_init() (chargeur de
 * demarrage, tout debut de l'application) restent seulement sur le port serie.
 */
#include <stddef.h>

#define LOG_RING_SIZE 8192

/* Branche le journal sur ESP_LOGx (le port serie continue de tout recevoir). */
void   log_ring_init(void);
/* Copie le journal dans out, du plus ancien au plus recent, en commencant a un
 * debut de ligne. Renvoie le nombre d'octets copies (0 si vide). */
size_t log_ring_copy(char *out, size_t cap);
