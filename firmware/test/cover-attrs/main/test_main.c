/* Banc de test de la description d'appareil pour le cloud (cover_attrs.c), et de la coherence de
 * ses noms de types avec profiles.json (source du vocabulaire). Sur PC. Voir README.md. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cover_attrs.h"
#include "dev_type.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

/* Le type <name> est-il declare dans profiles.json avec ce code ? (lecture simple du texte :
 * "<name>": { ... "code": <code>, dans l'objet "types"). */
static bool declared_in_profiles(const char *json, const char *name, int code) {
    const char *types = strstr(json, "\"types\"");
    if (!types) return false;
    char key[48]; snprintf(key, sizeof key, "\"%s\":", name);
    const char *k = strstr(types, key);
    if (!k) return false;
    const char *c = strstr(k, "\"code\":");
    const char *close = strchr(k, '}');
    if (!c || (close && c > close)) return false;
    return atoi(c + 7) == code;
}

int main(int argc, char **argv) {
    printf("Banc cover_attrs\n");
    char out[96];

    check(cover_attrs_json(TYPE_VOLET, true, out, sizeof out) > 0 && !strcmp(out, "{\"type\":\"volet\",\"positional\":true}"),
          "T0 volet positionnel");
    check(cover_attrs_json(TYPE_PORTAIL, false, out, sizeof out) > 0 && !strcmp(out, "{\"type\":\"portail\",\"positional\":false}"),
          "T1 portail impulsionnel");
    check(cover_attrs_json(200, false, out, sizeof out) == -1, "T2 type inconnu : refuse, rien d'invente");
    check(cover_attrs_json(TYPE_VOLET, true, out, 10) == -1 && out[0] == 0, "T3 tampon trop petit : refuse, chaine vide");
    check(dev_type_name(TYPE_PORTAIL + 1) == NULL, "T4 au-dela de l'enum : NULL");

    /* Coherence avec profiles.json : chaque type de l'enum y est declare sous ce nom et ce code. */
    const char *path = argc > 1 ? argv[1] : "../../main/web/profiles.json";
    FILE *f = fopen(path, "rb");
    check(f != NULL, "T5 profiles.json lisible");
    if (f) {
        static char json[262144];
        size_t n = fread(json, 1, sizeof json - 1, f); json[n] = 0; fclose(f);
        for (int t = TYPE_VOLET; t <= TYPE_PORTAIL; t++) {
            char name[64]; snprintf(name, sizeof name, "T6 type %d : meme nom et meme code dans profiles.json", t);
            check(dev_type_name((uint8_t)t) && declared_in_profiles(json, dev_type_name((uint8_t)t), t), name);
        }
    }
    printf(g_fail ? "ECHEC : %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
