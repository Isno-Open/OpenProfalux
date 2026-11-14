/* Description d'un appareil pour le portail cloud. Voir cover_attrs.h. */
#include "cover_attrs.h"
#include "dev_type.h"
#include <stdbool.h>
#include <stdio.h>

/* Noms des types : MIROIR des cles de profiles.json "types" (source de verite du vocabulaire),
 * comme les codes de dev_type_t le sont de ses "code". Le banc test/cover-attrs refuse toute
 * divergence entre les deux. */
static const char *const DEV_TYPE_NAMES[] = {
    [TYPE_VOLET]   = "volet",
    [TYPE_PORTAIL] = "portail",
};

const char *dev_type_name(uint8_t type) {
    return type < sizeof(DEV_TYPE_NAMES) / sizeof(DEV_TYPE_NAMES[0]) ? DEV_TYPE_NAMES[type] : NULL;
}

/* Ajoute c a out (position *w, capacite cap) ; false si ca ne tient pas. */
static bool put(char *out, size_t cap, size_t *w, const char *c, size_t n) {
    if (*w + n >= cap) return false;
    for (size_t i = 0; i < n; i++) out[(*w)++] = c[i];
    out[*w] = 0;
    return true;
}

/* Chaine JSON : guillemet, barre oblique inverse et caracteres de controle echappes ; les octets
 * UTF-8 (accents) passent tels quels. */
static bool put_json_string(char *out, size_t cap, size_t *w, const char *s) {
    if (!put(out, cap, w, "\"", 1)) return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[8];
        if (*p == '"' || *p == '\\') { esc[0] = '\\'; esc[1] = (char)*p; if (!put(out, cap, w, esc, 2)) return false; }
        else if (*p < 0x20) { int n = snprintf(esc, sizeof esc, "\\u%04x", *p); if (n < 0 || !put(out, cap, w, esc, (size_t)n)) return false; }
        else if (!put(out, cap, w, (const char *)p, 1)) return false;
    }
    return put(out, cap, w, "\"", 1);
}

bool cover_travel_ok(uint32_t ms) { return ms >= COVER_TRAVEL_MIN_MS && ms <= COVER_TRAVEL_MAX_MS; }

int cover_attrs_json(uint8_t type, bool positional, const char *name, uint32_t up_ms, uint32_t down_ms,
                     char *out, size_t cap) {
    const char *tname = dev_type_name(type);
    if (!tname || !out || cap == 0) return -1;
    int n = snprintf(out, cap, "{\"type\":\"%s\",\"positional\":%s", tname, positional ? "true" : "false");
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    size_t w = (size_t)n;
    if (name && name[0] && !(put(out, cap, &w, ",\"name\":", 8) && put_json_string(out, cap, &w, name))) { out[0] = 0; return -1; }
    if (cover_travel_ok(up_ms) && cover_travel_ok(down_ms)) {
        char t[64];
        int k = snprintf(t, sizeof t, ",\"travel_up_s\":%u,\"travel_down_s\":%u",
                         (unsigned)((up_ms + 500) / 1000), (unsigned)((down_ms + 500) / 1000));
        if (k < 0 || !put(out, cap, &w, t, (size_t)k)) { out[0] = 0; return -1; }
    }
    if (!put(out, cap, &w, "}", 1)) { out[0] = 0; return -1; }
    return (int)w;
}

int json_string(const char *s, char *out, size_t cap) {
    if (!out || cap == 0) return -1;
    size_t w = 0;
    out[0] = 0;
    if (!put_json_string(out, cap, &w, s ? s : "")) { out[0] = 0; return -1; }
    return (int)w;
}
