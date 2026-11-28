/* Le catalogue lu au demarrage. Voir catalog.h. */
#include "catalog.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

/* Noms du catalogue que ce firmware connait : ses encodeurs (du code) et les commandes d'un appareil.
 * Un seul endroit les ecrit ; le schema (profiles.schema.json) enumere les memes. */
static const char *const ENCODER_NAME[CAT_ENC_COUNT] = {
    [CAT_ENC_KEELOQ] = "keeloq", [CAT_ENC_SOMFY_RTS] = "somfy-rts", [CAT_ENC_RAW_REPLAY] = "raw-replay",
};
static const char *const COMMAND_NAME[CAT_CMD_COUNT] = {
    [CAT_CMD_UP] = "up", [CAT_CMD_STOP] = "stop", [CAT_CMD_DOWN] = "down", [CAT_CMD_PROG] = "prog",
};

static const char *const COMPONENT_NAME[] = { CATALOG_COMPONENTS };
static const char *const COVER_CLASS[] = { CATALOG_COVER_CLASSES };
#define COVER_CLASS_COUNT (sizeof COVER_CLASS / sizeof COVER_CLASS[0])
_Static_assert(sizeof COMPONENT_NAME / sizeof COMPONENT_NAME[0] == CAT_COMPONENT_COUNT, "CATALOG_COMPONENTS : un nom par famille");

/* Commande d'un bouton pas a pas (types[].buttons[].command) : il parcourt son cycle. */
#define BUTTON_STEP "step"
/* Creneau ou se range la trame d'un bouton pas a pas. Le pas a pas n'est pas encore emis (#134, etape 5) :
 * la trame unique du portail se capture sous « up » (ui/src/profiles.ts, captureSlot) et porte les
 * commandes de son cycle. */
#define STEP_SLOT CAT_CMD_UP
/* Methode d'ajout d'un profil (« add ») qui fait apprendre le boitier au moteur ; l'autre est la copie. */
#define ADD_ENROL "enrol"

static catalog_protocol_t s_proto[CAT_ENC_COUNT];
static catalog_model_t    s_models[CATALOG_MODELS_MAX];
static int                s_n_models;
static catalog_type_t     s_types[CATALOG_TYPES_MAX];
static int                s_n_types;
static catalog_profile_t  s_profiles[CATALOG_PROFILES_MAX];
static int                s_n_profiles;

int catalog_cmd_of(const char *name) {
    for (int c = 0; name && c < CAT_CMD_COUNT; c++) if (!strcmp(name, COMMAND_NAME[c])) return c;
    return -1;
}
const char *catalog_component_name(catalog_component_t c) { return (unsigned)c < CAT_COMPONENT_COUNT ? COMPONENT_NAME[c] : NULL; }
const char *catalog_cmd_name(catalog_cmd_t cmd) { return (unsigned)cmd < CAT_CMD_COUNT ? COMMAND_NAME[cmd] : NULL; }
static int encoder_of(const char *name) {
    for (int e = 0; name && e < CAT_ENC_COUNT; e++) if (!strcmp(name, ENCODER_NAME[e])) return e;
    return -1;
}

/* Entier borne lu dans un objet : -1 si absent, pas un entier, ou hors [lo, hi]. */
static long int_in(const cJSON *o, const char *key, long lo, long hi) {
    const cJSON *x = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(x) || x->valuedouble != (double)(long)x->valuedouble) return -1;
    long v = (long)x->valuedouble;
    return (v < lo || v > hi) ? -1 : v;
}

static void reset(void) {
    memset(s_proto, 0, sizeof s_proto);
    memset(s_models, 0, sizeof s_models);
    s_n_models = 0;
    memset(s_types, 0, sizeof s_types);
    s_n_types = 0;
    memset(s_profiles, 0, sizeof s_profiles);
    s_n_profiles = 0;
}

static int fail(char *err, size_t errlen, const char *what, const char *where) {
    if (err && errlen) snprintf(err, errlen, "catalogue : %s (%s)", what, where ? where : "");
    reset();
    return -1;
}

static int read_protocol(const cJSON *p, char *err, size_t errlen) {
    const char *key = p->string;
    int enc = encoder_of(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "encoder")));
    if (enc < 0) return fail(err, errlen, "encodeur inconnu de ce firmware", key);
    catalog_protocol_t *out = &s_proto[enc];
    if (out->present) return fail(err, errlen, "deux protocoles pour le meme encodeur", key);
    out->present = true;
    for (int c = 0; c < CAT_CMD_COUNT; c++) out->code[c] = CATALOG_NO_CODE;
    out->enrol.code = CATALOG_NO_CODE;
    out->enrol.command = -1;

    const cJSON *cmds = cJSON_GetObjectItemCaseSensitive(p, "commands");
    const cJSON *c;
    cJSON_ArrayForEach(c, cmds) {
        int cmd = catalog_cmd_of(c->string);
        long code = int_in(c, "code", 0, 15);
        if (cmd < 0) continue;   /* commande que ce firmware n'emet pas (step : un bouton, pas un code) */
        if (code < 0) return fail(err, errlen, "code de commande absent ou hors 0..15", key);
        out->code[cmd] = (int8_t)code;
    }

    const cJSON *press = cJSON_GetObjectItemCaseSensitive(p, "press");
    long frames = int_in(press, "frames", 1, 255);
    if (frames < 0) return fail(err, errlen, "press.frames absent ou hors 1..255", key);
    out->press_frames = (uint8_t)frames;
    if (cJSON_GetObjectItemCaseSensitive(press, "gap_us")) {
        long gap = int_in(press, "gap_us", 1, 1000000);
        if (gap < 0) return fail(err, errlen, "press.gap_us hors 1..1000000", key);
        out->press_gap_us = (uint32_t)gap;
    }

    const cJSON *enrol = cJSON_GetObjectItemCaseSensitive(p, "enrol");
    if (enrol) {
        if (cJSON_GetObjectItemCaseSensitive(enrol, "code")) {
            long code = int_in(enrol, "code", 0, 15);
            long fpb = int_in(enrol, "frames_per_burst", 1, 255), bursts = int_in(enrol, "bursts", 1, 255);
            long gap = int_in(enrol, "burst_gap_ms", 0, 60000);
            if (code < 0 || fpb < 0 || bursts < 0 || gap < 0)
                return fail(err, errlen, "enrol : code, frames_per_burst, bursts et burst_gap_ms attendus", key);
            out->enrol.code = (int8_t)code;
            out->enrol.frames_per_burst = (uint8_t)fpb;
            out->enrol.bursts = (uint8_t)bursts;
            out->enrol.burst_gap_ms = (uint16_t)gap;
        }
        const cJSON *command = cJSON_GetObjectItemCaseSensitive(enrol, "command");
        if (command) {
            int cmd = catalog_cmd_of(cJSON_GetStringValue(command));
            if (cmd < 0 || out->code[cmd] == CATALOG_NO_CODE) return fail(err, errlen, "enrol.command sans code dans commands", key);
            out->enrol.command = (int8_t)cmd;
        }
    }
    return 0;
}

static int read_type(const cJSON *t, char *err, size_t errlen) {
    if (s_n_types >= CATALOG_TYPES_MAX) return fail(err, errlen, "trop de types", t->string);
    if (strlen(t->string) >= CATALOG_ID_MAX) return fail(err, errlen, "cle de type trop longue", t->string);
    catalog_type_t *out = &s_types[s_n_types++];
    snprintf(out->key, sizeof out->key, "%s", t->string);
    out->positional = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(t, "positional"));
    /* L'entite Home Assistant : une famille que ce firmware sait annoncer, une classe de cette famille */
    const cJSON *entity = cJSON_GetObjectItemCaseSensitive(t, "entity");
    const char *component = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entity, "component"));
    const char *dclass = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entity, "device_class"));
    int comp = -1;
    for (int c = 0; component && c < CAT_COMPONENT_COUNT; c++) if (!strcmp(component, COMPONENT_NAME[c])) comp = c;
    if (comp < 0) return fail(err, errlen, "entity.component inconnu", t->string);
    out->component = (catalog_component_t)comp;
    bool known = false;
    for (size_t c = 0; dclass && c < COVER_CLASS_COUNT; c++) if (!strcmp(dclass, COVER_CLASS[c])) known = true;
    if (!known) return fail(err, errlen, "entity.device_class inconnue", t->string);
    snprintf(out->device_class, sizeof out->device_class, "%s", dclass);
    const cJSON *legacy = cJSON_GetObjectItemCaseSensitive(t, "legacy");
    out->legacy = legacy ? (int)int_in(legacy, "code", 0, 255) : CATALOG_NO_LEGACY;
    if (legacy && out->legacy < 0) return fail(err, errlen, "legacy.code hors 0..255", t->string);
    for (int i = 0; i + 1 < s_n_types; i++)
        if (out->legacy != CATALOG_NO_LEGACY && s_types[i].legacy == out->legacy) return fail(err, errlen, "meme numero legacy", t->string);
    /* Les boutons : quelle commande chacun porte, et ou se range sa trame */
    for (int c = 0; c < CAT_CMD_COUNT; c++) out->slot[c] = CATALOG_NO_SLOT;
    const cJSON *b;
    cJSON_ArrayForEach(b, cJSON_GetObjectItemCaseSensitive(t, "buttons")) {
        const char *command = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(b, "command"));
        const int c = catalog_cmd_of(command);
        if (c >= 0) { out->slot[c] = (int8_t)c; continue; }
        if (!command || strcmp(command, BUTTON_STEP)) return fail(err, errlen, "bouton de commande inconnue", t->string);
        if (out->step.len) return fail(err, errlen, "deux boutons pas a pas", t->string);
        const cJSON *step;
        cJSON_ArrayForEach(step, cJSON_GetObjectItemCaseSensitive(b, "cycle")) {
            const int sc = catalog_cmd_of(cJSON_GetStringValue(step));
            if (sc < 0 || sc == CAT_CMD_PROG) return fail(err, errlen, "cycle : commande inconnue", t->string);
            if (out->step.len >= CATALOG_CYCLE_MAX) return fail(err, errlen, "cycle trop long", t->string);
            out->step.act[out->step.len++] = (int8_t)sc;
            if (out->slot[sc] == CATALOG_NO_SLOT) out->slot[sc] = STEP_SLOT;
        }
        const long gap = int_in(b, "press_gap_ms", 1, CATALOG_PRESS_GAP_MAX_MS);
        if (!out->step.len || gap < 0) return fail(err, errlen, "bouton pas a pas sans cycle ou sans press_gap_ms", t->string);
        out->step.press_gap_ms = (uint16_t)gap;
    }
    return 0;
}

/* Les profils, resolus : un profil qui herite prend de sa base ce qu'il ne dit pas (objets fusionnes sur
 * un niveau, comme pyramid-detect.js). Lus dans l'ordre ; une base doit donc preceder ce qui en herite,
 * et le generateur refuse deja une base absente ou une boucle. */
static int read_profile(const cJSON *p, const cJSON *protocols, char *err, size_t errlen) {
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "id"));
    if (!id || strlen(id) >= CATALOG_ID_MAX) return fail(err, errlen, "identifiant de profil absent ou trop long", id);
    if (s_n_profiles >= CATALOG_PROFILES_MAX) return fail(err, errlen, "trop de profils", id);
    catalog_profile_t *out = &s_profiles[s_n_profiles];
    const char *base = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "inherits"));
    if (base) {
        const catalog_profile_t *b = catalog_profile(base);
        if (!b) return fail(err, errlen, "base absente ou declaree apres", id);
        *out = *b;
    } else {
        memset(out, 0, sizeof *out);
        out->encoder = CAT_ENC_COUNT;
        out->repeat_bit = out->rmt = -1;
    }
    snprintf(out->id, sizeof out->id, "%s", id);
    const char *brand = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "brand"));
    if (brand) snprintf(out->brand, sizeof out->brand, "%s", brand);
    const char *type = cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(p, "types"), 0));
    if (type) snprintf(out->type, sizeof out->type, "%s", type);
    const char *add = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "add"));
    if (add) out->enrol = !strcmp(add, ADD_ENROL);
    const char *proto = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "protocol"));
    if (proto) {
        const cJSON *pr = cJSON_GetObjectItemCaseSensitive(protocols, proto);
        int enc = encoder_of(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(pr, "encoder")));
        if (enc < 0) return fail(err, errlen, "protocole inconnu", id);
        out->encoder = (catalog_encoder_t)enc;
    }
    const cJSON *radio = cJSON_GetObjectItemCaseSensitive(p, "radio");
    if (cJSON_GetObjectItemCaseSensitive(radio, "freq_khz")) {
        long f = int_in(radio, "freq_khz", 1, 10000000);
        if (f < 0) return fail(err, errlen, "radio.freq_khz hors bornes", id);
        out->freq_khz = (uint32_t)f;
    }
    const cJSON *tx = cJSON_GetObjectItemCaseSensitive(p, "tx");
    if (cJSON_GetObjectItemCaseSensitive(tx, "te_us")) {
        long te = int_in(tx, "te_us", 1, 65535);
        if (te < 0) return fail(err, errlen, "tx.te_us hors bornes", id);
        out->te_us = (uint16_t)te;
    }
    const cJSON *rb = cJSON_GetObjectItemCaseSensitive(tx, "repeat_bit");
    if (cJSON_IsBool(rb)) out->repeat_bit = cJSON_IsTrue(rb) ? 1 : 0;
    const cJSON *rmt = cJSON_GetObjectItemCaseSensitive(tx, "rmt");
    if (cJSON_IsBool(rmt)) out->rmt = cJSON_IsTrue(rmt) ? 1 : 0;
    if (cJSON_GetObjectItemCaseSensitive(tx, "frame_bits")) {
        long fb = int_in(tx, "frame_bits", 1, 255);
        if (fb < 0) return fail(err, errlen, "tx.frame_bits hors bornes", id);
        out->frame_bits = (uint8_t)fb;
    }
    if (out->encoder == CAT_ENC_COUNT || !out->type[0] || !out->freq_khz)
        return fail(err, errlen, "profil sans protocole, type ou porteuse (ni declare ni herite)", id);
    s_n_profiles++;
    return 0;
}

int catalog_load(const char *json, char *err, size_t errlen) {
    reset();
    if (err && errlen) err[0] = 0;
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!root) return fail(err, errlen, "JSON illisible", NULL);
    int rc = 0;

    const cJSON *protocols = cJSON_GetObjectItemCaseSensitive(root, "protocols");
    const cJSON *p;
    if (!cJSON_IsObject(protocols)) rc = fail(err, errlen, "objet protocols attendu", NULL);
    cJSON_ArrayForEach(p, protocols) {
        if (rc == 0) rc = read_protocol(p, err, errlen);
    }

    const cJSON *types = cJSON_GetObjectItemCaseSensitive(root, "types");
    const cJSON *t;
    if (rc == 0 && !cJSON_IsObject(types)) rc = fail(err, errlen, "objet types attendu", NULL);
    cJSON_ArrayForEach(t, types) {
        if (rc == 0 && t->string[0] != '_') rc = read_type(t, err, errlen);
    }

    /* Les modeles, dans l'ordre des profils : un profil qui ne les declare pas n'en ajoute pas (le
     * generateur refuse qu'un modele designe deux profils, heritage compris). */
    const cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    const cJSON *prof;
    cJSON_ArrayForEach(prof, profiles) {
        if (rc == 0) rc = read_profile(prof, protocols, err, errlen);
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(prof, "id"));
        const cJSON *m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(prof, "models")) {
            const char *name = cJSON_GetStringValue(m);
            if (rc) break;
            if (!id || !name || strlen(name) >= CATALOG_NAME_MAX || strlen(id) >= CATALOG_ID_MAX)
                rc = fail(err, errlen, "modele ou profil trop long", name);
            else if (s_n_models >= CATALOG_MODELS_MAX)
                rc = fail(err, errlen, "trop de modeles", name);
            else {
                snprintf(s_models[s_n_models].name, CATALOG_NAME_MAX, "%s", name);
                snprintf(s_models[s_n_models].profile, CATALOG_ID_MAX, "%s", id);
                s_n_models++;
            }
        }
    }
    cJSON_Delete(root);
    return rc;
}

const catalog_protocol_t *catalog_protocol(catalog_encoder_t enc) {
    return (enc < CAT_ENC_COUNT && s_proto[enc].present) ? &s_proto[enc] : NULL;
}
int catalog_code(catalog_encoder_t enc, catalog_cmd_t cmd) {
    const catalog_protocol_t *p = catalog_protocol(enc);
    return (p && cmd < CAT_CMD_COUNT) ? p->code[cmd] : CATALOG_NO_CODE;
}
int catalog_model_count(void) { return s_n_models; }
const catalog_model_t *catalog_model(int i) { return (i >= 0 && i < s_n_models) ? &s_models[i] : NULL; }

const catalog_type_t *catalog_type(const char *key) {
    for (int i = 0; key && i < s_n_types; i++) if (!strcmp(s_types[i].key, key)) return &s_types[i];
    return NULL;
}
const char *catalog_type_of_legacy(int code) {
    for (int i = 0; code != CATALOG_NO_LEGACY && i < s_n_types; i++) if (s_types[i].legacy == code) return s_types[i].key;
    return NULL;
}
const catalog_profile_t *catalog_profile(const char *id) {
    for (int i = 0; id && i < s_n_profiles; i++) if (!strcmp(s_profiles[i].id, id)) return &s_profiles[i];
    return NULL;
}
const catalog_profile_t *catalog_profile_infer(catalog_encoder_t enc, bool enrol, const char *brand) {
    const catalog_profile_t *first = NULL;
    for (int i = 0; i < s_n_profiles; i++) {
        const catalog_profile_t *p = &s_profiles[i];
        if (p->encoder != enc || p->enrol != enrol) continue;
        if (brand && brand[0] && !strcmp(p->brand, brand)) return p;
        if (!first) first = p;
    }
    return first;
}
