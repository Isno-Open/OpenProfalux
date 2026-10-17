#pragma once
/* captive_dns — DNS du point d'acces de configuration : toute requete A recoit l'adresse du boitier
 * (192.168.4.1). Le telephone qui teste sa connexion (generate_204, hotspot-detect.html...) arrive
 * donc sur le boitier, qui le redirige vers la page de configuration : le portail s'ouvre seul.
 * API brute lwIP (udp_pcb) : ne consomme aucun socket (la pile n'en a que 10). */
void captive_dns_start(void);   /* idempotent */
