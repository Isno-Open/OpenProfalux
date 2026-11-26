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

/* L'identifiant est-il dans la liste (terminee par NULL) ? */
static bool taken_in(const char *uid, void *ctx) {
    for (const char *const *p = ctx; *p; p++) if (!strcmp(*p, uid)) return true;
    return false;
}

int main(int argc, char **argv) {
    printf("Banc cover_attrs\n");
    char out[96];

    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = NULL, .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) > 0 && !strcmp(out, "{\"type\":\"volet\",\"positional\":true}"),
          "T0 volet positionnel");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_PORTAIL, .positional = false, .name = "", .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) > 0 && !strcmp(out, "{\"type\":\"portail\",\"positional\":false}"),
          "T1 portail impulsionnel");
    check(cover_attrs_json(&(cover_attrs_t){ .type = 200, .positional = false, .name = "x", .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) == -1, "T2 type inconnu : refuse, rien d'invente");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = NULL, .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, 10) == -1 && out[0] == 0, "T3 tampon trop petit : refuse, chaine vide");
    check(dev_type_name(TYPE_PORTAIL + 1) == NULL, "T4 au-dela de l'enum : NULL");
    /* le vrai nom de l'appareil, tel que l'utilisateur l'a donne, echappe pour JSON */
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = "Chambre Parents", .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) > 0
          && !strcmp(out, "{\"type\":\"volet\",\"positional\":true,\"name\":\"Chambre Parents\"}"), "T7 nom : tel quel");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = false, .name = "S\xc3\xa9jour d'\xc3\xa9t\xc3\xa9", .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) > 0
          && !strcmp(out, "{\"type\":\"volet\",\"positional\":false,\"name\":\"S\xc3\xa9jour d'\xc3\xa9t\xc3\xa9\"}"), "T8 nom : accents UTF-8 et apostrophe intacts");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = false, .name = "a\"b\\c\nd", .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) > 0
          && !strcmp(out, "{\"type\":\"volet\",\"positional\":false,\"name\":\"a\\\"b\\\\c\\u000ad\"}"), "T9 nom : guillemet, barre oblique inverse et retour a la ligne echappes");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = "Chambre Parents", .up_ms = 0, .down_ms = 0, .rank = 0, .orientation = -1 }, out, 50) == -1 && out[0] == 0, "T10 nom qui ne tient pas : refuse, chaine vide");
    /* temps de course : publies (en secondes arrondies) seulement si le volet est etalonne */
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = "V", .up_ms = 19000, .down_ms = 17600, .rank = 0, .orientation = -1 }, out, sizeof out) > 0
          && !strcmp(out, "{\"type\":\"volet\",\"positional\":true,\"name\":\"V\",\"travel_up_s\":19,\"travel_down_s\":18}"), "T11 etalonne : temps de course publies");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = false, .name = "V", .up_ms = 19000, .down_ms = 0, .rank = 0, .orientation = -1 }, out, sizeof out) > 0 && !strstr(out, "travel"), "T12 etalonnage incomplet : rien de publie");
    check(cover_travel_ok(COVER_TRAVEL_MIN_MS) && cover_travel_ok(COVER_TRAVEL_MAX_MS) && !cover_travel_ok(COVER_TRAVEL_MIN_MS - 1)
          && !cover_travel_ok(COVER_TRAVEL_MAX_MS + 1) && !cover_travel_ok(0), "T13 bornes du temps de course");

    /* rang dans la liste et orientation de la facade : publies seulement dans leurs bornes */
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = "V", .rank = 3, .orientation = 180 }, out, sizeof out) > 0
          && !strcmp(out, "{\"type\":\"volet\",\"positional\":true,\"name\":\"V\",\"rank\":3,\"orientation\":180}"), "T18 rang et orientation publies");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .rank = 0, .orientation = -1 }, out, sizeof out) > 0
          && !strstr(out, "rank") && !strstr(out, "orientation"), "T19 non range, orientation non definie : rien de publie");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .rank = COVER_DEVICES_MAX + 1, .orientation = 360 }, out, sizeof out) > 0
          && !strstr(out, "rank") && !strstr(out, "orientation"), "T20 hors bornes : rien de publie");

    /* role et membres : seulement pour une centrale ou une zone, membres toujours presents (meme vides) */
    char big[256];
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .name = "Tout", .orientation = -1, .role = COVER_ROLE_CENTRAL, .members = "Salon,Chambre_Parents" }, big, sizeof big) > 0
          && !strcmp(big, "{\"type\":\"volet\",\"positional\":false,\"name\":\"Tout\",\"role\":\"central\",\"members\":[\"Salon\",\"Chambre_Parents\"]}"), "T22 centrale : role et membres");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .orientation = -1, .role = COVER_ROLE_ZONE, .members = NULL }, big, sizeof big) > 0
          && strstr(big, "\"role\":\"zone\",\"members\":[]}"), "T23 zone sans suiveur : liste vide");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .orientation = -1, .role = COVER_ROLE_SIMPLE, .members = "x" }, big, sizeof big) > 0
          && !strstr(big, "role") && !strstr(big, "members"), "T24 appareil simple : ni role ni membres");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .orientation = -1, .role = COVER_ROLE_CENTRAL, .members = "a,,b," }, big, sizeof big) > 0
          && strstr(big, "\"members\":[\"a\",\"b\"]}"), "T25 membres : vides ignores");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .orientation = -1, .role = COVER_ROLE_CENTRAL, .members = "Salon,Chambre" }, big, 60) == -1 && big[0] == 0, "T26 membres qui ne tiennent pas : refuse, chaine vide");
    check(cover_role_name(COVER_ROLE_SIMPLE) == NULL && cover_role_name(COVER_ROLE_COUNT) == NULL, "T27 role simple ou inconnu : pas de nom");

    /* slug d'un nom, et identifiant stable d'un appareil (garde au renommage) */
    char sl[COVER_NAME_MAX + 1], uid[COVER_NAME_MAX + 1];
    cover_slug(sl, sizeof sl, "Chambre Parents");
    check(!strcmp(sl, "Chambre_Parents"), "T30 slug : hors alphabet devient _");
    cover_slug(sl, sizeof sl, "");
    check(!strcmp(sl, "volet"), "T31 slug d'un nom vide : volet");
    cover_slug(sl, 4, "Salon");
    check(!strcmp(sl, "Sal"), "T32 slug : borne par la capacite");
    check(cover_uid_ok("Chambre_Parents") && cover_uid_ok("a-1") && !cover_uid_ok("") && !cover_uid_ok(NULL) && !cover_uid_ok("a b")
          && !cover_uid_ok("s\xc3\xa9jour") && cover_uid_ok("12345678901234567890123") && !cover_uid_ok("123456789012345678901234"),
          "T33 identifiant stable : alphabet d'un slug, 1 a COVER_NAME_MAX octets");
    cover_uid_pick(uid, sizeof uid, "Chambre Parents", NULL, NULL);
    check(!strcmp(uid, "Chambre_Parents"), "T34 identifiant libre : le slug du nom");
    cover_uid_pick(uid, sizeof uid, "Salon", taken_in, (void *)(const char *const[]){ "Salon", NULL });
    check(!strcmp(uid, "Salon_2"), "T35 identifiant deja porte : suffixe _2");
    cover_uid_pick(uid, sizeof uid, "Salon", taken_in, (void *)(const char *const[]){ "Salon", "Salon_2", "Salon_3", NULL });
    check(!strcmp(uid, "Salon_4"), "T36 suffixes deja portes : le suivant");
    cover_uid_pick(uid, sizeof uid, "12345678901234567890123", taken_in, (void *)(const char *const[]){ "12345678901234567890123", NULL });
    check(!strcmp(uid, "123456789012345678901_2") && cover_uid_ok(uid), "T37 nom a la longueur maximale : base raccourcie, identifiant valable");
    cover_uid_pick(uid, 4, "Salon", NULL, NULL);
    check(uid[0] == 0, "T38 tampon trop petit : rien, plutot qu'un identifiant tronque");
    /* publie avec la description, avant le nom ; jamais un identifiant invalide */
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .positional = true, .name = "S\xc3\xa9jour", .orientation = -1, .uid = "Salon" }, big, sizeof big) > 0
          && !strcmp(big, "{\"type\":\"volet\",\"positional\":true,\"uid\":\"Salon\",\"name\":\"S\xc3\xa9jour\"}"), "T39 identifiant stable publie, independant du nom");
    check(cover_attrs_json(&(cover_attrs_t){ .type = TYPE_VOLET, .name = "V", .orientation = -1, .uid = "pas valable" }, big, sizeof big) > 0 && !strstr(big, "uid"), "T40 identifiant invalide : non publie");

    /* json_string : texte choisi par l'utilisateur (SSID) en chaine JSON */
    char js[64];
    check(json_string("Box \"Maison\"", js, sizeof js) > 0 && !strcmp(js, "\"Box \\\"Maison\\\"\""), "T15 json_string : guillemets echappes");
    check(json_string(NULL, js, sizeof js) == 2 && !strcmp(js, "\"\""), "T16 json_string : NULL donne une chaine vide");
    check(json_string("abcdefghij", js, 8) == -1 && js[0] == 0, "T17 json_string : trop long, refuse");

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
        /* Bornes du temps de course : celles que profiles.json declare (le portail en tire son formulaire) */
        const char *ts = strstr(json, "\"travel_s\"");
        unsigned mn = 0, mx = 0;
        check(ts && sscanf(ts, "\"travel_s\": { \"min\": %u, \"max\": %u", &mn, &mx) == 2
              && mn * 1000u == COVER_TRAVEL_MIN_MS && mx * 1000u == COVER_TRAVEL_MAX_MS, "T14 bornes du temps de course : celles de profiles.json");
        /* roles : chaque role nomme est une cle de profiles.json "roles" (et le portail en tire ses cartes) */
        const char *roles = strstr(json, "\"roles\"");
        for (int r = COVER_ROLE_SIMPLE + 1; r < COVER_ROLE_COUNT; r++) {
            char key[48], name[80];
            snprintf(key, sizeof key, "\"%s\":", cover_role_name((cover_role_t)r) ? cover_role_name((cover_role_t)r) : "?");
            snprintf(name, sizeof name, "T28 role %d : declare dans profiles.json roles", r);
            const char *k = roles ? strstr(roles, key) : NULL;
            const char *types = strstr(json, "\"types\"");
            check(k && (!types || types < roles || k < types), name);
        }
        const char *nm = strstr(json, "\"name_max\"");
        unsigned nmax = 0;
        check(nm && sscanf(nm, "\"name_max\": %u", &nmax) == 1 && nmax == COVER_NAME_MAX, "T29 longueur maximale d'un nom : celle de profiles.json");
        const char *dm = strstr(json, "\"devices_max\"");
        unsigned dmax = 0;
        check(dm && sscanf(dm, "\"devices_max\": %u", &dmax) == 1 && dmax == COVER_DEVICES_MAX, "T21 nombre maximal d'appareils : celui de profiles.json");
    }
    printf(g_fail ? "ECHEC : %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
