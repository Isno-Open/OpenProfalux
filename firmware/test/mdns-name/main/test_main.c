/* Banc de test de mdns_name (nom d'hote mDNS du boitier), sur PC. Voir README.md. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "mdns_name.h"

static int g_fail = 0;
static void check(bool ok, const char *name, const char *got) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s (obtenu « %s »)\n", name, got);
    if (!ok) g_fail++;
}

int main(void) {
    char o[40];
    /* Deux MAC distinctes, comme sur une carte : point d'acces = station + 1 (ESP-IDF). Le nom par
     * defaut prend celle du point d'acces (comme le SSID), l'identifiant du service celle de la station. */
    static const uint8_t mac[6] = { 0xec, 0xe3, 0x34, 0x1a, 0x0a, 0xc1 };   /* point d'acces */
    static const uint8_t sta[6] = { 0xec, 0xe3, 0x34, 0x1a, 0x0a, 0xc0 };   /* station */
    printf("Banc mdns_name\n");

    mdns_name_sanitize("Salon", o, sizeof(o));               check(!strcmp(o, "salon"), "T0 minuscules", o);
    mdns_name_sanitize("Boîtier Étage", o, sizeof(o));       check(!strcmp(o, "bo-tier-tage"), "T1 accents et espaces -> tirets", o);
    mdns_name_sanitize("  --a__b!!c--  ", o, sizeof(o));     check(!strcmp(o, "a-b-c"), "T2 pas de tiret en tete, en queue ni double", o);
    check(mdns_name_sanitize("!!! ???", o, sizeof(o)) == 0 && !o[0], "T3 nom tout invalide -> vide", o);
    mdns_name_sanitize("abcdefghij-abcdefghij-abcdefghij-abcdef", o, sizeof(o));
    check(strlen(o) == 32 && !strcmp(o, "abcdefghij-abcdefghij-abcdefghij"), "T4 tronque a 32", o);
    mdns_name_sanitize("abcdefghij-abcdefghij-abcdefghij-x", o, sizeof(o));
    check(strlen(o) <= 32 && o[strlen(o) - 1] != '-', "T5 pas de tiret en queue apres troncature", o);
    mdns_name_sanitize("ab", o, 3);                          check(!strcmp(o, "ab"), "T6 respecte la taille du tampon", o);

    mdns_name_default(mac, o, sizeof(o));                    check(!strcmp(o, "openprofalux-0ac1"), "T7 defaut : suffixe MAC (comme OpenProfalux_0AC1)", o);
    check(mdns_name_is_default_device("") && mdns_name_is_default_device("openprofalux")
          && mdns_name_is_default_device("volet_test") && !mdns_name_is_default_device("salon"),
          "T8 noms par defaut reconnus", "");
    mdns_name_first_boot(true, mac, o, sizeof(o));           check(!strcmp(o, "openprofalux"), "T9 boitier deja installe : openprofalux inchange", o);
    mdns_name_first_boot(false, mac, o, sizeof(o));          check(!strcmp(o, "openprofalux-0ac1"), "T10 boitier neuf : nom a suffixe", o);
    check(mdns_name_is_default_device("OpenProfalux") && mdns_name_is_default_device("OPENPROFALUX")
          && mdns_name_is_default_device("Volet_Test") && !mdns_name_is_default_device("OpenProfalux2"),
          "T11 noms par defaut reconnus sans tenir compte des majuscules", "");
    mdns_name_id(sta, o, sizeof(o));                         check(!strcmp(o, "ece3341a0ac0"), "T12 identifiant : MAC station en 12 hexa minuscules", o);

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
