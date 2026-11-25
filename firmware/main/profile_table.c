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
/* Les noms du fil des commandes d'un bouton (profiles.json types[].buttons[].cmd), dans l'ordre de profile_cmd_t. */
static const char *const CMD_NAMES[PROFILE_CMD_COUNT] = { "up", "stop", "down", "step" };
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

bool profile_engine_ready(const profile_def_t *p, const char **why) {
    const char *r = NULL;
    switch (profile_engine_of(p)) {
    case PROFILE_ENGINE_KEELOQ:
        if (!p->frames_per_press) r = "keeloq : tx.frames_per_press requis";
        else if (profile_add_of(p) == PROFILE_ADD_ENROL && (p->code_up < 0 || p->code_stop < 0 || p->code_down < 0))
            r = "keeloq en enrolement : un code par bouton requis (frames.<bouton>.code)";
        else if (profile_add_of(p) == PROFILE_ADD_ENROL && !p->has_enrol) r = "keeloq en enrolement : enrol requis";
        break;
    case PROFILE_ENGINE_REPLAY: {
        /* Chaque bouton rejoue avec ses repetitions et son silence : l'un sans l'autre n'emet rien de sense, et un
         * profil sans aucun bouton rejouable n'emet rien du tout. */
        int buttons = 0;
        for (int c = 0; c < PROFILE_CMD_COUNT && !r; c++) {
            if (!p->repeats[c] != !p->gap_us[c]) r = "rejeu : frames.<bouton>.repeats et gap_us vont ensemble";
            if (p->repeats[c]) buttons++;
        }
        if (!r && !buttons) r = "rejeu : au moins un bouton avec frames.<bouton>.repeats et gap_us requis";
        if (!r && p->step.len && !p->step_press_gap_ms) r = "pas a pas : frames.<bouton>.press_gap_ms requis";
        break;
    }
    case PROFILE_ENGINE_SOMFY:
        break;   /* codes et repetitions : la norme Somfy, dans somfy_rts.h */
    default:
        r = "moteur inconnu de ce firmware";
    }
    if (why) *why = r;
    return r == NULL;
}

int profile_button_code(const profile_def_t *p, profile_cmd_t cmd) {
    if (!p) return -1;
    switch (cmd) {
    case PROFILE_CMD_UP:   return p->code_up;
    case PROFILE_CMD_STOP: return p->code_stop;
    case PROFILE_CMD_DOWN: return p->code_down;
    default:               return -1;   /* le bouton pas a pas n'a pas de code : sa trame est captee */
    }
}

int profile_cmd_of(const char *name) {
    if (name)
        for (int c = 0; c < PROFILE_CMD_COUNT; c++)
            if (!strcmp(name, CMD_NAMES[c])) return c;
    return -1;
}

const char *profile_cmd_name(profile_cmd_t cmd) {
    return (cmd >= 0 && cmd < PROFILE_CMD_COUNT) ? CMD_NAMES[cmd] : NULL;
}

int profile_replay_repeats(const profile_def_t *p, profile_cmd_t cmd) {
    return (p && cmd >= 0 && cmd < PROFILE_CMD_COUNT) ? p->repeats[cmd] : 0;
}

uint32_t profile_replay_gap_us(const profile_def_t *p, profile_cmd_t cmd) {
    return (p && cmd >= 0 && cmd < PROFILE_CMD_COUNT) ? p->gap_us[cmd] : 0;
}

bool profile_has_step(const profile_def_t *p) { return p && p->step.len > 0; }

const type_def_t *type_find(uint8_t code) {
    for (int i = 0; i < TYPE_TABLE_COUNT; i++)
        if (TYPE_TABLE[i].code == code) return &TYPE_TABLE[i];
    return NULL;
}
