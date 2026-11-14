#pragma once
/* Sondes memoire pour la build de diagnostic test/heapdiag/sdkconfig.heaptrace.
 *
 * Sans CONFIG_HEAP_TRACING_STANDALONE, tout est vide : aucune ligne, aucun cout. Avec :
 *  - memprobe("etape") ecrit sur la console « MEMPROBE etape libre=… bloc=… min=… » ;
 *  - memtrace_window("raison", ms) enregistre chaque allocation pendant ms millisecondes
 *    (taille, adresse, appelants), ou seulement celles encore tenues a la fin (leaks), puis memtrace_poll(), appele par la boucle de fond, les
 *    ecrit sur la console. Les adresses se traduisent avec addr2line sur openprofalux.elf.
 * Pourquoi : un releve toutes les 20 s rate les creux de quelques secondes (mesure du
 * 2026-11-13 : ~18 Ko perdus le temps d'une commande de volet), et le suivi par tache retire
 * ~25 Ko au tas, ce qui fausse justement ce qu'on mesure. */
#include <stdbool.h>
#include <stdint.h>

void memprobe(const char *label);
/* leaks = true : ne garde que ce qui est ENCORE alloue a la fin de la fenetre (ce qui reste
 * tenu) ; false : toutes les allocations (ce qui passe, creux compris). */
void memtrace_window(const char *why, uint32_t ms, bool leaks);
void memtrace_poll(void);
/* Boucle de fond : une sonde a chaque baisse du minimum de plus de 1 Ko, et une fenetre de
 * tracage d'une seconde quand le libre passe sous MEMWATCH_LOW (le creux en cours). */
void memprobe_watch(void);
/* Boucle de fond, build de diagnostic : marge minimale de pile de chaque tache (« PILE nom
 * marge_min=… »), toutes les 2 min. Vide sans CONFIG_FREERTOS_USE_TRACE_FACILITY. */
void memprobe_stacks(void);
