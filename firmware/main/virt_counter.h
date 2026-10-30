#pragma once
/* virt_counter : le compteur roulant d'une telecommande virtuelle ne recule pas a la restauration.
 *
 * Une telecommande virtuelle (KeeLoq pour Profalux, code roulant pour Somfy RTS) avance son
 * compteur a chaque ordre, et le moteur retient le dernier recu. Une sauvegarde en est une photo :
 * restauree telle quelle, elle faisait emettre des compteurs deja vus, que le moteur ignore (la
 * telecommande paraissait morte). On garde donc le plus avance du compteur restaure et de celui
 * en place, comme la table d'enrolement le fait deja (pfx_enrol_import). Comparaison modulo 2^16 :
 * le compteur repart a 0 apres 65535.
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/virt-counter. */
#include <stdbool.h>
#include <stdint.h>

/* a est en avance sur b (de 1 a 32767 pas, modulo 2^16). */
bool vc_ahead(uint16_t a, uint16_t b);

/* Le compteur a garder : current s'il est en avance sur restored, sinon restored. */
uint16_t vc_keep(uint16_t restored, uint16_t current);
