/* Config persistante des volets : serialisation JSON et rangement. Voir cfg_model.h. */
#include "cfg_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __has_include(<bsd/string.h>)
#include <bsd/string.h>   /* strlcpy sur PC (banc de test) ; newlib la fournit sur l'ESP32 */
#endif
#include "esp_log.h"

static const char *TAG = "cfg_model";

/* ── Modele -> JSON ── */
static cJSON *volet_to_json(const volet_t *v) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", v->id);
    cJSON *sr = cJSON_AddArrayToObject(o, "serials");
    for (int j = 0; j < v->n_serials; j++) cJSON_AddItemToArray(sr, cJSON_CreateString(v->serials[j]));
    cJSON *cmd = cJSON_AddObjectToObject(o, "cmd");
    cJSON_AddStringToObject(cmd, "up", v->up);
    cJSON_AddStringToObject(cmd, "down", v->down);
    cJSON_AddStringToObject(cmd, "stop", v->stop);
    cJSON_AddNumberToObject(o, "up_btn", v->up_btn);
    cJSON_AddNumberToObject(o, "down_btn", v->down_btn);
    cJSON_AddNumberToObject(o, "stop_btn", v->stop_btn);
    cJSON_AddNumberToObject(o, "travel_up_ms", v->travel_up_ms);
    cJSON_AddNumberToObject(o, "travel_down_ms", v->travel_down_ms);
    cJSON_AddNumberToObject(o, "orientation", v->orientation);
    cJSON_AddNumberToObject(o, "order", v->order);   /* rang d'affichage UI (-1 = non range) */
    cJSON_AddNumberToObject(o, "position", (int)(v->position + 0.5f));
    if (v->virt) {   /* volet virtuel : identite 0x067 + compteur roulant persiste */
        cJSON_AddBoolToObject(o, "virt", true);
        cJSON_AddNumberToObject(o, "virt_serial", v->virt_serial);
        cJSON_AddNumberToObject(o, "virt_counter", v->virt_counter);
        cJSON_AddNumberToObject(o, "virt_te", v->virt_te);
    }
    if (v->central) {   /* centrale : liste des volets membres (CSV) */
        cJSON_AddBoolToObject(o, "central", true);
        cJSON_AddStringToObject(o, "members", v->members);
    }
    return o;
}
/* En-tete : tout ce qui n'est pas un volet. */
static cJSON *hdr_to_json(const cfg_model_t *m) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "log_frames", *m->log_frames);   /* ecoute permanente : restauree au boot */
    cJSON *rem = cJSON_AddObjectToObject(root, "remotes");
    for (int i = 0; i < *m->nremotes; i++) cJSON_AddStringToObject(rem, m->remotes[i].serial, m->remotes[i].name);
    return root;
}
char *cfg_model_export(const cfg_model_t *m) {
    cJSON *root = hdr_to_json(m);
    cJSON *vols = cJSON_AddArrayToObject(root, "volets");
    for (int i = 0; i < *m->nvolets; i++) cJSON_AddItemToArray(vols, volet_to_json(&m->volets[i]));
    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return js;
}
static char *print_and_free(cJSON *j) {   /* NULL si plus de memoire : cfg_store le compte en echec */
    char *s = j ? cJSON_PrintUnformatted(j) : NULL;
    cJSON_Delete(j);
    return s;
}
/* Un document a la fois : la config n'est jamais tenue entiere en RAM, ni
 * reecrite en bloc. Un volet inchange ne coute rien, la NVS ignore une valeur
 * identique. */
esp_err_t cfg_model_save(const cfg_model_t *m, char *failed_key) {
    cfg_store_tx_t tx;
    if (cfg_store_begin(&tx) == ESP_OK) {
        char *d = print_and_free(hdr_to_json(m));
        cfg_store_put_hdr(&tx, d); free(d);
        for (int i = 0; i < *m->nvolets; i++) {
            d = print_and_free(volet_to_json(&m->volets[i]));
            cfg_store_put_volet(&tx, d); free(d);
        }
    }
    esp_err_t e = cfg_store_end(&tx);
    if (failed_key) memcpy(failed_key, tx.failed_key, sizeof(tx.failed_key));
    return e;
}

/* ── JSON -> modele ── */
/* Peuple les telecommandes depuis l'en-tete (le modele doit etre remis a zero avant). */
static void parse_hdr_json(const cfg_model_t *m, cJSON *root) {
    cJSON *lf = cJSON_GetObjectItem(root, "log_frames");
    if (cJSON_IsBool(lf)) *m->log_frames = cJSON_IsTrue(lf);   /* restaure l'ecoute permanente au boot */
    cJSON *rem = cJSON_GetObjectItem(root, "remotes");
    /* Objet attendu : dans un tableau, it->string est NULL et strlcpy plantait
     * (redemarrage provoque par une restauration malformee). */
    if (!cJSON_IsObject(rem)) return;
    for (cJSON *it = rem->child; it && *m->nremotes < SH_MAX_REMOTES; it = it->next) {
        if (!it->string) continue;
        remote_t *r = &m->remotes[(*m->nremotes)++];
        memset(r, 0, sizeof(*r));
        strlcpy(r->serial, it->string, SH_SERIAL_LEN);
        strlcpy(r->name, cJSON_IsString(it) ? it->valuestring : "", SH_ID_LEN);
    }
}
/* Ajoute UN volet depuis son document JSON. */
static void parse_volet_json(const cfg_model_t *m, cJSON *o) {
    if (*m->nvolets >= SH_MAX_VOLETS) return;
    char id[SH_ID_LEN];
    strlcpy(id, cJSON_GetStringValue(cJSON_GetObjectItem(o, "id")) ?: "", sizeof(id));
    /* Identifiant deja charge : ignore. Ne se produit pas en temps normal, mais une
     * coupure au milieu de la suppression d'un volet (les cles suivantes sont
     * renumerotees) peut laisser deux fois le meme document. */
    for (int i = 0; i < *m->nvolets; i++)
        if (!strcmp(m->volets[i].id, id)) { ESP_LOGW(TAG, "volet '%s' en double, ignore", id); return; }
    volet_t *v = &m->volets[(*m->nvolets)++];
    memset(v, 0, sizeof(*v)); v->target = -1; v->pub_pos = -1; v->pub_dir = -9;
    strlcpy(v->id, id, SH_ID_LEN);
    cJSON *sr = cJSON_GetObjectItem(o, "serials");
    for (cJSON *s = sr ? sr->child : NULL; s && v->n_serials < SH_MAX_SERIALS; s = s->next)
        strlcpy(v->serials[v->n_serials++], cJSON_GetStringValue(s) ?: "", SH_SERIAL_LEN);
    cJSON *cmd = cJSON_GetObjectItem(o, "cmd");
    if (cmd) {
        strlcpy(v->up,   cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "up"))   ?: "", SH_BITS_LEN);
        strlcpy(v->down, cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "down")) ?: "", SH_BITS_LEN);
        strlcpy(v->stop, cJSON_GetStringValue(cJSON_GetObjectItem(cmd, "stop")) ?: "", SH_BITS_LEN);
    }
    v->up_btn   = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "up_btn"));
    v->down_btn = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "down_btn"));
    v->stop_btn = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "stop_btn"));
    v->travel_up_ms   = cJSON_GetNumberValue(cJSON_GetObjectItem(o, "travel_up_ms"));
    v->travel_down_ms = cJSON_GetNumberValue(cJSON_GetObjectItem(o, "travel_down_ms"));
    cJSON *ori = cJSON_GetObjectItem(o, "orientation");
    v->orientation    = ori ? (int)cJSON_GetNumberValue(ori) : -1;   /* -1 = non defini */
    /* Champ absent (config anterieure) -> -1 : le volet garde sa place actuelle. */
    cJSON *ord = cJSON_GetObjectItem(o, "order");
    v->order          = ord ? (int)cJSON_GetNumberValue(ord) : -1;
    v->position       = cJSON_GetNumberValue(cJSON_GetObjectItem(o, "position"));
    cJSON *vt = cJSON_GetObjectItem(o, "virt");
    if (cJSON_IsTrue(vt)) {
        v->virt         = true;
        v->virt_serial  = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "virt_serial"));
        v->virt_counter = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "virt_counter"));
        v->virt_te      = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(o, "virt_te"));
    }
    if (cJSON_IsTrue(cJSON_GetObjectItem(o, "central"))) {
        v->central = true;
        strlcpy(v->members, cJSON_GetStringValue(cJSON_GetObjectItem(o, "members")) ?: "", SH_MEMBERS_LEN);
    }
}
void cfg_model_import(const cfg_model_t *m, cJSON *root) {
    parse_hdr_json(m, root);
    cJSON *vols = cJSON_GetObjectItem(root, "volets");
    for (cJSON *o = vols ? vols->child : NULL; o; o = o->next) parse_volet_json(m, o);
}

/* ── NVS -> modele ── */
static void on_cfg_doc(void *ctx, int index, const char *doc) {
    const cfg_model_t *m = ctx;
    cJSON *j = cJSON_Parse(doc);
    if (!j) { ESP_LOGE(TAG, "document %d illisible", index); return; }
    if (index == CFG_DOC_LEGACY)   cfg_model_import(m, j);
    else if (index == CFG_DOC_HDR) parse_hdr_json(m, j);
    else                           parse_volet_json(m, j);
    cJSON_Delete(j);
}
/* L'ancienne config (cle NVS unique) contient-elle au moins un volet ? */
static bool legacy_has_volets(const char *doc) {
    cJSON *j = cJSON_Parse(doc);
    bool yes = cJSON_GetArraySize(cJSON_GetObjectItem(j, "volets")) > 0;
    cJSON_Delete(j);
    return yes;
}
cfg_layout_t cfg_model_load(const cfg_model_t *m) {
    return cfg_store_load(on_cfg_doc, (void *)m, legacy_has_volets);
}

/* ── Dataset de trames ── */
bool cfg_frames_load(const char *path, void *buf, size_t cap, size_t *sz) {
    *sz = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        *sz = fread(buf, 1, cap, f);
        fclose(f);
        return true;
    }
    size_t n = cfg_store_read_legacy_frames(buf, cap);
    if (!n) return false;
    *sz = n;
    FILE *w = fopen(path, "wb");
    bool ok = w && fwrite(buf, 1, n, w) == n;
    if (w && fclose(w) != 0) ok = false;
    if (ok) {
        ESP_LOGI(TAG, "dataset de trames : NVS -> fichier (%u o)", (unsigned)n);
    } else {
        /* Pas de fichier partiel : au demarrage suivant il passerait pour le
         * dataset complet, et la copie NVS serait effacee a tort. */
        if (w) remove(path);
        ESP_LOGE(TAG, "dataset de trames : ecriture du fichier KO, il reste en NVS");
    }
    return ok;
}
