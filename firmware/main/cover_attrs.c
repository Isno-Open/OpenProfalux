/* Description d'un appareil pour le portail cloud. Voir cover_attrs.h. */
#include "cover_attrs.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Noms des roles : MIROIR des cles de profiles.json "roles" (banc test/cover-attrs). */
static const char *const COVER_ROLE_NAMES[COVER_ROLE_COUNT] = {
    [COVER_ROLE_SIMPLE]  = NULL,
    [COVER_ROLE_CENTRAL] = "central",
    [COVER_ROLE_ZONE]    = "zone",
};

const char *cover_role_name(cover_role_t role) {
    return (unsigned)role < COVER_ROLE_COUNT ? COVER_ROLE_NAMES[role] : NULL;
}

static bool slug_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
void cover_slug(char *dst, size_t cap, const char *name) {
    size_t p = 0;
    if (!dst || cap == 0) return;
    for (size_t k = 0; name && name[k] && p + 1 < cap; k++) dst[p++] = slug_char(name[k]) ? name[k] : '_';
    dst[p] = 0;
    if (!p && cap > 6) { memcpy(dst, "volet", 6); }
}
bool cover_uid_ok(const char *uid) {
    if (!uid || !uid[0]) return false;
    size_t n = 0;
    for (; uid[n]; n++) if (!slug_char(uid[n]) || n >= COVER_NAME_MAX) return false;
    return true;
}
void cover_uid_pick(char *out, size_t cap, const char *name, bool (*taken)(const char *uid, void *ctx), void *ctx) {
    char base[COVER_NAME_MAX + 1];
    if (!out || cap < sizeof base) { if (out && cap) out[0] = 0; return; }
    cover_slug(base, sizeof base, name);
    memcpy(out, base, strlen(base) + 1);
    /* deja porte par un autre appareil (l'ancien « Salon » renomme, puis un nouveau « Salon ») : suffixe,
     * en raccourcissant la base pour rester dans la longueur d'un nom */
    for (unsigned n = 2; taken && taken(out, ctx) && n < 1000; n++) {
        char suf[8];
        int k = snprintf(suf, sizeof suf, "_%u", n);
        size_t keep = strlen(base);
        if (keep + (size_t)k > COVER_NAME_MAX) keep = COVER_NAME_MAX - (size_t)k;
        memcpy(out, base, keep);
        memcpy(out + keep, suf, (size_t)k + 1);
    }
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

/* Cle de type publiable telle quelle dans le JSON : non vide, alphabet d'un slug (le catalogue impose
 * [a-z][a-z0-9-]*, ceci refuse en plus toute fiche abimee). */
static bool type_key_ok(const char *t) {
    if (!t || !*t) return false;
    for (; *t; t++) if (!slug_char(*t)) return false;
    return true;
}

int cover_attrs_json(const cover_attrs_t *a, char *out, size_t cap) {
    const char *tname = a ? a->type : NULL;
    if (!type_key_ok(tname) || !out || cap == 0) return -1;
    const char *name = a->name;
    uint32_t up_ms = a->up_ms, down_ms = a->down_ms;
    int n = snprintf(out, cap, "{\"type\":\"%s\",\"positional\":%s", tname, a->positional ? "true" : "false");
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    size_t w = (size_t)n;
    /* identifiant stable : avant le nom, pour qu'un nom tres long ne puisse pas le faire sauter */
    if (cover_uid_ok(a->uid) && !(put(out, cap, &w, ",\"uid\":", 7) && put_json_string(out, cap, &w, a->uid))) { out[0] = 0; return -1; }
    if (name && name[0] && !(put(out, cap, &w, ",\"name\":", 8) && put_json_string(out, cap, &w, name))) { out[0] = 0; return -1; }
    if (cover_travel_ok(up_ms) && cover_travel_ok(down_ms)) {
        char t[64];
        int k = snprintf(t, sizeof t, ",\"travel_up_s\":%u,\"travel_down_s\":%u",
                         (unsigned)((up_ms + 500) / 1000), (unsigned)((down_ms + 500) / 1000));
        if (k < 0 || !put(out, cap, &w, t, (size_t)k)) { out[0] = 0; return -1; }
    }
    char t2[48];
    if (a->rank >= 1 && a->rank <= COVER_DEVICES_MAX) {
        int k = snprintf(t2, sizeof t2, ",\"rank\":%d", a->rank);
        if (k < 0 || !put(out, cap, &w, t2, (size_t)k)) { out[0] = 0; return -1; }
    }
    if (a->orientation >= 0 && a->orientation <= COVER_ORIENTATION_MAX) {
        int k = snprintf(t2, sizeof t2, ",\"orientation\":%d", a->orientation);
        if (k < 0 || !put(out, cap, &w, t2, (size_t)k)) { out[0] = 0; return -1; }
    }
    const char *role = cover_role_name(a->role);
    if (role) {
        /* les slugs sont des identifiants de topic (lettres, chiffres, _ et -) : rien a echapper, mais on
         * passe par put_json_string pour ne jamais produire un JSON invalide */
        if (!put(out, cap, &w, ",\"role\":", 8) || !put_json_string(out, cap, &w, role) || !put(out, cap, &w, ",\"members\":[", 12)) { out[0] = 0; return -1; }
        bool first = true;
        for (const char *p = a->members; p && *p; ) {
            const char *e = strchr(p, ',');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n > 0) {
                char one[64];
                if (n >= sizeof one) { out[0] = 0; return -1; }
                memcpy(one, p, n); one[n] = 0;
                if ((!first && !put(out, cap, &w, ",", 1)) || !put_json_string(out, cap, &w, one)) { out[0] = 0; return -1; }
                first = false;
            }
            p = e ? e + 1 : p + n;
        }
        if (!put(out, cap, &w, "]", 1)) { out[0] = 0; return -1; }
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
