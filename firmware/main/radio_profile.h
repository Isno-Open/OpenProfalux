#pragma once
/* Profil radio : la frequence porteuse selon la marque des volets.
 *
 * Profalux emet a 868,425 MHz ; Eveno (Zuni-R) a 868,33 MHz, mesure au scan sur une
 * installation (issue #3). Un profil « custom » prend une frequence saisie en kHz, bornee
 * par la plage autorisee de la carte (boards/<carte>.json). Le profil est un reglage du
 * boitier (NVS, /api/config), pas une constante de compilation : un meme binaire sert
 * toutes les marques, et la mise a jour depuis GitHub (un binaire par carte) ne fait pas
 * changer de marque.
 *
 * Module PUR (ni ESP-IDF, ni FreeRTOS) : teste sur PC, voir firmware/test/radio-profile.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RADIO_PROFILE_PROFALUX = 0,
    RADIO_PROFILE_EVENO    = 1,
    RADIO_PROFILE_CUSTOM   = 2,
    RADIO_PROFILE_COUNT
} radio_profile_t;

#define RADIO_PROFILE_DEFAULT RADIO_PROFILE_PROFALUX

/* Nom stable, utilise par l'API et l'UI : "profalux", "eveno", "custom". */
const char *radio_profile_name(radio_profile_t p);

/* Profil depuis son nom ; -1 si inconnu. */
int radio_profile_from_name(const char *name);

/* Frequence du profil en kHz. Pour CUSTOM, rend custom_khz tel quel. */
uint32_t radio_profile_freq_khz(radio_profile_t p, uint32_t custom_khz);

/* true si lo_khz <= khz <= hi_khz (plage autorisee de la carte, bornes incluses). */
bool radio_freq_in_band(uint32_t khz, uint32_t lo_khz, uint32_t hi_khz);

/* Frequence a appliquer pour (profil, saisie), controlee contre la plage de la carte.
 * Rend false (et *khz inchange) si le profil est inconnu ou si la frequence sort de
 * [lo_khz, hi_khz] : l'appelant garde alors la frequence en place. */
bool radio_profile_resolve(int profile, uint32_t custom_khz, uint32_t lo_khz, uint32_t hi_khz,
                           uint32_t *khz);

/* Mot de frequence du CC1101 (FREQ2:FREQ1:FREQ0, 24 bits) pour un quartz de 26 MHz :
 * f * 2^16 / 26 MHz, arrondi au plus proche. 868330 kHz -> 0x2165B6. */
uint32_t radio_cc1101_freq_word(uint32_t khz);
