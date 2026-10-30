#pragma once
/* Journal RX+TX d'une instance d'enrolement io, en RAM, pour diagnostic. On enregistre chaque
 * trame RECUE (dir 'R', avec RSSI) pendant la capture et chaque trame EMISE (dir 'T') pendant
 * l'enrolement, dans le MEME journal, pour voir la sequence complete : qui emet quoi, dans quel
 * ordre, a quel instant. Le testeur le recupere ensuite via /api/io/trace (bouton dans l'UI). */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Remet le journal a zero et repart le chrono (appele au debut d'une capture). Efface aussi la NVS. */
void io_trace_reset(void);

/* Ecrit le journal en NVS (survit au reboot). A appeler a la FIN d'une capture ou d'un enrolement,
 * jamais par trame (un commit NVS ferait rater des trames RX). */
void io_trace_persist(void);

/* Ajoute une trame au journal. dir = 'R' (recue) ou 'T' (emise) ; rssi en dBm (ignore si 'T'). */
void io_trace_add(char dir, int8_t rssi, const uint8_t *bytes, int n);

/* Insere un marqueur de section (nom d'etape) : le journal recupere est ainsi sectionne par bouton
 * (compteur / cle / PROG / Creer), pour savoir de quelle action vient chaque trame. */
void io_trace_mark(const char *label);

/* Ecrit le journal en texte (une ligne par trame : t+ms, sens, RSSI, cmd, hexa). Rend le nombre
 * d'octets ecrits (hors \0). */
int io_trace_dump(char *out, size_t cap);

/* Nombre de trames au journal. */
int io_trace_count(void);

#ifdef __cplusplus
}
#endif
