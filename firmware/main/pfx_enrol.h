/*
 * pfx_enrol.h — apprentissage "sans télécommande" (enrôlement d'une identité virtuelle 0x067).
 *
 * EXPÉRIMENTAL. Contrairement au clonage (capture + rejeu d'une vraie télécommande), ce module
 * GÉNÈRE une identité virtuelle PFX (famille 0x067, une des 63 clés du pool) et émet la trame
 * d'apprentissage. L'utilisateur choisit son modèle : le modèle fixe le TE d'émission et la
 * chorégraphie de gestes.
 *
 * Le moteur valide en normal-learn : il dérive la clé du serial émis. L'enrôlement nécessite
 * la télécommande d'origine pour la gestuelle de validation (double accostage en butée).
 */
#pragma once
#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>

/* Catalogue de modèles. L'index = l'id exposé à l'UI. */
typedef struct {
    const char *name;      /* libellé affiché */
    uint16_t    te_us;     /* TE d'émission (Profalux 455, FranciaFlex 415) */
    const char *routine;   /* "R6" | "R7" | "R8" — chorégraphie d'apprentissage */
} pfx_model_t;

int                 pfx_enrol_model_count(void);
const pfx_model_t  *pfx_enrol_model(int id);   /* NULL si hors borne */
uint16_t            pfx_enrol_model_te(int model);   /* TE d'émission du modèle (défaut 455) */

/* Boutons PFX 0x067 — directions corrigées d'après test réel FranciaFlex M4G (2026-08-28).
 * Partagés avec shutters pour piloter un volet virtuel. */
#define PFX_BTN_UP     0x2
#define PFX_BTN_STOP   0x4
#define PFX_BTN_DOWN   0x8

/* Construit la trame 66 bits on-air ('0'/'1', out[66]=NUL) pour une identité 0x067.
 * Retourne 0 si serial valide (famille 0x067), -1 sinon. Utilisé aussi par shutters
 * pour générer les trames d'un volet virtuel (compteur roulant géré par l'appelant). */
int pfx_enrol_frame(uint32_t serial, uint8_t button, uint16_t counter, char out[68]);

/* Identité virtuelle courante. */
typedef struct {
    bool     active;
    int      model;
    uint8_t  idx;        /* slot 0..62 */
    uint32_t serial;     /* 28-bit */
    uint16_t counter;    /* compteur roulant courant */
    uint64_t key;        /* clé KeeLoq du slot (table obfusquée) */
    uint32_t remote;     /* serial de la vraie télécommande associée (0 = aucune) */
} pfx_ident_t;

void pfx_enrol_init(void);              /* charge l'identité depuis NVS (idempotent) */
bool pfx_enrol_get(pfx_ident_t *out);   /* copie l'identité courante ; retourne active */

/* Capture : associe la vraie télécommande `remote` (serial capté en RX) à une identité 0x067,
 * et la charge comme identité courante.
 *   remote CONNU   -> recharge la même identité déjà attribuée (*is_new=false) ;
 *   remote INCONNU -> alloue le PROCHAIN slot libre (jamais un déjà pris) et le mémorise
 *                     DÉFINITIVEMENT (*is_new=true).
 * Retourne 0 (ok), -1 (plein : 63 identités déjà prises), -2 (remote invalide). */
int  pfx_enrol_capture(uint32_t remote, int model, bool *is_new);

/* Oublie l'identité courante : libère son slot dans la table (réattribuable) et efface l'état. */
void pfx_enrol_forget(void);

/* Reporte dans la table le compteur d'une identité dont le volet virtuel va disparaître
 * (next = prochain compteur à émettre). Sans ça, le compteur avancé par les commandes du volet
 * était perdu, et un ré-enrôlement repartait d'une valeur en retard (issue #45). */
void pfx_enrol_note_counter(uint32_t serial, uint16_t next);

/* Sauvegarde / restauration de la table d'enrôlement (tableau JSON {remote, slot, counter}).
 * La restauration fusionne : compteur le plus avancé, entrées restaurées non confirmées. */
cJSON *pfx_enrol_export(void);
void   pfx_enrol_import(const cJSON *arr);
/* Identité d'un volet virtuel restauré : redevient non confirmée (compteur-photo). */
void   pfx_enrol_unconfirm(uint32_t serial);

/* Confirme l'enrôlement de l'identité courante (le moteur a répondu : volet virtuel créé) : son
 * compteur devient sûr, un futur ré-enrôlement ne sautera plus. */
void pfx_enrol_confirm(void);

/* Émet la trame d'apprentissage (bouton LEARN, compteur courant de l'identité) en rafale. 0 si OK. */
int  pfx_enrol_emit_learn(void);
/* Émet une commande après enrôlement : "up" | "stop" | "down" (compteur roulant). 0 si OK. */
int  pfx_enrol_cmd(const char *cmd);
