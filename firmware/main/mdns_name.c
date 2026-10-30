/* mdns_name — voir mdns_name.h. */
#include "mdns_name.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

size_t mdns_name_sanitize(const char *in, char *out, size_t cap) {
    size_t lim = cap - 1 < MDNS_NAME_MAX ? cap - 1 : MDNS_NAME_MAX, k = 0;
    for (const char *c = in ? in : ""; *c && k < lim; c++) {
        char ch = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) out[k++] = ch;
        else if (k && out[k - 1] != '-') out[k++] = '-';
    }
    while (k && out[k - 1] == '-') k--;   /* pas de tiret en queue (ni apres troncature) */
    out[k] = 0;
    return k;
}

void mdns_name_default(const uint8_t mac[6], char *out, size_t cap) {
    snprintf(out, cap, "openprofalux-%02x%02x", mac[4], mac[5]);
}

bool mdns_name_is_default_device(const char *device) {
    /* sans tenir compte des majuscules : « OpenProfalux » est le nom du produit, pas un choix */
    return !device || !*device || !strcasecmp(device, "openprofalux") || !strcasecmp(device, "volet_test");
}

void mdns_name_id(const uint8_t mac[6], char *out, size_t cap) {
    snprintf(out, cap, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mdns_name_first_boot(bool already_installed, const uint8_t mac[6], char *out, size_t cap) {
    if (already_installed) snprintf(out, cap, "openprofalux");
    else mdns_name_default(mac, out, cap);
}
