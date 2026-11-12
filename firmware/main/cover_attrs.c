/* Description d'un appareil pour le portail cloud. Voir cover_attrs.h. */
#include "cover_attrs.h"
#include "dev_type.h"
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

int cover_attrs_json(uint8_t type, bool positional, char *out, size_t cap) {
    const char *name = dev_type_name(type);
    if (!name || !out || cap == 0) return -1;
    int n = snprintf(out, cap, "{\"type\":\"%s\",\"positional\":%s}", name, positional ? "true" : "false");
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    return n;
}
