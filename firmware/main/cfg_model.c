/* Config persistante des volets : serialisation JSON et rangement. Voir cfg_model.h. */
#include "cfg_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __has_include(<bsd/string.h>)
#include <bsd/string.h>   /* strlcpy sur PC (banc de test) ; newlib la fournit sur l'ESP32 */
#endif
#include <dirent.h>
#include <inttypes.h>
#include "esp_log.h"
#include "cover_attrs.h"   /* slug et identifiant stable d'un appareil (unite pure) */

static const char *TAG = "cfg_model";

/* ── Captures de rejeu en fichier ──
 * Le document NVS d'un appareil ne porte plus que le nom du fichier ("rejeu_file"). L'export (sauvegarde,
 * restauration) garde la capture en clair : son format ne change pas. Le fichier est ecrit AVANT le
 * document qui le nomme, et un fichier qu'aucun document ne nomme n'est efface qu'apres un
 * enregistrement reussi : une coupure ne laisse jamais un document sans sa capture. */
/* Cle du document NVS qui nomme le fichier de la capture (donnee du format, pas un identifiant). */
#define DOC_KEY_REPLAY_FILE "rejeu_file"
#define REPLAY_FILE_PREFIX "rj"
#define REPLAY_FILE_EXT    ".json"
#define REPLAY_FILE_TMP    ".tmp"              /* suffixe du temporaire de cfg_write_file */
#define REPLAY_FILE_HEX    8
#define REPLAY_FILE_MAX    (64 * 1024)         /* au-dela, un fichier n'est pas une capture de ce firmware */
#define REPLAY_PATH_MAX    96

/* FNV-1a 32 bits. Jamais 0 : 0 veut dire « aucun contenu connu sur disque ». */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1u;
}
/* Un nom que ce firmware a pu ecrire : seul nom lu, et seul nom efface (un document retouche a la main
 * ne fait pas lire ni effacer autre chose). */
static bool replay_name_ok(const char *n) {
    const size_t lp = strlen(REPLAY_FILE_PREFIX);
    if (strlen(n) != lp + REPLAY_FILE_HEX + strlen(REPLAY_FILE_EXT) || strncmp(n, REPLAY_FILE_PREFIX, lp)) return false;
    for (size_t i = 0; i < REPLAY_FILE_HEX; i++) {
        const char c = n[lp + i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return !strcmp(n + lp + REPLAY_FILE_HEX, REPLAY_FILE_EXT);
}
static bool replay_path(char *out, const char *dir, const char *name) {
    return snprintf(out, REPLAY_PATH_MAX, "%s/%s", dir, name) < REPLAY_PATH_MAX;
}
/* Nom deja pris par un autre appareil dont la capture est en fichier ? */
static bool replay_name_taken(const cfg_model_t *m, int self, const char *name) {
    for (int k = 0; k < *m->nvolets; k++)
        if (k != self && !strcmp(m->volets[k].replay_file, name)) return true;   /* relue ou non */
    return false;
}
/* Range la capture de l'appareil i dans son fichier. true : elle y est, et v->replay_file la nomme ;
 * false : elle reste dans le document NVS (pas de dossier, ou fichier impossible a ecrire). */
static bool replay_store_file(const cfg_model_t *m, int i) {
    volet_t *v = &m->volets[i];
    if (!m->capture_dir || !v->rejeu) return false;
    if (!v->replay_file[0] || replay_name_taken(m, i, v->replay_file)) {
        /* un nom tire de l'identifiant stable ; un autre appareil le porte deja : le suivant */
        uint32_t h = fnv1a(v->uid[0] ? v->uid : v->id);
        for (int t = 0; t < SH_MAX_VOLETS; t++, h++) {
            snprintf(v->replay_file, sizeof v->replay_file, REPLAY_FILE_PREFIX "%08" PRIx32 REPLAY_FILE_EXT, h);
            if (!replay_name_taken(m, i, v->replay_file)) break;
        }
        v->replay_hash = 0;   /* nouveau nom : rien encore sur disque */
    }
    const uint32_t h = fnv1a(v->rejeu);
    if (v->replay_hash == h) return true;
    char path[REPLAY_PATH_MAX];
    if (!replay_path(path, m->capture_dir, v->replay_file) || !cfg_write_file(path, v->rejeu, strlen(v->rejeu))) {
        ESP_LOGW(TAG, "'%s' : capture non ecrite en fichier, elle reste dans la NVS", v->id);
        v->replay_file[0] = 0;   /* le document la porte en clair : il ne nomme plus de fichier */
        v->replay_hash = 0;
        return false;
    }
    v->replay_hash = h;
    return true;
}
/* Apres un enregistrement reussi : efface les fichiers de capture qu'aucun appareil ne nomme plus
 * (appareil supprime, capture retiree). Un temporaire suit le sort de son fichier. */
static void replay_sweep(const cfg_model_t *m) {
    if (!m->capture_dir) return;
    DIR *d = opendir(m->capture_dir);
    if (!d) return;
    /* On efface apres closedir : retirer une entree pendant le parcours n'est pas garanti sur SPIFFS. */
    char gone[2 * SH_MAX_VOLETS][CFG_REPLAY_NAME_LEN + sizeof(REPLAY_FILE_TMP)];
    int ngone = 0;
    struct dirent *e;
    while ((e = readdir(d)) && ngone < (int)(sizeof gone / sizeof gone[0])) {
        char name[sizeof gone[0]];
        if (strlen(e->d_name) >= sizeof name) continue;
        strcpy(name, e->d_name);
        size_t n = strlen(name), lt = strlen(REPLAY_FILE_TMP);
        if (n > lt && !strcmp(name + n - lt, REPLAY_FILE_TMP)) name[n - lt] = 0;
        if (!replay_name_ok(name)) continue;
        bool used = false;
        /* une fiche dont la capture n'a pas ete relue nomme encore son fichier : il reste */
        for (int k = 0; k < *m->nvolets && !used; k++) used = !strcmp(m->volets[k].replay_file, name);
        if (!used) strcpy(gone[ngone++], e->d_name);
    }
    closedir(d);
    for (int i = 0; i < ngone; i++) {
        char path[REPLAY_PATH_MAX];
        if (replay_path(path, m->capture_dir, gone[i]) && remove(path) == 0)
            ESP_LOGI(TAG, "capture %s : plus aucun appareil ne la nomme, effacee", gone[i]);
    }
}
/* Relit la capture que nomme un document NVS. */
static void replay_load_file(const cfg_model_t *m, volet_t *v, const char *name) {
    char path[REPLAY_PATH_MAX];
    size_t n = 0;
    char *buf = (m->capture_dir && replay_name_ok(name) && replay_path(path, m->capture_dir, name))
              ? cfg_read_file(path, REPLAY_FILE_MAX, &n) : NULL;
    /* Meme non relue (RAM, fichier abime), la fiche garde le nom : le document qui l'enregistre de
     * nouveau le nomme encore, et le menage n'efface pas le fichier. La capture revient au demarrage
     * suivant si c'etait la RAM. */
    if (replay_name_ok(name)) strlcpy(v->replay_file, name, sizeof v->replay_file);
    if (!buf) { ESP_LOGE(TAG, "'%s' : capture %s introuvable", v->id, name); return; }
    v->rejeu = buf;
    v->replay_hash = fnv1a(buf);
}

/* ── Membres d'une centrale ── */
int cfg_volet_set_members(volet_t *v, const char *csv) {
    size_t n = csv ? strlen(csv) : 0;
    if (n >= SH_MEMBERS_LEN) return -1;
    char *copy = NULL;
    if (n && !(copy = strdup(csv))) return -1;
    free(v->members);
    v->members = copy;
    return 0;
}
void cfg_volet_release(volet_t *v) {
    free(v->members);
    v->members = NULL;
    free(v->rejeu);
    v->rejeu = NULL;
}
void cfg_model_remove(const cfg_model_t *m, int idx) {
    int n = *m->nvolets;
    if (idx < 0 || idx >= n) return;
    cfg_volet_release(&m->volets[idx]);
    memmove(&m->volets[idx], &m->volets[idx + 1], (size_t)(n - idx - 1) * sizeof(volet_t));
    memset(&m->volets[n - 1], 0, sizeof(volet_t));   /* la place liberee ne partage plus de liste */
    (*m->nvolets)--;
}
/* Meme decoupage que l'ancien parcours (copie + strtok_r + espaces de tete),
 * sans tampon : un tampon fixe etait ce qui tronquait la liste. */
const char *cfg_members_next(const char *p, char id[SH_ID_LEN]) {
    if (!p) return NULL;
    while (*p == ',') p++;
    if (!*p) return NULL;
    while (*p == ' ') p++;
    size_t n = strcspn(p, ",");
    if (n < SH_ID_LEN) { memcpy(id, p, n); id[n] = 0; }
    else id[0] = 0;
    return p + n;
}

/* ── Identifiant stable ── */
typedef struct { const cfg_model_t *m; int upto, skip; } uid_scope_t;
/* uid deja porte par un des `upto` premiers volets, hors le volet `skip` (celui qu'on sert) ? */
static bool uid_taken(const char *uid, void *ctx) {
    const uid_scope_t *s = ctx;
    for (int i = 0; i < s->upto; i++) if (i != s->skip && !strcmp(s->m->volets[i].uid, uid)) return true;
    return false;
}
int cfg_model_ensure_uids(const cfg_model_t *m) {
    int n = *m->nvolets, served = 0;
    /* d'abord les identifiants deja choisis : un doublon ou une valeur invalide (sauvegarde retouchee
     * a la main, appareil recopie) perd le sien, le premier garde le sien */
    for (int i = 0; i < n; i++) {
        uid_scope_t before = { m, i, -1 };
        if (m->volets[i].uid[0] && (!cover_uid_ok(m->volets[i].uid) || uid_taken(m->volets[i].uid, &before))) m->volets[i].uid[0] = 0;
    }
    /* puis ceux qui n'en ont pas : leur slug, s'il est libre parmi TOUS les autres */
    for (int i = 0; i < n; i++) {
        if (m->volets[i].uid[0]) continue;
        uid_scope_t others = { m, n, i };
        cover_uid_pick(m->volets[i].uid, sizeof(m->volets[i].uid), m->volets[i].id, uid_taken, &others);
        served++;
    }
    return served;
}

/* ── Modele -> JSON ── */
/* Le type qu'une fiche a quand elle n'en ecrit pas : celui de son profil, sinon celui des fiches d'avant
 * les types par cle (mode legacy, numero 0). NULL si le catalogue ne dit rien. */
static const char *default_type(const volet_t *v) {
    const catalog_profile_t *p = catalog_profile(v->profile);
    return p ? p->type : catalog_type_of_legacy(0);
}

/* replay_file : la capture est dans ce fichier, le document ne porte que son nom (NVS) ; NULL : en clair. */
static cJSON *volet_to_json(const volet_t *v, const char *replay_file) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", v->id);
    /* Identifiant stable : omis tant qu'il vaut le slug du nom (jamais renomme), pour garder les
     * sauvegardes existantes a l'identique ; au chargement, un volet sans ce champ le retrouve tel quel. */
    char slug[SH_ID_LEN];
    cover_slug(slug, sizeof(slug), v->id);
    if (v->uid[0] && strcmp(v->uid, slug)) cJSON_AddStringToObject(o, "uid", v->uid);
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
        /* Protocole : omis quand il vaut Profalux (0), pour garder le JSON et les
         * sauvegardes existantes a l'identique. */
        if (v->proto != VOLET_PROTO_PROFALUX)
            cJSON_AddNumberToObject(o, "proto", v->proto);
    }
    /* Le profil, quand on le connait. Le type : la cle du catalogue, jamais son numero (mode legacy :
     * lu, jamais ecrit), et seulement s'il differe du type par defaut (default_type) : la NVS d'une
     * carte n'a que 16 Ko, et une fiche ordinaire n'a pas a grossir pour redire son profil. */
    if (v->profile[0]) cJSON_AddStringToObject(o, "profile", v->profile);
    const char *def = default_type(v);
    if (!def || strcmp(v->type, def)) cJSON_AddStringToObject(o, "type", v->type);
    if (v->central) cJSON_AddBoolToObject(o, "central", true);
    /* Centrale : volets membres. Zone : volets suiveurs. Toujours ecrit pour une
     * centrale (meme vide, comme avant) ; pour un volet, seulement s'il en a. */
    if (v->central || v->members) cJSON_AddStringToObject(o, "members", v->members ? v->members : "");
    if (v->rejeu && replay_file) cJSON_AddStringToObject(o, DOC_KEY_REPLAY_FILE, replay_file);   /* capture en fichier */
    else if (v->rejeu) cJSON_AddStringToObject(o, "rejeu", v->rejeu);   /* rejeu brut par commande */
    else if (replay_file) cJSON_AddStringToObject(o, DOC_KEY_REPLAY_FILE, replay_file);   /* capture non relue : son nom reste */
    /* Parametres d'emission par appareil (etage 2, issue #111). Omis quand ils valent le defaut
     * (0 = heriter), pour garder les sauvegardes existantes a l'identique et rester compatible dans
     * les deux sens : une version anterieure ignore ces cles, celle-ci les retrouve a 0 si absentes. */
    if (v->freq_khz) cJSON_AddNumberToObject(o, "freq_khz", v->freq_khz);
    if (v->te_us)    cJSON_AddNumberToObject(o, "te_us", v->te_us);
    if (v->tx_rpt)   cJSON_AddNumberToObject(o, "tx_rpt", v->tx_rpt);
    if (v->tx_rmt)   cJSON_AddNumberToObject(o, "tx_rmt", v->tx_rmt);
    if (v->frame_bits) cJSON_AddNumberToObject(o, "frame_bits", v->frame_bits);
    if (v->marque[0]) cJSON_AddStringToObject(o, "marque", v->marque);
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
    for (int i = 0; i < *m->nvolets; i++) cJSON_AddItemToArray(vols, volet_to_json(&m->volets[i], NULL));
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
    cfg_model_ensure_uids(m);   /* un volet tout juste cree recoit le sien avant d'etre ecrit */
    cfg_store_tx_t tx;
    if (cfg_store_begin(&tx) == ESP_OK) {
        char *d = print_and_free(hdr_to_json(m));
        cfg_store_put_hdr(&tx, d); free(d);
        for (int i = 0; i < *m->nvolets; i++) {
            const volet_t *v = &m->volets[i];
            const bool in_file = replay_store_file(m, i);   /* le fichier AVANT le document qui le nomme */
            const bool unread = m->capture_dir && !v->rejeu && v->replay_file[0];   /* capture non relue */
            d = print_and_free(volet_to_json(v, in_file || unread ? v->replay_file : NULL));
            cfg_store_put_volet(&tx, d); free(d);
        }
    }
    esp_err_t e = cfg_store_end(&tx);
    if (e == ESP_OK) replay_sweep(m);
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
    /* identifiant stable : absent = jamais renomme, cfg_model_ensure_uids lui rendra son slug */
    strlcpy(v->uid, cJSON_GetStringValue(cJSON_GetObjectItem(o, "uid")) ?: "", SH_ID_LEN);
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
        cJSON *pr       = cJSON_GetObjectItem(o, "proto");   /* absent -> Profalux (0) */
        v->proto        = pr ? (uint8_t)cJSON_GetNumberValue(pr) : (uint8_t)VOLET_PROTO_PROFALUX;
    }
    /* Type d'appareil : une cle du catalogue. Une fiche v0.2.16 porte un numero, ou rien pour un volet
     * (mode legacy, traduit par le catalogue). Une cle inconnue est gardee telle quelle : elle vient
     * peut-etre d'une version plus recente, et l'effacer perdrait l'information. */
    strlcpy(v->profile, cJSON_GetStringValue(cJSON_GetObjectItem(o, "profile")) ?: "", sizeof v->profile);
    cJSON *ty = cJSON_GetObjectItem(o, "type");
    const char *key = cJSON_IsString(ty) ? cJSON_GetStringValue(ty)
                    : cJSON_IsNumber(ty) ? catalog_type_of_legacy((int)cJSON_GetNumberValue(ty))
                    : default_type(v);   /* absent : le type par defaut (celui du profil, sinon du numero 0) */
    strlcpy(v->type, key ? key : "", sizeof v->type);
    v->central = cJSON_IsTrue(cJSON_GetObjectItem(o, "central"));
    /* Membres d'une centrale, ou suiveurs d'une zone. Une liste ecrite par le
     * firmware tient toujours : seul un fichier modifie a la main peut depasser
     * SH_MEMBERS_LEN. Refusee entiere plutot que tronquee, et signalee. */
    const char *mem = cJSON_GetStringValue(cJSON_GetObjectItem(o, "members")) ?: "";
    if (cfg_volet_set_members(v, mem) != 0)
        ESP_LOGE(TAG, "%s '%s' : liste de membres de %u o refusee (trop longue ou plus de memoire)",
                 v->central ? "centrale" : "zone", v->id, (unsigned)strlen(mem));
    const char *rj = cJSON_GetStringValue(cJSON_GetObjectItem(o, "rejeu"));
    const char *rf = cJSON_GetStringValue(cJSON_GetObjectItem(o, DOC_KEY_REPLAY_FILE));
    v->rejeu = (rj && *rj) ? strdup(rj) : NULL;   /* rejeu brut par commande ; libere par cfg_volet_release */
    if (!v->rejeu && rf && *rf) replay_load_file(m, v, rf);   /* document NVS : la capture est en fichier */
    /* Parametres d'emission par appareil (etage 2, issue #111). memset() a deja mis 0 = heriter : on
     * ne surcharge que si la cle est presente. Compatible avec une config anterieure (cles absentes). */
    cJSON *fk = cJSON_GetObjectItem(o, "freq_khz"); if (fk) v->freq_khz = (uint32_t)cJSON_GetNumberValue(fk);
    cJSON *tu = cJSON_GetObjectItem(o, "te_us");    if (tu) v->te_us    = (uint16_t)cJSON_GetNumberValue(tu);
    cJSON *xp = cJSON_GetObjectItem(o, "tx_rpt");   if (xp) v->tx_rpt   = (uint8_t)cJSON_GetNumberValue(xp);
    cJSON *xm = cJSON_GetObjectItem(o, "tx_rmt");   if (xm) v->tx_rmt   = (uint8_t)cJSON_GetNumberValue(xm);
    cJSON *fb = cJSON_GetObjectItem(o, "frame_bits"); if (fb) v->frame_bits = (uint8_t)cJSON_GetNumberValue(fb);
    strlcpy(v->marque, cJSON_GetStringValue(cJSON_GetObjectItem(o, "marque")) ?: "", SH_ID_LEN);
}
void cfg_model_import(const cfg_model_t *m, cJSON *root) {
    parse_hdr_json(m, root);
    cJSON *vols = cJSON_GetObjectItem(root, "volets");
    for (cJSON *o = vols ? vols->child : NULL; o; o = o->next) parse_volet_json(m, o);
    cfg_model_ensure_uids(m);
}
bool cfg_model_is_backup(const cJSON *root) {
    if (!cJSON_IsObject(root)) return false;
    const cJSON *rem = cJSON_GetObjectItemCaseSensitive(root, "remotes");
    return cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "volets")) && (!rem || cJSON_IsObject(rem));
}
bool cfg_model_save_copy(const cfg_model_t *m, const char *path) {
    char *js = cfg_model_export(m);
    bool ok = js && cfg_write_file(path, js, strlen(js));
    free(js);
    return ok;
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
    cfg_layout_t lay = cfg_store_load(on_cfg_doc, (void *)m, legacy_has_volets);
    cfg_model_ensure_uids(m);   /* un document par volet : seulement une fois tous lus */
    return lay;
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
    /* Ecrit directement sous son nom final, un fichier coupe en cours d'ecriture
     * (panne de courant) passait au demarrage suivant pour le dataset complet, et
     * sa copie NVS, la seule entiere, etait effacee : d'ou cfg_write_file. */
    bool ok = cfg_write_file(path, buf, n);
    if (ok) ESP_LOGI(TAG, "dataset de trames : NVS -> fichier (%u o)", (unsigned)n);
    else    ESP_LOGE(TAG, "dataset de trames : ecriture du fichier KO, il reste en NVS");
    return ok;
}

/* Le temporaire est complet avant de toucher a l'ancien fichier. SPIFFS refuse de
 * renommer vers un fichier existant (SPIFFS_ERR_CONFLICTING_NAME) : l'ancien est
 * alors efface juste avant. Une coupure entre les deux laisse le temporaire complet,
 * que cfg_read_file reprend ; un temporaire incomplet est reecrit la fois suivante. */
bool cfg_write_file(const char *path, const void *buf, size_t n) {
    char tmp[96];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE *w = fopen(tmp, "wb");
    bool ok = w && fwrite(buf, 1, n, w) == n;
    if (w && fclose(w) != 0) ok = false;
    if (!ok) { if (w) remove(tmp); return false; }
    if (rename(tmp, path) == 0) return true;
    if (remove(path) != 0) { remove(tmp); return false; }   /* l'ancien reste */
    return rename(tmp, path) == 0;
}
char *cfg_read_file(const char *path, size_t cap, size_t *n) {
    char tmp[96];
    FILE *f = fopen(path, "rb");
    if (!f && snprintf(tmp, sizeof(tmp), "%s.tmp", path) < (int)sizeof(tmp)) f = fopen(tmp, "rb");
    if (!f) return NULL;
    /* Le tampon a la taille du fichier, cap n'est qu'un plafond : une capture relue en garde le tampon
     * tant que l'appareil existe, et des tampons de cap octets chacun epuisaient la RAM d'un ESP32
     * (revue de la PR #156). */
    long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (size <= 0 || (size_t)size > cap || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1);
    size_t got = buf ? fread(buf, 1, (size_t)size + 1, f) : 0;
    fclose(f);
    if (!buf || got != (size_t)size) { free(buf); return NULL; }
    buf[got] = 0;
    if (n) *n = got;
    return buf;
}

/* ── Encodeur et profil d'un appareil ── */
char *cfg_replay_empty(void) { return strdup("{}"); }

bool cfg_device_is_blank(const volet_t *v) {
    return !v->virt && !v->central && !v->rejeu && !v->up[0] && !v->down[0] && !v->stop[0];
}

catalog_encoder_t cfg_device_encoder(const volet_t *v) {
    if (!v->virt) {
        if (v->rejeu) return CAT_ENC_RAW_REPLAY;
        return cfg_device_is_blank(v) ? CAT_ENC_NONE : CAT_ENC_KEELOQ;
    }
    switch ((volet_proto_t)v->proto) {
    case VOLET_PROTO_PROFALUX:  return CAT_ENC_KEELOQ;
    case VOLET_PROTO_SOMFY_RTS: return CAT_ENC_SOMFY_RTS;
    default:                    return CAT_ENC_NONE;
    }
}

const catalog_profile_t *cfg_device_assign_profile(volet_t *v, const char *wanted) {
    if (v->central) return NULL;
    const catalog_encoder_t enc = cfg_device_encoder(v);
    const catalog_profile_t *p = catalog_profile(wanted);
    if (p && (p->encoder != enc || p->enrol != v->virt)) {
        ESP_LOGW(TAG, "'%s' : profil %s refuse (autre encodeur ou autre methode)", v->id, wanted);
        p = NULL;
    }
    if (!p && !v->profile[0] && enc != CAT_ENC_NONE) p = catalog_profile_infer(enc, v->virt, v->marque);
    if (p) strlcpy(v->profile, p->id, sizeof v->profile);
    if (p && !v->type[0]) strlcpy(v->type, p->type, sizeof v->type);
    return catalog_profile(v->profile);
}

const catalog_profile_t *cfg_device_assign_copy_profile(volet_t *v, const cfg_tx_t *radio) {
    const bool had_profile = v->profile[0];
    const catalog_profile_t *p = cfg_device_assign_profile(v, NULL);
    if (!had_profile && p && p->encoder == CAT_ENC_KEELOQ) cfg_device_rebase_tx(v, p, radio);
    return p;
}

int cfg_model_migrate_profiles(const cfg_model_t *m, const cfg_tx_t *keeloq, const cfg_tx_t *somfy) {
    int n = 0;
    for (int i = 0; i < *m->nvolets; i++) {
        volet_t *v = &m->volets[i];
        if (v->central || v->profile[0]) continue;
        const catalog_profile_t *p = cfg_device_assign_profile(v, NULL);
        if (!p) { ESP_LOGW(TAG, "migration : '%s' sans profil (vide ou inclassable), garde son comportement", v->id); continue; }
        /* le rejeu ne lit que sa porteuse, qu'il prend de sa capture : rien a figer */
        if (p->encoder == CAT_ENC_KEELOQ)    cfg_device_rebase_tx(v, p, keeloq);
        if (p->encoder == CAT_ENC_SOMFY_RTS) cfg_device_rebase_tx(v, p, somfy);
        ESP_LOGW(TAG, "migration : '%s' -> profil %s, type %s", v->id, v->profile, v->type);
        n++;
    }
    return n;
}

/* ── Reglages d'emission : la fiche, sinon le profil ── */
cfg_tx_t cfg_device_tx(const volet_t *v, const catalog_profile_t *p) {
    cfg_tx_t t = {
        .freq_khz   = v->freq_khz ? v->freq_khz : (p ? p->freq_khz : 0),
        .te_us      = v->te_us ? v->te_us : (p ? p->te_us : 0),
        .rpt        = v->tx_rpt == TXOVR_ON ? 1 : v->tx_rpt == TXOVR_OFF ? 0 : (p ? p->repeat_bit : -1),
        .rmt        = v->tx_rmt == TXOVR_ON ? 1 : v->tx_rmt == TXOVR_OFF ? 0 : (p ? p->rmt : -1),
        .frame_bits = v->frame_bits ? v->frame_bits : (p ? p->frame_bits : 0),
    };
    return t;
}

void cfg_device_inherit_equal(volet_t *v, const catalog_profile_t *p) {
    if (!p) return;
    /* Un rejeu brut : sa porteuse a 0 est celle de SA CAPTURE, pas celle du profil. Une valeur egale au
     * profil reste donc ecrite, sinon il emettrait sur la capture (revue de la PR #156). */
    if (!v->rejeu && p->freq_khz && v->freq_khz == p->freq_khz) v->freq_khz = 0;
    if (p->te_us && v->te_us == p->te_us)          v->te_us = 0;
    if (p->repeat_bit >= 0 && v->tx_rpt == (p->repeat_bit ? TXOVR_ON : TXOVR_OFF)) v->tx_rpt = TXOVR_INHERIT;
    if (p->rmt >= 0 && v->tx_rmt == (p->rmt ? TXOVR_ON : TXOVR_OFF))               v->tx_rmt = TXOVR_INHERIT;
    if (p->frame_bits && v->frame_bits == p->frame_bits) v->frame_bits = 0;
}

bool cfg_device_rebase_tx(volet_t *v, const catalog_profile_t *p, const cfg_tx_t *now) {
    if (!p || !now) return false;
    const volet_t before = *v;
    /* 1. figer ce que l'appareil emet aujourd'hui, pour chaque reglage qu'il laissait au defaut */
    if (!v->freq_khz)   v->freq_khz   = now->freq_khz;
    if (!v->te_us)      v->te_us      = now->te_us;
    if (v->tx_rpt == TXOVR_INHERIT && now->rpt >= 0) v->tx_rpt = now->rpt ? TXOVR_ON : TXOVR_OFF;
    if (v->tx_rmt == TXOVR_INHERIT && now->rmt >= 0) v->tx_rmt = now->rmt ? TXOVR_ON : TXOVR_OFF;
    if (!v->frame_bits) v->frame_bits = now->frame_bits;
    /* 2. rendre au profil ce qui lui est egal */
    cfg_device_inherit_equal(v, p);
    return v->freq_khz != before.freq_khz || v->te_us != before.te_us || v->tx_rpt != before.tx_rpt
        || v->tx_rmt != before.tx_rmt || v->frame_bits != before.frame_bits;
}
