/* req_guard : voir req_guard.h. */
#include "req_guard.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

/* [*b, *e) sans les espaces autour. */
static void trim(const char **b, const char **e) {
    while (*b < *e && isspace((unsigned char)**b)) (*b)++;
    while (*e > *b && isspace((unsigned char)(*e)[-1])) (*e)--;
}

static bool ieq(const char *a, size_t na, const char *b, size_t nb) {
    if (na != nb) return false;
    for (size_t i = 0; i < na; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* Hote d'une autorite "hote[:port]" ou "[ipv6][:port]", sans le port ni le point final d'un nom. */
static bool host_part(const char *s, const char *end, const char **hb, const char **he) {
    if (s < end && *s == '[') {   /* IPv6 : les crochets font partie de l'hote */
        const char *c = memchr(s, ']', (size_t)(end - s));
        if (!c) return false;
        *hb = s; *he = c + 1;
        return true;
    }
    const char *c = memchr(s, ':', (size_t)(end - s));
    *hb = s; *he = c ? c : end;
    if (*he > *hb && (*he)[-1] == '.') (*he)--;   /* openprofalux.local. = openprofalux.local */
    return *he > *hb;
}

bool rg_write_ok(const char *sfs, const char *origin, const char *host) {
    if (sfs) {
        const char *b = sfs, *e = sfs + strlen(sfs);
        trim(&b, &e);
        return !ieq(b, (size_t)(e - b), "cross-site", 10);
    }
    if (!origin) return true;   /* pas un navigateur : Home Assistant en REST, curl, scripts */
    const char *ob = origin, *oe = origin + strlen(origin);
    trim(&ob, &oe);
    const char *p = strstr(ob, "://");
    if (!p || p >= oe) return false;   /* "null", ou une valeur qui n'est pas une origine */
    p += 3;
    const char *slash = memchr(p, '/', (size_t)(oe - p));   /* une origine n'a pas de chemin */
    if (slash) oe = slash;
    const char *ohb, *ohe, *hhb, *hhe;
    if (!host_part(p, oe, &ohb, &ohe) || !host) return false;
    const char *hb = host, *he = host + strlen(host);
    trim(&hb, &he);
    if (!host_part(hb, he, &hhb, &hhe)) return false;
    return ieq(ohb, (size_t)(ohe - ohb), hhb, (size_t)(hhe - hhb));
}
