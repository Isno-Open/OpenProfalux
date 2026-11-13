#pragma once
/* Reecriture des topics pour la connexion CLOUD : prefixe par device_id = isolation ACL du broker
 * (chaque boitier confine a openprofalux/{device_id}/#). C'est la propriete de securite centrale du
 * pont cloud, donc elle est isolee en unite PURE (aucune dependance ESP-IDF) et testee sur PC :
 * cf firmware/test/cloud-topic. */
#include <stdbool.h>
#include <stddef.h>

#define OPF_ROOT "openprofalux/"

/* Sous-arbres de "openprofalux/" qui ne partent JAMAIS au cloud :
 *  - "frames/" : trames radio captees (numero de serie, code tournant, RSSI de TOUTE telecommande
 *    entendue, voisins compris). Donnee personnelle d'un tiers : elle reste dans la maison.
 *  - "counter/", "busy/" : synchronisation entre cartes du meme reseau local (compteur, verrou
 *    d'emission) ; sans objet dans le cloud.
 *  - "log" : journal de debogage du boitier. */
extern const char *const CLOUD_LOCAL_ONLY[];

/* Local -> cloud. "openprofalux/<reste>" devient "<base>/<reste>" ou base = "openprofalux/<device_id>".
 * Un sous-arbre de CLOUD_LOCAL_ONLY renvoie false (reste local).
 * Un topic HORS "openprofalux/" (ex. "homeassistant/..." de la Discovery HA) renvoie false : il ne
 * part PAS au cloud (l'ACL le refuserait, le cloud rend sa propre IHM). Un topic DEJA prefixe par le
 * device_id est laisse tel quel. false aussi si le resultat ne tient pas dans cap. */
bool cloud_topic_from_local(const char *local, const char *device_id, const char *base,
                            char *out, size_t cap);

/* Cloud recu -> forme locale, pour reutiliser le meme chemin de traitement local.
 * "<base>/cover/X/set" -> "openprofalux/cover/X/set". false si hors de la base du device. */
bool local_topic_from_cloud(const char *cloud_topic, int len, const char *base,
                            char *out, size_t cap);
