/*
 * radio_profile.c — profils de frequence (voir radio_profile.h). Pur, teste sur PC.
 */
#include "radio_profile.h"
#include <string.h>

static const struct { const char *name; uint32_t khz; } s_profiles[RADIO_PROFILE_COUNT] = {
    [RADIO_PROFILE_PROFALUX] = { "profalux", 868425 },   /* mesure 2026-08-10 */
    [RADIO_PROFILE_EVENO]    = { "eveno",    868330 },   /* pic au scan 2026-09-30, plateau 868,30-868,36 */
    [RADIO_PROFILE_CUSTOM]   = { "custom",   0 },
};

const char *radio_profile_name(radio_profile_t p) {
    return (unsigned)p < RADIO_PROFILE_COUNT ? s_profiles[p].name : "?";
}

int radio_profile_from_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < RADIO_PROFILE_COUNT; i++)
        if (!strcmp(name, s_profiles[i].name)) return i;
    return -1;
}

uint32_t radio_profile_freq_khz(radio_profile_t p, uint32_t custom_khz) {
    if (p == RADIO_PROFILE_CUSTOM) return custom_khz;
    return (unsigned)p < RADIO_PROFILE_COUNT ? s_profiles[p].khz : s_profiles[RADIO_PROFILE_DEFAULT].khz;
}

bool radio_freq_in_band(uint32_t khz, uint32_t lo_khz, uint32_t hi_khz) {
    return khz >= lo_khz && khz <= hi_khz;
}

bool radio_profile_resolve(int profile, uint32_t custom_khz, uint32_t lo_khz, uint32_t hi_khz,
                           uint32_t *khz) {
    if (profile < 0 || profile >= RADIO_PROFILE_COUNT) return false;
    uint32_t f = radio_profile_freq_khz((radio_profile_t)profile, custom_khz);
    if (!radio_freq_in_band(f, lo_khz, hi_khz)) return false;
    if (khz) *khz = f;
    return true;
}

uint32_t radio_cc1101_freq_word(uint32_t khz) {
    /* f_hz * 2^16 / 26e6 = khz * 65536 / 26000, en 64 bits, arrondi au plus proche. */
    return (uint32_t)(((uint64_t)khz * 65536u + 13000u) / 26000u);
}
