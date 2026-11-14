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
#include <stdint.h>

#define LOG_RING_SIZE 8192

/* Branche le journal sur ESP_LOGx (le port serie continue de tout recevoir). */
void   log_ring_init(void);
/* Lecture par morceaux, sans copie du journal entier (8 Ko d'un bloc a chaque lecture de la
 * page Journal morcelaient le tas : mesure du 2026-11-13, voir web_ui.c h_log).
 * log_ring_span rend la fin du journal (position absolue) et met dans *begin le debut d'une
 * ligne entiere ; log_ring_read copie au plus cap octets a partir de *pos, sans depasser end,
 * et avance *pos. Ce qui a ete ecrase entre deux morceaux est saute. 0 = fini (ou occupe). */
uint32_t log_ring_span(uint32_t *begin);
size_t   log_ring_read(uint32_t *pos, uint32_t end, char *out, size_t cap);
