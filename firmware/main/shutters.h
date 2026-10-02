/*
 * shutters.h — modele "cover" OpenProfalux.
 * Clone/replay : chaque volet stocke 3 trames captees (up/down/stop) rejouees telles quelles.
 * Position estimee par le temps (le moteur ne renvoie rien). Multi-serial par volet.
 */
#ifndef SHUTTERS_H
#define SHUTTERS_H

#include <stdbool.h>
#include <stdint.h>

#define SH_MAX_VOLETS   24
#define SH_MEMBERS_LEN  384   /* CSV d'ids de volets membres d'une centrale */
#define SH_MAX_SERIALS  4
#define SH_BITS_LEN     72   /* 66 bits + marge + NUL */
#define SH_ID_LEN       24
#define SH_SERIAL_LEN   12

/* Charge la config NVS + demarre la tache de suivi position. */
void shutters_init(void);

/* Commande : cmd = "up" | "down" | "stop" | "pos" (value = 0..100). Rejoue la trame + suit la position. */
int  shutters_cmd(const char *id, const char *cmd, int value);
int  shutters_delete_volet(const char *id);

/* Apprentissage : affecte une trame captee (bitstring) a une action d'un volet. Cree le volet si absent. */
int  shutters_learn_assign(const char *id, const char *action, const char *bits);
/* Cree un volet "virtuel PFX" a partir d'une identite 0x067 enrolee (pilote par generation,
 * pas rejeu) : apparait dans l'onglet Volets + cover HA. Retourne 0 si OK. */
int  shutters_create_virtual(const char *id, uint32_t serial, uint16_t counter, uint16_t te);
/* Cree une "centrale" virtuelle : une commande (ouvrir/fermer/stop) diffusee a un GROUPE de
 * volets. members_csv = ids de volets separes par des virgules. Apparait en cover HA. */
int  shutters_create_central(const char *id, const char *members_csv);
int  shutters_reassign(const char *id, const char *from, const char *to);

/* Calibration : temps de course. */
int  shutters_calibrate(const char *id, uint32_t up_ms, uint32_t down_ms);
int  shutters_set_orientation(const char *id, int orientation);   /* azimut facade (-1 = non defini) */
/* Ordre d'affichage dans l'UI : CSV d'ids dans l'ordre voulu. N'affecte QUE
 * l'affichage web (ni radio, ni pilotage, ni decouverte HA). */
int  shutters_set_order(const char *ids_csv);

/* Nommage d'une telecommande (serial -> nom). */
int  shutters_remote_name(const char *serial, const char *name);

/* Appele par le sniff RX : met a jour la position si une commande externe bouge un volet. */
void shutters_on_rx(const char *bits, uint32_t serial, uint8_t button, int8_t rssi, uint32_t hop);
/* Adopte une trame vue dans le journal RF (identifiee par serial+hop) et l'affecte a une action. */
int  shutters_adopt(const char *id, const char *action, const char *serial, uint32_t hop);
/* Rejoue telle quelle une trame captee (serial+hop) depuis le journal RF (bouton "rejouer" du debug). */
int  shutters_replay_frame(const char *serial, uint32_t hop);
/* Ring RF (300 trames recentes) pour /api/rf : k=0 = plus recente. Renvoie 0 si presente, -1 sinon. */
int  shutters_rf_get(int k, char *serial, int sser, uint8_t *button, uint32_t *hop, uint32_t *t, int8_t *rssi);
int  shutters_rf_capacity(void);

/* JSON /api/status, alloue a sa taille : a liberer par l'appelant (NULL si plus de
 * memoire). Plus de tampon fixe : celui de 4 Ko tronquait l'etat au-dela de ~9 volets,
 * et l'UI recevait un JSON invalide. */
char *shutters_status_json(void);

/* ── Integration Home Assistant (MQTT) ── */
/* Publie la discovery HA (1 cover par volet) + l'etat courant. A appeler apres connexion MQTT. */
void shutters_mqtt_announce(const char *device);
void shutters_mqtt_lost(void);   /* broker perdu -> statut hors ligne */
/* Export dataset slide : par telecommande, ses trames distinctes {hop, bouton, t} (tout brut). */
typedef struct { uint32_t hop; uint32_t t; uint8_t button; } dframe_t;
int  shutters_remote_count(void);
int  shutters_remote_dump(int i, char *serial, int sser, char *name, int sname, dframe_t *frames, int maxframes);
/* Route une commande HA cover recue en MQTT : openprofalux/cover/<id>/set|set_position. */
void shutters_mqtt_on_message(const char *topic, const char *data, int len);
/* Active/desactive la publication MQTT de TOUTES les trames captees (dedup par serial). */
void shutters_set_log_frames(bool on);

/* ── Sauvegarde / restauration ── */
/* Exporte toute la config (telecommandes + noms + trames de reference + calibration) en
 * JSON, alloue a sa taille : a liberer par l'appelant (NULL si plus de memoire). */
char *shutters_export_json(void);
/* Restaure la config depuis un JSON (remplace tout). Retourne 0 si OK. */
int  shutters_import_json(const char *js);

/* ── Identités virtuelles PFX (anti-conflit + resync compteur pour l'enrôlement) ── */
bool     shutters_virt_serial_used(uint32_t serial);        /* serial 0x067 déjà pris par un volet ? */
uint16_t shutters_virt_counter_for_serial(uint32_t serial); /* compteur du volet de ce serial (0 si aucun) */

#endif
