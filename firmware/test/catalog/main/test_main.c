/* Banc hote du catalogue (catalog.c), sur le VRAI profiles.json. Voir README.md.
 *
 * 1. Le firmware lit, dans le catalogue, EXACTEMENT les valeurs qu'il avait en dur jusqu'a la v0.2.17
 *    (shutters.c, pfx_enrol.c, somfy_tx.h) : le passage au catalogue ne change aucune emission.
 * 2. Les codes Somfy du catalogue sont ceux de la norme que l'encodeur connait (somfy_rts.h).
 * 3. Un catalogue faux est refuse en entier : rien n'est lu, donc rien n'est emis.
 *
 * Rend 1 au premier echec. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "catalog.h"
#include "somfy_rts.h"

/* Le profil du catalogue qui reconnait le modele NeoSol. */
#define NEOSOL_PROFILE "neosol-enrol"
/* Un profil qui herite (Eveno), sa base, et la marque qui le designe. */
#define EVENO_PROFILE          "eveno-enrol"
#define PROFALUX_ENROL_PROFILE "profalux-enrol"
#define EVENO_BRAND            "Eveno"

static int s_fail;
#define CHECK(cond, what) do { if (cond) printf("  ok   %s\n", what); else { printf("  ECHEC %s\n", what); s_fail = 1; } } while (0)

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); s = NULL; }
    if (s) s[n] = 0;
    fclose(f);
    return s;
}

/* Le catalogue est refuse POUR LA RAISON ATTENDUE (sinon un refus pour une autre faute passerait pour
 * la bonne), et plus rien n'est lu. */
static void refused(const char *json, const char *reason, const char *what) {
    char err[CATALOG_ERR_MAX];
    int rc = catalog_load(json, err, sizeof err);
    CHECK(rc == -1 && strstr(err, reason) && !catalog_protocol(CAT_ENC_KEELOQ) && catalog_model_count() == 0
          && !catalog_type_of_legacy(0) && !catalog_profile_infer(CAT_ENC_KEELOQ, true, ""), what);
    if (rc != -1 || !strstr(err, reason)) printf("       raison : %s\n", err);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../../main/web/profiles.json";
    char *real = read_file(path);
    if (!real) { printf("profiles.json illisible : %s\n", path); return 1; }
    char err[CATALOG_ERR_MAX];

    printf("Le vrai catalogue :\n");
    CHECK(catalog_load(real, err, sizeof err) == 0, "C1 lu sans erreur");
    if (err[0]) printf("       %s\n", err);

    const catalog_protocol_t *k = catalog_protocol(CAT_ENC_KEELOQ);
    CHECK(k && k->code[CAT_CMD_UP] == 0x2 && k->code[CAT_CMD_STOP] == 0x4 && k->code[CAT_CMD_DOWN] == 0x8,
          "C2 KeeLoq : boutons 0x2 / 0x4 / 0x8 (ex PFX_BTN_UP/STOP/DOWN)");
    CHECK(k && k->code[CAT_CMD_PROG] == CATALOG_NO_CODE, "C3 KeeLoq : pas de commande prog");
    CHECK(k && k->press_frames == 10 && k->press_gap_us == 0, "C4 KeeLoq : 10 trames par appui (ex PRESS_REPEATS)");
    CHECK(k && k->enrol.code == 0x5 && k->enrol.frames_per_burst == 9 && k->enrol.bursts == 5 && k->enrol.burst_gap_ms == 15,
          "C5 KeeLoq : apprentissage 0x5, 5 salves de 9 trames a 15 ms (ex BTN_LEARN, PFX_BURST, PFX_LEARN_ROUNDS)");

    const catalog_protocol_t *s = catalog_protocol(CAT_ENC_SOMFY_RTS);
    CHECK(s && s->code[CAT_CMD_UP] == SOMFY_RTS_CMD_UP && s->code[CAT_CMD_STOP] == SOMFY_RTS_CMD_MY
          && s->code[CAT_CMD_DOWN] == SOMFY_RTS_CMD_DOWN && s->code[CAT_CMD_PROG] == SOMFY_RTS_CMD_PROG,
          "C6 Somfy : codes de la norme (somfy_rts.h), stop = My");
    CHECK(s && s->press_frames == 2, "C7 Somfy : 2 trames par appui (ex SOMFY_RTS_TX_REPEATS 1, apres la 1re)");
    CHECK(s && s->enrol.command == CAT_CMD_PROG && s->enrol.code == CATALOG_NO_CODE, "C8 Somfy : appairage par la commande prog");

    const catalog_protocol_t *r = catalog_protocol(CAT_ENC_RAW_REPLAY);
    CHECK(r && r->press_frames == 8 && r->press_gap_us == 12000, "C9 rejeu : 8 trames, 12 ms entre deux (ex REJEU_REPEATS, REJEU_GAP_US)");
    CHECK(catalog_code(CAT_ENC_RAW_REPLAY, CAT_CMD_UP) == CATALOG_NO_CODE, "C10 rejeu : aucun code, la trame est la capture");

    /* Les modeles d'avant le catalogue, dans leur ordre, et le profil qui reconnait le troisieme. */
    static const char *const MODELS[] = { "Profalux MAI-EMPX / MUR / NOE", "FranciaFlex M4G", "Profalux NeoSol", "Profalux MAI-RDPX / RDTELPX" };
    bool same = catalog_model_count() >= 4;
    for (int i = 0; same && i < 4; i++) same = !strcmp(catalog_model(i)->name, MODELS[i]);
    CHECK(same, "C11 modeles dans l'ordre d'avant (le numero d'un modele est garde en NVS)");
    CHECK(catalog_model(2) && !strcmp(catalog_model(2)->profile, NEOSOL_PROFILE), "C12 un modele designe son profil");
    CHECK(catalog_model(catalog_model_count()) == NULL && catalog_model(-1) == NULL, "C13 modele hors borne : NULL");
    CHECK(catalog_cmd_of("stop") == CAT_CMD_STOP && catalog_cmd_of("step") == -1 && catalog_cmd_of(NULL) == -1, "C14 commande par son nom");

    printf("Types et profils :\n");
    const char *shutter = catalog_type_of_legacy(0), *gate = catalog_type_of_legacy(1);
    CHECK(shutter && gate && catalog_type(shutter)->positional && !catalog_type(gate)->positional,
          "C16 mode legacy : 0 et 1 designent un type positionnel et un type sans position");
    CHECK(catalog_type_of_legacy(7) == NULL && catalog_type("inconnu") == NULL, "C17 numero ou cle inconnus : NULL");
    const catalog_type_t *st = catalog_type(shutter), *gt = catalog_type(gate);
    CHECK(st && st->slot[CAT_CMD_UP] == CAT_CMD_UP && st->slot[CAT_CMD_STOP] == CAT_CMD_STOP
          && st->slot[CAT_CMD_DOWN] == CAT_CMD_DOWN && st->slot[CAT_CMD_PROG] == CATALOG_NO_SLOT,
          "C24 volet : chaque commande a son bouton ; prog n'est porte par aucun bouton");
    CHECK(gt && gt->slot[CAT_CMD_UP] == CAT_CMD_UP && gt->slot[CAT_CMD_STOP] == CAT_CMD_UP
          && gt->slot[CAT_CMD_DOWN] == CAT_CMD_UP && gt->slot[CAT_CMD_PROG] == CATALOG_NO_SLOT,
          "C25 portail : son bouton pas a pas porte les commandes de son cycle, sur une seule trame");
    bool names = catalog_cmd_name(CAT_CMD_COUNT) == NULL;
    for (int c = 0; c < CAT_CMD_COUNT; c++) names = names && catalog_cmd_of(catalog_cmd_name((catalog_cmd_t)c)) == c;
    CHECK(names, "C26 nom d'une commande : celui que catalog_cmd_of relit");
    CHECK(gt && gt->step.len == 4 && gt->step.act[0] == CAT_CMD_UP && gt->step.act[2] == CAT_CMD_DOWN && gt->step.press_gap_ms > 0
          && st && st->step.len == 0, "C27 le portail a un cycle pas a pas et son delai ; le volet n'en a pas");
    const catalog_profile_t *eveno = catalog_profile(EVENO_PROFILE), *base = catalog_profile(PROFALUX_ENROL_PROFILE);
    CHECK(eveno && base && eveno->freq_khz != base->freq_khz && eveno->te_us == base->te_us
          && eveno->repeat_bit == base->repeat_bit && eveno->rmt == base->rmt && eveno->encoder == CAT_ENC_KEELOQ
          && eveno->enrol && !strcmp(eveno->type, base->type),
          "C18 heritage : Eveno change sa porteuse et garde le reste de sa base");
    CHECK(catalog_profile_infer(CAT_ENC_KEELOQ, true, EVENO_BRAND) == eveno, "C19 fiche ancienne : la marque designe le profil");
    CHECK(catalog_profile_infer(CAT_ENC_KEELOQ, true, "") == base && catalog_profile_infer(CAT_ENC_KEELOQ, true, "Marque inconnue") == base,
          "C20 fiche ancienne sans marque connue : le premier profil de l'encodeur et de la methode");
    CHECK(catalog_profile_infer(CAT_ENC_KEELOQ, false, "") && !catalog_profile_infer(CAT_ENC_KEELOQ, false, "")->enrol,
          "C21 copie et enrolement ne se confondent pas");
    CHECK(catalog_profile_infer(CAT_ENC_SOMFY_RTS, false, "") == NULL, "C22 aucun profil de cet encodeur et de cette methode : NULL");
    CHECK(catalog_profile_infer(CAT_ENC_COUNT, true, "") == NULL && catalog_profile_infer(CAT_ENC_COUNT, true, EVENO_BRAND) == NULL,
          "C23 protocole inconnu de ce firmware (io d'une autre branche) : aucun profil, meme avec une marque");

    printf("Un catalogue faux est refuse en entier :\n");
    refused("{", "JSON illisible", "R1 JSON illisible");
    refused(NULL, "JSON illisible", "R2 pas de texte");
    refused("{\"protocols\":{\"x\":{\"encoder\":\"nice\",\"press\":{\"frames\":1}}}}", "encodeur inconnu", "R3 encodeur inconnu de ce firmware");
    refused("{\"protocols\":{\"a\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1}},\"b\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1}}}}",
            "deux protocoles", "R4 deux protocoles pour le meme encodeur");
    refused("{\"protocols\":{\"a\":{\"encoder\":\"keeloq\",\"commands\":{\"up\":{\"code\":16}},\"press\":{\"frames\":1}}}}", "code de commande", "R5 code hors 0..15");
    refused("{\"protocols\":{\"a\":{\"encoder\":\"keeloq\",\"commands\":{\"up\":{\"code\":2}},\"press\":{}}}}", "press.frames", "R6 press.frames absent");
    refused("{\"protocols\":{\"a\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1.5}}}}", "press.frames", "R7 press.frames pas entier");
    refused("{\"protocols\":{\"a\":{\"encoder\":\"somfy-rts\",\"press\":{\"frames\":2},\"enrol\":{\"command\":\"prog\"}}}}",
            "enrol.command", "R8 enrol.command sans code dans commands");
    refused("{\"protocols\":{\"a\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1},\"enrol\":{\"code\":5}}}}",
            "enrol : code", "R9 enrolement KeeLoq sans ses salves");
    refused("{\"protocols\":[]}", "objet protocols", "R10 protocols n'est pas un objet");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"legacy\":{\"code\":0}},\"b\":{\"legacy\":{\"code\":0}}}}", "meme numero legacy", "R12 deux types avec le meme numero legacy");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"legacy\":{\"code\":300}}}}", "legacy.code hors", "R15 numero legacy hors 0..255");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"buttons\":[{\"id\":\"x\",\"command\":\"ouvre\"}]}}}", "bouton de commande inconnue", "R16 bouton d'une commande inconnue");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"buttons\":[{\"id\":\"x\",\"command\":\"step\",\"cycle\":[\"up\",\"ouvre\"]}]}}}", "cycle : commande inconnue", "R17 cycle d'une commande inconnue");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"buttons\":[{\"id\":\"x\",\"command\":\"step\",\"cycle\":[\"up\",\"stop\",\"down\",\"stop\",\"up\"],\"press_gap_ms\":1000}]}}}", "cycle trop long", "R18 cycle plus long que le lecteur");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"buttons\":[{\"id\":\"x\",\"command\":\"step\",\"cycle\":[\"up\",\"down\"]}]}}}", "sans cycle ou sans press_gap_ms", "R19 bouton pas a pas sans delai");
    refused("{\"protocols\":{},\"types\":{\"a\":{\"buttons\":[{\"id\":\"x\",\"command\":\"step\",\"cycle\":[\"up\",\"down\"],\"press_gap_ms\":1000},{\"id\":\"y\",\"command\":\"step\",\"cycle\":[\"up\",\"down\"],\"press_gap_ms\":1000}]}}}", "deux boutons pas a pas", "R20 deux boutons pas a pas");
    refused("{\"protocols\":{\"p\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1}}},\"types\":{\"v\":{}},"
            "\"profiles\":[{\"id\":\"b\",\"inherits\":\"a\"},{\"id\":\"a\",\"protocol\":\"p\",\"types\":[\"v\"],\"radio\":{\"freq_khz\":868000}}]}",
            "base absente", "R13 base declaree apres ce qui en herite");
    refused("{\"protocols\":{\"p\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1}}},\"types\":{\"v\":{}},"
            "\"profiles\":[{\"id\":\"a\",\"protocol\":\"p\",\"types\":[\"v\"]}]}",
            "sans protocole, type ou porteuse", "R14 profil sans porteuse");
    /* un modele de trop : CATALOG_MODELS_MAX + 1 noms */
    char many[4096]; int n = snprintf(many, sizeof many, "{\"protocols\":{\"a\":{\"encoder\":\"keeloq\",\"press\":{\"frames\":1}}},\"types\":{\"v\":{}},"
                               "\"profiles\":[{\"id\":\"p\",\"protocol\":\"a\",\"types\":[\"v\"],\"radio\":{\"freq_khz\":868000},\"models\":[");
    for (int i = 0; i <= CATALOG_MODELS_MAX; i++) n += snprintf(many + n, sizeof many - (size_t)n, "%s\"m%d\"", i ? "," : "", i);
    snprintf(many + n, sizeof many - (size_t)n, "]}]}");
    refused(many, "trop de modeles", "R11 trop de modeles");

    printf("Un bon catalogue relu apres un faux :\n");
    CHECK(catalog_load(real, err, sizeof err) == 0 && catalog_code(CAT_ENC_KEELOQ, CAT_CMD_UP) == 0x2, "C15 relu entier");

    free(real);
    printf(s_fail ? "ECHEC\n" : "OK\n");
    return s_fail;
}
