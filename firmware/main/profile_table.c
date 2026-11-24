/*
 * profile_table.c : voir profile_table.h.
 */
#include "profile_table.h"
#include <string.h>

/* Les noms du fil (profiles.json) des moteurs et des methodes : declares UNE fois, ici. */
static const struct { profile_engine_t e; const char *name; } ENGINES[] = {
    { PROFILE_ENGINE_REPLAY, "rejeu-brut" },
    { PROFILE_ENGINE_KEELOQ, "keeloq" },
    { PROFILE_ENGINE_SOMFY,  "somfy-rts" },
};
static const struct { profile_add_t a; const char *name; } ADDS[] = {
    { PROFILE_ADD_COPY,  "copy" },
    { PROFILE_ADD_ENROL, "enrol" },
};
/* Marques par defaut d'une fiche sans marque (les memes que l'interface, profiles.ts brandOf). */
static const char BRAND_SOMFY[] = "Somfy RTS", BRAND_REPLAY[] = "CAME", BRAND_KEELOQ[] = "Profalux";

profile_engine_t profile_engine_of(const profile_def_t *p) {
    if (p && p->engine)
        for (size_t i = 0; i < sizeof ENGINES / sizeof ENGINES[0]; i++)
            if (!strcmp(p->engine, ENGINES[i].name)) return ENGINES[i].e;
    return PROFILE_ENGINE_UNKNOWN;
}

profile_add_t profile_add_of(const profile_def_t *p) {
    if (p && p->add)
        for (size_t i = 0; i < sizeof ADDS / sizeof ADDS[0]; i++)
            if (!strcmp(p->add, ADDS[i].name)) return ADDS[i].a;
    return PROFILE_ADD_UNKNOWN;
}

const profile_def_t *profile_find(const char *id) {
    if (!id || !*id) return NULL;
    for (int i = 0; i < PROFILE_TABLE_COUNT; i++)
        if (!strcmp(PROFILE_TABLE[i].id, id)) return &PROFILE_TABLE[i];
    return NULL;
}

/* La methode deduite de la fiche (voir profile_resolve). */
static void method_of(bool virt, bool somfy, bool replay, profile_add_t *add, profile_engine_t *engine) {
    *add    = virt ? PROFILE_ADD_ENROL : PROFILE_ADD_COPY;
    *engine = virt ? (somfy ? PROFILE_ENGINE_SOMFY : PROFILE_ENGINE_KEELOQ)
                   : (replay ? PROFILE_ENGINE_REPLAY : PROFILE_ENGINE_KEELOQ);
}

const profile_def_t *profile_resolve(const char *brand, bool virt, bool somfy, bool replay) {
    profile_add_t add; profile_engine_t engine;
    method_of(virt, somfy, replay, &add, &engine);
    const char *wanted = (brand && *brand) ? brand
                       : (virt && somfy) ? BRAND_SOMFY : (!virt && replay) ? BRAND_REPLAY : BRAND_KEELOQ;
    const profile_def_t *first = NULL;
    for (int i = 0; i < PROFILE_TABLE_COUNT; i++) {
        const profile_def_t *p = &PROFILE_TABLE[i];
        if (profile_add_of(p) != add || profile_engine_of(p) != engine) continue;
        if (!strcmp(p->brand, wanted)) return p;
        if (!first) first = p;
    }
    return first;
}

const profile_def_t *profile_for_device(const char *current, const char *brand, bool virt, bool somfy, bool replay) {
    const profile_def_t *p = profile_find(current);
    if (p) {
        profile_add_t add; profile_engine_t engine;
        method_of(virt, somfy, replay, &add, &engine);
        bool same_brand = !brand || !*brand || !strcmp(p->brand, brand);
        if (profile_add_of(p) == add && profile_engine_of(p) == engine && same_brand) return p;
    }
    return profile_resolve(brand, virt, somfy, replay);
}
