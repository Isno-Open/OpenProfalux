/*
 * Profalux protocol specific - frame build/parse + TX/RX
 */
#ifndef PROFALUX_H
#define PROFALUX_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware_config.h"

/* Profalux transmitter state (=persisted in NVS) */
typedef struct {
    uint32_t serial;         /* 28-bit */
    uint64_t crypt_key;      /* 64-bit */
    uint16_t counter;        /* Rolling code counter */
    uint16_t discrimination; /* 12-bit discrim (=8 LSB serial typical) */
} pfx_tx_state_t;

/* Parsed RX frame from external transmitter */
typedef struct {
    uint32_t serial;         /* Serial extracted from frame */
    uint32_t encrypted_hop;  /* Raw encrypted 32-bit hop */
    uint8_t  button;         /* Button code */
    uint8_t  status_flags;   /* 2 status bits */
    uint16_t counter;        /* Decrypted counter (=only if we have the crypt_key) */
    bool     decoded;        /* true if crypt_key was known and decoded */
} pfx_rx_frame_t;

/* Init state: load NVS or generate new random */
int  pfx_state_init(pfx_tx_state_t *st);
int  pfx_state_save(const pfx_tx_state_t *st);
int  pfx_state_reset(pfx_tx_state_t *st);  /* Force new random (=re-pair) */

/* Build 66-bit frame in 9-byte buffer */
void pfx_frame_build(const pfx_tx_state_t *st, uint8_t button, uint8_t frame[9]);

/* TEST 2 : build sans chiffrement, hop KeeLoq injecte (rejeu d'un hop reel via notre
 * pipeline de sortie = preuve d'emission correcte). hop_true = VRAI hop KeeLoq
 * (= bit-reverse de la valeur MSB-first lue sur l'air). Convention validee 7/7. */
void pfx_frame_build_with_hop(uint32_t hop_true, uint32_t serial, uint8_t button, uint8_t frame[9]);

/* Parse received 66-bit frame */
int  pfx_frame_parse(const uint8_t frame[9], pfx_rx_frame_t *out);

/* Try to decrypt hop using known crypt_key */
int  pfx_frame_decrypt(pfx_rx_frame_t *frm, uint64_t crypt_key);

/* Emit N frames spaced over duration_ms (=for pairing burst) */
void pfx_emit_burst(pfx_tx_state_t *st, uint8_t button, uint32_t n_frames, uint32_t duration_ms);

/* Tient un bouton "appuye" (meme compteur/hop) pendant duration_ms, comme un
 * vrai appui maintenu. Incremente et sauvegarde le compteur UNE fois a la fin.
 * Utilise pour l'etape 2 de la notice (STOP-en-P maintenu 5 s = bouton 0x8). */
void pfx_emit_hold(pfx_tx_state_t *st, uint8_t button, uint32_t duration_ms);

/* SCAN : emet l'enrolement DEVMEL (bouton 0x5, counter 3) pour les 63 identites
 * en boucle pendant duration_ms. Si UNE identite est valide pour ce moteur, elle
 * sera enrolee. Sert a tester tout le pool d'un coup. */
void pfx_emit_enroll_all(uint32_t duration_ms);

/* SCAN : emet une commande (button) pour les 63 identites DEVMEL, counter qui
 * avance a chaque appel. Le moteur repond a celle qu'il a apprise. */
void pfx_emit_command_all(uint8_t button);

/* Auto-verif : emet une trame, la RE-CAPTURE sur GDO0 (RMT) et la decode, pour
 * confirmer que ce qu'on emet est byte-parfait (serial, famille, bouton, RPT). */
void pfx_selfverify(pfx_tx_state_t *st, uint8_t button);

/* Demo : spam n trames vers le moteur (le jamme = prouve qu'il recoit). */
void pfx_spam(pfx_tx_state_t *st, uint8_t button, int n);

/* Pas-a-pas : enrole UNE identite (slot 1..63) = une pression propre 0x8 + ecoute
 * RX 3s. Piloter cette identite avec un bouton (montee/stop/descente). */
void pfx_emit_enroll_slot(int slot);
void pfx_emit_command_slot(int slot, uint8_t button);

/* Emit single command (=3 repeats HCS301-style) */
void pfx_emit_command(pfx_tx_state_t *st, uint8_t button);

#endif
