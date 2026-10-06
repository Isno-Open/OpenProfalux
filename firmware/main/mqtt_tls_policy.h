#pragma once
#include <stdbool.h>

/* Politique TLS pour mqtts, extraite de mqtt_bridge.c pour etre testable en hote
 * (sans esp-mqtt). Voir firmware/test/mqtt-tls/. */

/* L'hote de l'URI est-il une IPv4 litterale (ex. mqtts://192.168.1.10:8883) ? */
bool mqtt_host_is_ipv4(const char *uri);

/* Faut-il lever le controle du nom (CN) du certificat du broker ?
 * Oui seulement si le broker est joint par IP ET qu'un CA prive est fourni : l'utilisateur
 * maitrise alors l'emission, le risque se limite a ses propres certificats. Avec le bundle
 * public (has_ca = false) on garde le controle, sinon n'importe quel certificat public
 * usurperait un broker joint par IP. */
bool mqtt_tls_skip_cn(const char *uri, bool has_ca);
