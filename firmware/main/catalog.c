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

static catalog_protocol_t s_proto[CAT_ENC_COUNT];
static catalog_model_t    s_models[CATALOG_MODELS_MAX];
static int                s_n_models;

int catalog_cmd_of(const char *name) {
    for (int c = 0; name && c < CAT_CMD_COUNT; c++) if (!strcmp(name, COMMAND_NAME[c])) return c;
    return -1;
}
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

    /* Les modeles, dans l'ordre des profils : un profil qui ne les declare pas n'en ajoute pas (le
     * generateur refuse qu'un modele designe deux profils, heritage compris). */
    const cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    const cJSON *prof;
    cJSON_ArrayForEach(prof, profiles) {
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
