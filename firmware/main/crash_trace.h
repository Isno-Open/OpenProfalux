/* Trace du dernier plantage, gardee a travers le redemarrage (memoire RTC non initialisee).
 *
 * Pourquoi : un boitier installe n'a pas de liaison serie, et la trace d'une panique ne sortait que
 * la. Constate le 2026-11-15 : couper le cloud faisait planter l'ATOM, et rien ne disait ou. Au moment
 * de la panique, on releve l'adresse fautive et la pile d'appels ; au demarrage suivant, le boitier les
 * rend (journal, /api/health). Les adresses se traduisent en fonctions avec le .elf de la version :
 *   xtensa-esp32-elf-addr2line -pfC -e openprofalux.elf <adresses>
 *
 * Seulement sur Xtensa (ESP32, ESP32-S3) : le parcours de pile du RISC-V (ESP32-C3) est autre ;
 * la, crash_trace_last rend toujours false. */
#ifndef CRASH_TRACE_H
#define CRASH_TRACE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRASH_TRACE_DEPTH 10   /* appels gardes : assez pour remonter d'un pilote a la tache */
/* Place du texte JSON : en-tete (~40) + 10 adresses de 11 caracteres, avec marge. */
#define CRASH_JSON_MAX 200

typedef struct {
    uint32_t pc;                          /* instruction fautive */
    uint32_t exccause;                    /* cause Xtensa (EXCCAUSE), 0 pour abort() ou chien de garde */
    uint32_t depth;                       /* nombre d'adresses dans bt */
    uint32_t bt[CRASH_TRACE_DEPTH];       /* pile d'appels, de l'instruction fautive vers la tache */
} crash_trace_t;

/* Le dernier plantage garde, s'il y en a un et qu'il est intact. */
bool crash_trace_last(crash_trace_t *out);

/* {"pc":"0x...","cause":N,"bt":"0x... 0x..."} ou null : pour /api/health et le journal. */
void crash_trace_json(char *buf, size_t len);

#endif
