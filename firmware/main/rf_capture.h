#pragma once
#include <stdint.h>

/* Moteur de capture BRUTE OOK + rejeu (issue #95). Enregistre les impulsions brutes d'une
 * telecommande OOK a CODE FIXE (portail CAME, capteurs...) sur la radio choisie, et les rejoue a
 * l'identique. Protocole-agnostique : aucun decodeur, aucun "filtre de ce qu'on connait". Les
 * codes TOURNANTS/chiffres (Somfy RTS, KeeLoq Profalux, io) ne se rejouent PAS ainsi et restent
 * l'affaire des decodeurs dedies. Remplit le journal commun (capture_journal) quand il est present
 * sur la branche (auto-cable). API appelee par les routes /api/capture/... (web_ui.c). */

#define RFCAP_MAX_PULSES 512

/* Lance une capture sur la radio idx, bloquante ~timeout_ms (l'utilisateur presse sa telecommande
 * pendant ce temps). Rend le nb d'impulsions captees (>0), 0 si rien, <0 erreur. Stocke la
 * derniere capture (pour rejeu + export UI). */
int  rfcap_capture(int radio, uint32_t timeout_ms);

/* Rejoue la derniere capture `repeats` fois (TX OOK sur la radio et la porteuse captees).
 * Rend 0 si ok, <0 si pas de capture / erreur. */
int  rfcap_replay(int repeats);

/* Derniere capture pour l'UI : ecrit un objet JSON dans out, rend sa longueur (>0), un objet
 * {"n":0} si pas de capture. Forme :
 *   {"n":..,"radio":..,"band":"433","freq_khz":..,"rssi":..,"age_ms":..,"pulses":[[lvl,us],...]} */
int  rfcap_last_json(char *out, int max);

/* Combien de trames distinctes au plus une capture MULTI collecte. */
#define RFCAP_MAX_FRAMES 6

/* Capture MULTI-trames : pendant window_ms, collecte CHAQUE pression distincte sur la radio idx.
 * Dedoublonne deux trames identiques qui se suivent (un code FIXE presse plusieurs fois ne compte
 * qu'une fois) ; garde les trames differentes (les hoppings d'un code TOURNANT). Utile pour un
 * rapport de debug : plusieurs echantillons aident le decodage serveur. Stocke le lot pour le
 * rapport ; la DERNIERE trame reste aussi la "derniere capture" (rejeu + rfcap_last_json).
 * Rend le nombre de trames collectees (>=0), <0 en cas d'erreur. */
int  rfcap_capture_multi(int radio, uint32_t window_ms);

/* Ecrit le lot de trames collectees par rfcap_capture_multi en JSON dans out, rend sa longueur.
 * Forme (le serveur sait relire l'ancienne a UNE trame via `pulses`/`n`, et la nouvelle via `frames`) :
 *   {"radio":..,"band":"868","freq_khz":..,"nframes":K,"frames":[{"rssi":..,"pulses":[[lvl,us],..]},..],
 *    "pulses":[derniere trame],"n":..}
 * {"n":0} si aucune trame. */
int  rfcap_frames_json(char *out, int max);
