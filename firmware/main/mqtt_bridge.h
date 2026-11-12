/*
 * MQTT API - OpenProfalux
 *
 * Topics (le pilotage passe par le modele cover dans shutters.c) :
 *   openprofalux/cover/{slug}/set          [sub]  → "OPEN"|"CLOSE"|"STOP"
 *   openprofalux/cover/{slug}/set_position [sub]  → 0..100 (% d'ouverture)
 *   openprofalux/cover/{slug}/state|position [pub, retained]
 *   openprofalux/listen/set|state          [sub/pub] → switch "Ecoute RF permanente"
 *   openprofalux/frames/last|log/{slot}    [pub, retained] → trames captees
 *   openprofalux/ota/pull                  [sub]  → URL .bin (HTTPS)
 *   openprofalux/{device}/state            [pub]  → heartbeat {"last_cmd","rssi","free_heap"}
 *   openprofalux/{device}/status           [pub, retained] → LWT "online"/"offline"
 *   openprofalux/system/status             [pub, retained] → {"fw","target","free_heap"}
 *
 * HA MQTT Discovery : cover entities + switch/capteur publies par shutters.c
 */
#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

int  mqtt_bridge_start(const char *broker_uri, const char *client_id, const char *user, const char *pass);
void mqtt_bridge_stop(void);

/* ── Connexion CLOUD additive (broker ISNO en mqtts mTLS), EN PLUS du local, qui reste INTACT. ──
 * Un 2e client MQTT. Les topics cloud sont prefixes par le device_id (openprofalux/{device_id}/...)
 * pour coller a l'ACL du broker. La publication des volets (mqtt_pub_*) part automatiquement sur les
 * DEUX connexions, avec le prefixe pose cote cloud ; les commandes cloud recues sont ramenees a la
 * forme locale et traitees par le meme chemin. cloud_uri vide => cloud non demarre (local seul).
 * Certificats cloud via mqtt_cert_write avec which = "cloud_ca" | "cloud_cert" | "cloud_key". */
int  mqtt_cloud_start(const char *cloud_uri, const char *device_id);
void mqtt_cloud_stop(void);
bool mqtt_cloud_is_connected(void);

/* Certificats TLS pour mqtts, stockes en SPIFFS (partition storage, 960 Ko) et non en
 * NVS : la NVS de 16 Ko est deja trop juste pour la config des volets, exactement comme
 * pour le ring de trames. which = "ca" (autorite du broker) | "cert" (client) | "key"
 * (cle privee client). mqtt_bridge_start lit ces fichiers tout seul quand l'URI est en
 * mqtts://. Ecrit le PEM tel quel ; longueur 0 efface le certificat. */
int    mqtt_cert_write(const char *which, const char *pem);
size_t mqtt_cert_len(const char *which);

/* Publish */
int  mqtt_pub_state(const char *device_name, const char *json);
int  mqtt_pub_system_status(void);
/* Publish générique (topic libre). qos/retain passés tels quels. */
int  mqtt_pub_raw(const char *topic, const char *payload, int qos, int retain);

/* Callback registered by main - dispatched based on topic */
typedef struct {
    void (*on_message)(const char *topic, const char *data, int len); /* cover HA : openprofalux/cover/# */
    void (*on_ota_pull)(const char *url);                             /* openprofalux/ota/pull */
    void (*on_connected)(void);                                       /* broker connecte : publier la decouverte */
    void (*on_disconnected)(void);                                    /* broker perdu : statut hors ligne */
} mqtt_handlers_t;

int  mqtt_bridge_set_handlers(const mqtt_handlers_t *h);

#endif
