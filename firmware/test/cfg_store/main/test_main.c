/* Banc de test de la persistance de la config (cfg_model.c + cfg_store.c) sur la
 * VRAIE bibliotheque NVS d'ESP-IDF (cible linux), partition emulee de la taille
 * declaree dans partitions.csv (16 Ko, la table des boitiers deployes).
 *
 * Les deux modules sont compiles tels quels. Le banc tient deux configs :
 *   - g_ref : ce qui DOIT se trouver en flash ;
 *   - g_ram : ce que le firmware a charge au demarrage, et qu'il reecrit.
 * Tout passe par le code du firmware : serialisation JSON des volets, ecriture
 * par cle, relecture, migration et fin de demarrage.
 *
 * L'etat de depart reproduit l'occupation d'une NVS relevee sur un boitier en
 * usage reel : pile Wi-Fi, calibration radio, reglages, 6 volets (dont une
 * centrale), 15 telecommandes, et le dataset de trames qu'alimente un MQTT
 * actif. Les contenus sont factices, les tailles voisines de celles mesurees.
 *
 * Coupure de courant : la NVS ecrit via esp_partition_write_raw / _erase_range,
 * interceptes ici (ld --wrap). Au point de coupure l'ecriture en cours est
 * TRONQUEE, puis plus rien n'atteint la flash jusqu'au "redemarrage" (NVS fermee
 * puis rouverte : elle relit tout depuis la flash). */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cfg_model.h"
#include "cfg_store.h"

/* ════ Coupure de courant ════════════════════════════════════════════════ */
static bool s_armed, s_dead;
static long s_budget;   /* cycles d'ecriture (4 o) restants avant la coupure */
static long s_cycles;   /* cycles consommes : sert a mesurer une operation */

esp_err_t __real_esp_partition_write_raw(const esp_partition_t *, size_t, const void *, size_t);
esp_err_t __real_esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);
esp_err_t __real_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);

typedef esp_err_t (*wr_fn)(const esp_partition_t *, size_t, const void *, size_t);
static esp_err_t cut_write(wr_fn real, const esp_partition_t *p, size_t off, const void *src, size_t size) {
    if (s_dead) return ESP_FAIL;
    long cyc = (long)(size / 4);
    s_cycles += cyc;
    if (s_armed && s_budget < cyc) {
        if (s_budget > 0) real(p, off, src, (size_t)s_budget * 4);   /* ecriture TRONQUEE */
        s_dead = true;
        return ESP_FAIL;
    }
    if (s_armed) s_budget -= cyc;
    return real(p, off, src, size);
}
esp_err_t __wrap_esp_partition_write_raw(const esp_partition_t *p, size_t o, const void *s, size_t n) {
    return cut_write(__real_esp_partition_write_raw, p, o, s, n);
}
esp_err_t __wrap_esp_partition_write(const esp_partition_t *p, size_t o, const void *s, size_t n) {
    return cut_write(__real_esp_partition_write, p, o, s, n);
}
esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *p, size_t o, size_t n) {
    if (s_dead) return ESP_FAIL;
    s_cycles += 1;
    if (s_armed) {
        if (s_budget < 1) { s_dead = true; return ESP_FAIL; }
        s_budget -= 1;
    }
    return __real_esp_partition_erase_range(p, o, n);
}
static void arm(long k) { s_budget = k; s_armed = true; s_dead = false; }

/* ════ Outils NVS ═════════════════════════════════════════════════════════ */
#define NVS_MAX 0x8000                      /* taille maximale geree par le banc */
static const esp_partition_t *s_part;
static int s_fail;
#define CHECK(c, ...) do { if (!(c)) { s_fail++; printf("  ECHEC : " __VA_ARGS__); printf("\n"); } } while (0)

/* Redemarrage : courant retabli, la NVS relit tout depuis la flash. Une NVS qui
 * refuserait de s'ouvrir serait EFFACEE par le firmware : c'est un echec. */
static void reboot(void) {
    nvs_flash_deinit();
    s_armed = s_dead = false;
    esp_err_t e = nvs_flash_init();
    CHECK(e == ESP_OK, "nvs_flash_init apres coupure : %s (le firmware effacerait la NVS)", esp_err_to_name(e));
}
static void snapshot(uint8_t *img) { esp_partition_read_raw(s_part, 0, img, s_part->size); }
static void use_snapshot(const uint8_t *img) {
    nvs_flash_deinit();
    s_armed = s_dead = false;
    __real_esp_partition_erase_range(s_part, 0, s_part->size);
    __real_esp_partition_write_raw(s_part, 0, img, s_part->size);
    nvs_flash_init();
}
static void wipe(void) {
    nvs_flash_deinit();
    s_armed = s_dead = false;
    __real_esp_partition_erase_range(s_part, 0, s_part->size);
    nvs_flash_init();
}
static void stats(const char *label) {
    nvs_stats_t st;
    nvs_get_stats(NULL, &st);
    printf("  %-36s %3u entrees ecrites, %3u libres, sur %u\n", label,
           (unsigned)st.used_entries, (unsigned)st.free_entries, (unsigned)st.total_entries);
}
/* Taille d'une cle du namespace "shutters" (0 si absente). */
static size_t key_size(const char *key, bool blob) {
    nvs_handle_t h;
    if (nvs_open("shutters", NVS_READONLY, &h) != ESP_OK) return 0;
    size_t sz = 0;
    esp_err_t e = blob ? nvs_get_blob(h, key, NULL, &sz) : nvs_get_str(h, key, NULL, &sz);
    nvs_close(h);
    return e == ESP_OK ? sz : 0;
}
static int read_nv(void) {
    nvs_handle_t h; uint8_t nv = 0;
    if (nvs_open("shutters", NVS_READONLY, &h) != ESP_OK) return -1;
    esp_err_t e = nvs_get_u8(h, "nv", &nv);
    nvs_close(h);
    return e == ESP_OK ? nv : -1;
}

/* ════ Config : les tableaux du firmware, en plusieurs exemplaires ════════ */
typedef struct {
    bool     log_frames;
    remote_t remotes[SH_MAX_REMOTES]; int nremotes;
    volet_t  volets[SH_MAX_VOLETS];   int nvolets;
    cfg_model_t m;
} store_t;
static void store_bind(store_t *s) {
    s->m = (cfg_model_t){ &s->log_frames, s->remotes, &s->nremotes, s->volets, &s->nvolets };
}
static void store_reset(store_t *s) { memset(s, 0, sizeof(*s)); store_bind(s); }
static void store_copy(store_t *dst, const store_t *src) { *dst = *src; store_bind(dst); }
static store_t g_ref, g_ram;

static void bits66(char *out, unsigned seed) {
    for (int i = 0; i < 66; i++) { seed = seed * 1103515245u + 12345u; out[i] = (char)('0' + ((seed >> 16) & 1)); }
    out[66] = 0;
}
static volet_t *mk_volet(store_t *s, const char *id, unsigned seed, uint32_t up_ms, uint32_t down_ms) {
    volet_t *v = &s->volets[s->nvolets++];
    memset(v, 0, sizeof(*v));
    snprintf(v->id, sizeof(v->id), "%s", id);
    snprintf(v->serials[0], SH_SERIAL_LEN, "0x%07X", 0x0A10000u + seed * 0x111u);
    v->n_serials = 1;
    bits66(v->up, seed * 3 + 1); bits66(v->down, seed * 3 + 2); bits66(v->stop, seed * 3 + 3);
    v->up_btn = 8; v->down_btn = 2; v->stop_btn = 4;
    v->travel_up_ms = up_ms; v->travel_down_ms = down_ms;
    v->orientation = -1; v->order = s->nvolets - 1; v->position = 100;
    return v;
}
/* Le boitier de reference : 15 telecommandes, 5 volets, 1 centrale. */
static void model_init(store_t *s) {
    store_reset(s);
    s->log_frames = true;
    const char *names[SH_MAX_REMOTES] = { "Salon", "Cuisine", "Bureau", "Chambre 1", "Chambre 2", "Generale",
                                          "Ancienne salon", "Voisin", "Inconnue 1", "Inconnue 2" };
    for (int i = 0; i < 15; i++) {
        snprintf(s->remotes[i].serial, SH_SERIAL_LEN, "0x%07X", 0x0B20000u + (unsigned)i * 0x1357u);
        snprintf(s->remotes[i].name, SH_ID_LEN, "%s", names[i] ? names[i] : "");
    }
    s->nremotes = 15;
    mk_volet(s, "Volet bureau", 1, 0, 0);
    mk_volet(s, "Volet du salon", 2, 20989, 20643);
    mk_volet(s, "Volet chambre nord", 3, 0, 0);
    mk_volet(s, "Volet chambre parents", 4, 0, 0);
    mk_volet(s, "Volet cuisine", 5, 17883, 17441);
    volet_t *c = mk_volet(s, "G\xC3\xA9n\xC3\xA9ral", 6, 0, 0);          /* UTF-8, comme sur le terrain */
    c->n_serials = 0; c->up[0] = c->down[0] = c->stop[0] = 0;
    c->up_btn = c->down_btn = c->stop_btn = 0; c->position = 0; c->central = true;
    snprintf(c->members, sizeof(c->members), "Volet bureau,Volet du salon,Volet chambre nord,Volet chambre parents,Volet cuisine");
}
static void add_volet(store_t *s) {
    char id[SH_ID_LEN];
    snprintf(id, sizeof(id), "Volet ajoute numero %02d", s->nvolets);
    mk_volet(s, id, 20u + (unsigned)s->nvolets, 21000, 20500);
}
static void move_one(store_t *s, int m) {
    volet_t *v = &s->volets[(m * 7) % s->nvolets];
    v->position = (float)(((int)v->position + 13) % 101);
}

/* Egalite sur ce qui est PERSISTE (l'etat d'execution n'en fait pas partie). */
static bool volet_equal(const volet_t *a, const volet_t *b) {
    if (strcmp(a->id, b->id) || a->n_serials != b->n_serials) return false;
    for (int i = 0; i < a->n_serials; i++) if (strcmp(a->serials[i], b->serials[i])) return false;
    return !strcmp(a->up, b->up) && !strcmp(a->down, b->down) && !strcmp(a->stop, b->stop)
        && a->up_btn == b->up_btn && a->down_btn == b->down_btn && a->stop_btn == b->stop_btn
        && a->virt == b->virt && a->virt_serial == b->virt_serial
        && a->virt_counter == b->virt_counter && a->virt_te == b->virt_te
        && a->central == b->central && !strcmp(a->members, b->members)
        && a->travel_up_ms == b->travel_up_ms && a->travel_down_ms == b->travel_down_ms
        && a->orientation == b->orientation && a->order == b->order
        && (int)(a->position + 0.5f) == (int)(b->position + 0.5f);
}
static bool hdr_equal(const store_t *a, const store_t *b) {
    if (a->log_frames != b->log_frames || a->nremotes != b->nremotes) return false;
    for (int i = 0; i < a->nremotes; i++)
        if (strcmp(a->remotes[i].serial, b->remotes[i].serial) || strcmp(a->remotes[i].name, b->remotes[i].name)) return false;
    return true;
}
static bool store_equal(const store_t *a, const store_t *b) {
    if (!hdr_equal(a, b) || a->nvolets != b->nvolets) return false;
    for (int i = 0; i < a->nvolets; i++) if (!volet_equal(&a->volets[i], &b->volets[i])) return false;
    return true;
}

/* Lit la flash avec le code du firmware. */
static store_t g_seen;
static cfg_layout_t load_seen(void) { store_reset(&g_seen); return cfg_model_load(&g_seen.m); }
static bool flash_is(const store_t *expected, cfg_layout_t *lay_out) {
    cfg_layout_t lay = load_seen();
    if (lay_out) *lay_out = lay;
    return lay != CFG_LAYOUT_NONE && store_equal(&g_seen, expected);
}
/* Ecrit la config comme l'ANCIEN firmware : une seule chaine "cfg". */
static esp_err_t save_legacy(const store_t *s) {
    nvs_handle_t h;
    esp_err_t e = nvs_open("shutters", NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    char *d = cfg_model_export(&s->m);
    e = nvs_set_str(h, "cfg", d); free(d);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
static esp_err_t save_ref(char *failed_key) { return cfg_model_save(&g_ref.m, failed_key); }

/* ════ Etat de terrain ════════════════════════════════════════════════════ */
static void put_blob(nvs_handle_t h, const char *k, size_t n) {
    static uint8_t b[4096];
    memset(b, 0xA5, n);
    ESP_ERROR_CHECK(nvs_set_blob(h, k, b, n));
}
static void put_str(nvs_handle_t h, const char *k, size_t n_with_nul) {
    char s[64];
    memset(s, 'w', n_with_nul - 1); s[n_with_nul - 1] = 0;
    ESP_ERROR_CHECK(nvs_set_str(h, k, s));
}
/* Ce qu'ecrivent la pile Wi-Fi, la calibration radio et /api/config. */
static void system_state(void) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("nvs.net80211", NVS_READWRITE, &h));
    const struct { const char *k; size_t n; } blobs[] = {
        {"sta.apinfo", 700}, {"ap.pmk_info", 132}, {"ap.passwd", 65}, {"sta.pswd", 65},
        {"ap.ssid", 36}, {"sta.ssid", 36}, {"sta.apsw", 2}, {"sta.sae_h2e_id", 32} };
    for (size_t i = 0; i < sizeof(blobs) / sizeof(blobs[0]); i++) put_blob(h, blobs[i].k, blobs[i].n);
    const char *u8[] = { "ap.sndchan", "ap.authmode", "ap.sae_h2e", "ap.chan", "ap.hidden", "ap.max.conn",
        "ap.p_cipher", "ap.ftm_r", "opmode", "bssid.set", "sta.scan_method", "sta.sort_method",
        "sta.minauth", "sta.pmf_e", "sta.pmf_r", "sta.rrm_e", "sta.btm_e", "sta.mbo_e", "sta.ft",
        "sta.owe", "sta.bss_retry", "sta.trans_d", "sta.sae_h2e", "sta.sae_pk_mode", "sta.chan" };
    for (size_t i = 0; i < sizeof(u8) / sizeof(u8[0]); i++) ESP_ERROR_CHECK(nvs_set_u8(h, u8[i], 1));
    ESP_ERROR_CHECK(nvs_set_u16(h, "bcn.interval", 100));
    ESP_ERROR_CHECK(nvs_set_u16(h, "sta.lis_intval", 3));
    ESP_ERROR_CHECK(nvs_set_i8(h, "sta.minrssi", -127));
    nvs_commit(h); nvs_close(h);

    ESP_ERROR_CHECK(nvs_open("phy", NVS_READWRITE, &h));
    put_blob(h, "cal_data", 1904); put_blob(h, "cal_mac", 6);
    ESP_ERROR_CHECK(nvs_set_u32(h, "cal_version", 1));
    nvs_commit(h); nvs_close(h);

    ESP_ERROR_CHECK(nvs_open("cfg", NVS_READWRITE, &h));
    put_str(h, "wifi_ssid", 11); put_str(h, "wifi_pass", 19); put_str(h, "device", 13);
    put_str(h, "mqtt_uri", 28); put_str(h, "mqtt_user", 12); put_str(h, "mqtt_pass", 20);
    const char *c8[] = { "log_frames", "debug", "rx_gain" };
    for (int i = 0; i < 3; i++) ESP_ERROR_CHECK(nvs_set_u8(h, c8[i], 0));
    ESP_ERROR_CHECK(nvs_set_u32(h, "tx_te", 457));
    nvs_commit(h); nvs_close(h);
}
static void fill_frames(int size) {
    static uint8_t fb[4096];
    memset(fb, 0x5A, sizeof(fb));
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("shutters", NVS_READWRITE, &h));
    nvs_set_blob(h, "framesv2", fb, (size_t)size);   /* peut echouer : NVS pleine, c'est le sujet */
    nvs_commit(h); nvs_close(h);
}

/* ════ Demarrage du firmware ══════════════════════════════════════════════
 * Les memes appels que shutters_init(), dans le meme ordre : la config, le
 * dataset de trames, puis cfg_store_finish_boot(). Le fichier du dataset vit
 * sur le disque du PC (SPIFFS sur le boitier). */
#define FRAMES_OK  "/tmp/cfg_store_test_frames.bin"
#define FRAMES_BAD "/nonexistent-cfg-store-test/frames.bin"   /* ecriture impossible */
static const char *s_frames_path = FRAMES_OK;
static esp_err_t save_ram(void) { return cfg_model_save(&g_ram.m, NULL); }
static cfg_layout_t boot_sequence(void) {
    static uint8_t buf[4096];
    size_t sz = 0;
    store_reset(&g_ram);
    cfg_layout_t lay = cfg_model_load(&g_ram.m);
    bool frames_stored = cfg_frames_load(s_frames_path, buf, sizeof(buf), &sz);
    cfg_store_finish_boot(lay, frames_stored, save_ram);
    return lay;
}
/* Boitier qui n'a jamais demarre le nouveau firmware : pas encore de fichier. */
static void no_frames_file(void) { remove(FRAMES_OK); s_frames_path = FRAMES_OK; }

/* ════ Tests ══════════════════════════════════════════════════════════════ */
static uint8_t g_saturated[NVS_MAX], g_migrated[NVS_MAX];
static store_t g_sat_ref, g_before, g_tmp;

int main(void) {
    esp_log_level_set("*", ESP_LOG_NONE);   /* les erreurs provoquees font partie du test */
    ESP_ERROR_CHECK(nvs_flash_init());
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!s_part || s_part->size > NVS_MAX) { printf("partition 'nvs' absente ou trop grande\n"); return 2; }
    const bool field_size = s_part->size == 0x4000;
    printf("== Banc cfg_model + cfg_store : NVS ESP-IDF reelle, partition emulee de %u Ko ==\n", (unsigned)(s_part->size / 1024));
    cfg_layout_t lay;
    char fk[8] = "";

    /* ── T0. Serialisation JSON, sans NVS ── */
    printf("\n[T0] Serialisation JSON : export, import, anciens exports\n");
    {
        model_init(&g_ref);
        volet_t *q = mk_volet(&g_ref, "Volet \"sud\" \\ 2", 7, 15000, 14000);   /* guillemets et antislash */
        snprintf(q->serials[1], SH_SERIAL_LEN, "0x0ABCDEF"); q->n_serials = 2; q->orientation = 180;
        volet_t *v = mk_volet(&g_ref, "Virtuel", 8, 0, 0);                        /* volet virtuel */
        v->virt = true; v->virt_serial = 0x0670123; v->virt_counter = 4321; v->virt_te = 455;
        char *js = cfg_model_export(&g_ref.m);
        cJSON *root = cJSON_Parse(js);
        store_reset(&g_tmp);
        if (root) cfg_model_import(&g_tmp.m, root);
        char *js2 = cfg_model_export(&g_tmp.m);
        bool rt = root && store_equal(&g_tmp, &g_ref) && js2 && !strcmp(js, js2);
        CHECK(rt, "export -> import -> export ne redonne pas la meme config");
        printf("  aller-retour de %d volets (centrale, virtuel, guillemets, accents) : %s\n", g_ref.nvolets, rt ? "identique" : "DIFFERENT");
        /* Export d'une version sans rang d'affichage : "order" absent -> -1, le reste intact. */
        cJSON *v0;
        cJSON_ArrayForEach(v0, cJSON_GetObjectItem(root, "volets")) cJSON_DeleteItemFromObject(v0, "order");
        store_reset(&g_tmp);
        cfg_model_import(&g_tmp.m, root);
        bool old = g_tmp.nvolets == g_ref.nvolets;
        for (int i = 0; old && i < g_tmp.nvolets; i++) {
            old = g_tmp.volets[i].order == -1;
            g_tmp.volets[i].order = g_ref.volets[i].order;
        }
        old = old && store_equal(&g_tmp, &g_ref);
        CHECK(old, "import d'un ancien export (sans \"order\")");
        printf("  ancien export sans rang d'affichage : %s\n", old ? "repris, rang a -1" : "KO");
        /* Deux fois le meme volet dans un document : le second est ignore. */
        cJSON *vols = cJSON_GetObjectItem(root, "volets");
        cJSON_AddItemToArray(vols, cJSON_Duplicate(cJSON_GetArrayItem(vols, 0), true));
        store_reset(&g_tmp);
        cfg_model_import(&g_tmp.m, root);
        CHECK(g_tmp.nvolets == g_ref.nvolets, "un volet en double devrait etre ignore (%d volets)", g_tmp.nvolets);
        cJSON_Delete(root); free(js); free(js2);
    }

    model_init(&g_ref);
    { char *l = cfg_model_export(&g_ref.m); printf("\nconfig de reference : 6 volets, 15 telecommandes, %u o en un seul document\n", (unsigned)strlen(l)); free(l); }

    /* ── T1. Reproduire le bug ── */
    printf("\n[T1] Ancien format sur un boitier de terrain : la config est resauvegardee a\n"
           "     chaque mouvement pendant que le dataset de trames grossit (MQTT actif)\n");
    wipe();
    system_state();
    int first_fail = -1, fail = 0, ok = 0;
    for (int fr = 256; fr <= 4096; fr += 256) {
        fill_frames(fr);
        for (int m = 0; m < 20; m++) {
            move_one(&g_ref, m);
            if (save_legacy(&g_ref) == ESP_OK) ok++;
            else { fail++; if (first_fail < 0) first_fail = fr; }
        }
    }
    stats("etat final");
    printf("  sauvegardes de config : %d reussies, %d ECHOUEES", ok, fail);
    if (first_fail > 0) printf(" (des que le dataset atteint %d o)", first_fail);
    printf("\n");
    if (field_size) CHECK(fail > 0, "bug NON reproduit : le modele de terrain ne colle plus");
    lay = load_seen();                              /* la reference devient ce qui est REELLEMENT en flash */
    CHECK(lay == CFG_LAYOUT_LEGACY && g_seen.nvolets == 6, "ancienne cle attendue en fin de T1 (format %d, %d volets)", lay, g_seen.nvolets);
    store_copy(&g_ref, &g_seen);
    snapshot(g_saturated);
    store_copy(&g_sat_ref, &g_ref);

    /* ── T2. Migration sur cette NVS ── */
    printf("\n[T2] Premier demarrage du nouveau firmware sur ce boitier\n");
    use_snapshot(g_saturated); no_frames_file();
    s_cycles = 0;
    boot_sequence();
    long mig_cycles = s_cycles;
    stats("apres migration");
    reboot();
    bool ok2 = flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT;
    CHECK(ok2, "migration : format %d, contenu %s", lay, store_equal(&g_seen, &g_ref) ? "identique" : "DIFFERENT");
    CHECK(!key_size("cfg", false), "l'ancienne cle 'cfg' devrait etre effacee");
    CHECK(!key_size("framesv2", true), "l'ancien dataset 'framesv2' devrait etre efface");
    printf("  %s (%ld cycles d'ecriture)\n", ok2 ? "migration OK : chaque champ de chaque volet identique" : "migration KO", mig_cycles);
    if (!ok2) {   /* les tests suivants partent de l'etat migre : sans objet s'il n'existe pas */
        printf("\n== ECHEC : migration impossible, tests suivants non joues ==\n");
        return 1;
    }
    printf("  documents en NVS : en-tete %u o ; volets", (unsigned)key_size("hdr", false) - 1);
    for (int i = 0; i < 6; i++) { char k[8]; snprintf(k, sizeof(k), "v%d", i); printf(" %u", (unsigned)key_size(k, false) - 1); }
    printf(" o\n");
    snapshot(g_migrated);

    /* ── T3. Endurance ── */
    printf("\n[T3] 10 000 mouvements apres migration\n");
    use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
    int ko = 0; long cyc = 0;
    for (int m = 0; m < 10000; m++) {
        move_one(&g_ref, m);
        s_cycles = 0;
        if (save_ref(fk) != ESP_OK) ko++;
        cyc += s_cycles;
    }
    stats("apres 10 000 mouvements");
    printf("  %d echec ; %.0f o ecrits en flash par mouvement\n", ko, cyc * 4.0 / 10000);
    CHECK(ko == 0, "%d sauvegardes echouees (cle %s)", ko, fk);
    reboot();
    CHECK(flash_is(&g_ref, NULL), "contenu different apres l'endurance");
    {   /* comparaison : ancien format, sur une NVS ou il tient encore (dataset parti) */
        use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
        long c = 0; int n_ok = 0;
        for (int m = 0; m < 200; m++) {
            move_one(&g_ref, m);
            s_cycles = 0;
            if (save_legacy(&g_ref) == ESP_OK) { n_ok++; c += s_cycles; }
        }
        if (n_ok) printf("  pour comparaison, ancien format : %.0f o par mouvement\n", c * 4.0 / n_ok);
    }

    /* ── T4. Saturation : jusqu'ou, et que reste-t-il apres un refus ? ── */
    printf("\n[T4] Saturation : volets ajoutes un a un, 200 mouvements par palier\n");
    use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
    int max_split = g_ref.nvolets; bool refused = false;
    while (g_ref.nvolets < SH_MAX_VOLETS) {
        add_volet(&g_ref);
        bool bad = save_ref(fk) != ESP_OK;
        for (int m = 0; !bad && m < 200; m++) { move_one(&g_ref, m); bad = save_ref(fk) != ESP_OK; }
        if (bad) { printf("  %d volets : refuse (cle '%s')\n", g_ref.nvolets, fk); refused = true; break; }
        max_split = g_ref.nvolets;
    }
    if (refused) {
        /* L'ecriture refusee ne doit PAS laisser "nv" incoherent : nv volets
         * annonces, nv volets relus (aucun manquant, aucun en double). */
        reboot(); lay = load_seen();
        int nv = read_nv();
        bool sane = lay == CFG_LAYOUT_SPLIT && g_seen.nvolets == nv && hdr_equal(&g_seen, &g_ref)
                    && (nv == max_split || nv == max_split + 1);
        CHECK(sane, "config incoherente apres un refus (format %d, nv=%d, %d volets relus)", lay, nv, g_seen.nvolets);
        printf("  apres le refus : nv=%d, %d volets relus -> %s\n", nv, g_seen.nvolets, sane ? "coherent" : "INCOHERENT");

        /* NVS pleine : AJOUTER un volet echoue sur sa nouvelle cle. "nv" ne doit
         * pas annoncer ce volet, sinon la relecture attend un document absent. */
        store_copy(&g_ref, &g_seen);
        add_volet(&g_ref);
        esp_err_t e = save_ref(fk);
        reboot(); lay = load_seen();
        int nv2 = read_nv();
        if (e != ESP_OK) {
            bool kept = nv2 == nv && g_seen.nvolets == nv;
            CHECK(kept, "ajout refuse (cle '%s') mais nv=%d pour %d volets lisibles (attendu %d)", fk, nv2, g_seen.nvolets, nv);
            printf("  ajout d'un volet sur NVS pleine : refuse (cle '%s'), nv reste a %d -> %s\n", fk, nv2, kept ? "coherent" : "INCOHERENT");
        } else {
            printf("  ajout d'un volet : accepte (nv=%d)\n", nv2);
        }
    }
    {   /* Le plafond lui-meme : max_split volets, 1000 mouvements, relecture exacte */
        use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
        while (g_ref.nvolets < max_split) add_volet(&g_ref);
        int bad = save_ref(fk) != ESP_OK;
        for (int m = 0; m < 1000; m++) { move_one(&g_ref, m); bad += save_ref(fk) != ESP_OK; }
        stats("au plafond, apres 1000 mouvements");
        reboot();
        bool same = flash_is(&g_ref, NULL);
        CHECK(bad == 0 && same, "plafond de %d volets : %d echecs, relecture %s", max_split, bad, same ? "OK" : "KO");
    }
    {   /* ancien format, meme systeme mais SANS dataset : son cas le plus favorable */
        wipe(); system_state(); model_init(&g_ref);
        int max_legacy = 0;
        for (;;) {
            esp_err_t e = save_legacy(&g_ref);
            for (int m = 0; e == ESP_OK && m < 200; m++) { move_one(&g_ref, m); e = save_legacy(&g_ref); }
            if (e != ESP_OK) { printf("  ancien format, %d volets : %s\n", g_ref.nvolets, esp_err_to_name(e)); break; }
            max_legacy = g_ref.nvolets;
            if (g_ref.nvolets >= SH_MAX_VOLETS) break;
            add_volet(&g_ref);
        }
        if (max_legacy) printf("  => une cle par volet : %d volets ; ancien format : %d (sans dataset)\n", max_split, max_legacy);
        else printf("  => une cle par volet : %d volets ; ancien format : echoue des 6 volets, meme sans dataset\n", max_split);
    }
    if (field_size) CHECK(max_split >= 10, "capacite sur 16 Ko tombee a %d volets", max_split);

    /* ── T5. Coupure a chaque instant de la migration ── */
    printf("\n[T5] Coupure a CHAQUE point de la migration (%ld points), puis redemarrages\n", mig_cycles + 1);
    store_copy(&g_ref, &g_sat_ref);
    int bad_read = 0, bad_final = 0, n_legacy = 0, n_split = 0;
    for (long k = 0; k <= mig_cycles; k++) {
        use_snapshot(g_saturated); no_frames_file();
        arm(k); boot_sequence(); reboot();          /* la coupure tombe quelque part la-dedans */
        cfg_layout_t l1;
        if (!flash_is(&g_ref, &l1) && bad_read++ < 3) printf("  point %ld : config illisible ou alteree (format %d)\n", k, l1);
        if (l1 == CFG_LAYOUT_LEGACY) n_legacy++; else if (l1 == CFG_LAYOUT_SPLIT) n_split++;
        boot_sequence(); reboot();                   /* demarrage suivant : la migration reprend */
        cfg_layout_t l2;
        bool done = flash_is(&g_ref, &l2) && l2 == CFG_LAYOUT_SPLIT && !key_size("cfg", false) && !key_size("framesv2", true);
        if (!done && bad_final++ < 3) printf("  point %ld : migration non aboutie au demarrage suivant\n", k);
    }
    printf("  config intacte juste apres la coupure : %ld/%ld (encore ancien format %d, deja nouveau %d)\n",
           mig_cycles + 1 - bad_read, mig_cycles + 1, n_legacy, n_split);
    printf("  migration aboutie au demarrage suivant : %ld/%ld\n", mig_cycles + 1 - bad_final, mig_cycles + 1);
    CHECK(bad_read == 0 && bad_final == 0, "coupure pendant la migration");

    /* ── T6. Coupure pendant une sauvegarde ordinaire ── */
    use_snapshot(g_migrated); store_copy(&g_before, &g_sat_ref); store_copy(&g_ref, &g_sat_ref);
    const int moved = 3;
    g_ref.volets[moved].position = (float)(((int)g_ref.volets[moved].position + 50) % 101);
    s_cycles = 0; save_ref(NULL);
    long mv_cycles = s_cycles;
    printf("\n[T6] Coupure a chaque point d'une sauvegarde ordinaire (%ld points)\n", mv_cycles + 1);
    int t6_bad = 0, t6_old = 0, t6_new = 0;
    for (long k = 0; k <= mv_cycles; k++) {
        use_snapshot(g_migrated);
        arm(k); save_ref(NULL); reboot();
        bool okk = load_seen() == CFG_LAYOUT_SPLIT && g_seen.nvolets == 6 && hdr_equal(&g_seen, &g_ref);
        for (int i = 0; okk && i < 6; i++) {
            bool is_old = volet_equal(&g_seen.volets[i], &g_before.volets[i]);
            bool is_new = volet_equal(&g_seen.volets[i], &g_ref.volets[i]);
            okk = is_old || is_new;
            if (i == moved) { if (is_old) t6_old++; else if (is_new) t6_new++; }
        }
        if (!okk && t6_bad++ < 3) printf("  point %ld : config alteree\n", k);
    }
    printf("  coherente : %ld/%ld (le volet deplace garde l'ancienne valeur %d fois, la nouvelle %d fois)\n",
           mv_cycles + 1 - t6_bad, mv_cycles + 1, t6_old, t6_new);
    CHECK(t6_bad == 0, "coupure pendant une sauvegarde");

    /* ── T7. Coupure pendant la suppression d'un volet ── */
    store_copy(&g_ref, &g_before);
    const int gone = 2;                                  /* les volets suivants changent de cle */
    for (int i = gone; i < g_ref.nvolets - 1; i++) g_ref.volets[i] = g_ref.volets[i + 1];
    g_ref.nvolets--;
    use_snapshot(g_migrated); s_cycles = 0; save_ref(NULL);
    long del_cycles = s_cycles;
    printf("\n[T7] Coupure a chaque point de la suppression d'un volet (%ld points)\n", del_cycles + 1);
    int t7_bad = 0;
    for (long k = 0; k <= del_cycles; k++) {
        use_snapshot(g_migrated);
        arm(k); save_ref(NULL); reboot();
        /* Le chargeur du firmware ignore un volet deja lu. Tout autre volet que
         * celui supprime doit etre la, intact ; rien d'inconnu ne doit apparaitre. */
        bool okk = load_seen() == CFG_LAYOUT_SPLIT && hdr_equal(&g_seen, &g_before)
                   && (g_seen.nvolets == 5 || g_seen.nvolets == 6);
        for (int b = 0; okk && b < 6; b++) {
            bool found = false;
            for (int i = 0; i < g_seen.nvolets; i++) if (volet_equal(&g_seen.volets[i], &g_before.volets[b])) found = true;
            if (b != gone && !found) okk = false;
            if (b == gone && !found && g_seen.nvolets != 5) okk = false;
        }
        if (!okk && t7_bad++ < 3) printf("  point %ld : config alteree\n", k);
    }
    printf("  aucun autre volet perdu ni altere : %ld/%ld\n", del_cycles + 1 - t7_bad, del_cycles + 1);
    CHECK(t7_bad == 0, "coupure pendant une suppression");

    /* ── T8. Boitier neuf, et document impossible a produire ── */
    printf("\n[T8] Boitier neuf (NVS vide), puis un document impossible a produire\n");
    wipe(); no_frames_file(); model_init(&g_ref);
    lay = boot_sequence();
    CHECK(lay == CFG_LAYOUT_NONE && g_ram.nvolets == 0, "NVS vide : rien a lire");
    CHECK(save_ref(NULL) == ESP_OK, "premiere sauvegarde");
    reboot();
    bool ok8 = flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT;
    CHECK(ok8, "relecture d'un boitier neuf");
    {   /* Plus de memoire pour serialiser un volet : "nv" ne doit rien annoncer de plus. */
        cfg_store_tx_t tx;
        cfg_store_begin(&tx);
        for (int i = 0; i < 7; i++) cfg_store_put_volet(&tx, NULL);
        esp_err_t e = cfg_store_end(&tx);
        reboot();
        bool kept = e != ESP_OK && read_nv() == 6 && flash_is(&g_ref, NULL);
        CHECK(kept, "document NULL : erreur %s, nv=%d", esp_err_to_name(e), read_nv());
        ok8 = ok8 && kept;
    }
    printf("  %s\n", ok8 ? "OK" : "KO");

    /* ── T9. Retour a un ancien firmware, puis nouvelle mise a jour ── */
    printf("\n[T9] Retour a un ancien firmware, config modifiee, puis nouvelle mise a jour\n");
    {
        /* L'ancien firmware fait vivre la config, l'utilisateur y apprend un
         * volet, en supprime : il reecrit "cfg" sans toucher aux cles par volet,
         * desormais PERIMEES. Elles lui prennent de la place : selon la taille
         * de sa config, il arrive ou non a l'enregistrer. On essaie toutes les
         * tailles, de 7 volets a 1. */
        store_t after; int played = 0, good = 0, cut_k = 0;
        for (int k = 7; k >= 1; k--) {
            use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
            for (int m = 0; m < 5; m++) move_one(&g_ref, m);
            add_volet(&g_ref);
            g_ref.nvolets = k;
            if (save_legacy(&g_ref) != ESP_OK) continue;   /* celle-la ne tient pas : cas de T10 */
            played++;
            if (!cut_k) { cut_k = k; store_copy(&after, &g_ref); }
            boot_sequence(); reboot();
            bool ok9 = flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT && !key_size("cfg", false);
            boot_sequence(); reboot();                     /* un demarrage de plus ne change rien */
            ok9 = ok9 && flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT;
            if (ok9) good++;
            else printf("  %d volet(s) : config de l'ancien firmware NON reprise (format %d, %d volets relus)\n", k, lay, g_seen.nvolets);
        }
        CHECK(played > 0, "l'ancien firmware n'a jamais pu ecrire sa config : T9 non joue");
        CHECK(good == played, "les cles par volet perimees l'ont emporte, ou la reprise a echoue");
        printf("  la config de l'ancien firmware fait foi : %d/%d tailles de config essayees\n", good, played);

        /* Et avec une coupure a chaque point de cette reprise. */
        if (cut_k) {
            use_snapshot(g_migrated); save_legacy(&after);
            static uint8_t img[NVS_MAX];
            snapshot(img);
            s_cycles = 0; boot_sequence();
            long n = s_cycles; int bad = 0;
            for (long k = 0; k <= n; k++) {
                use_snapshot(img);
                arm(k); boot_sequence(); reboot();
                bool okk = flash_is(&after, NULL);         /* jamais l'etat perime, jamais un melange */
                boot_sequence(); reboot();
                okk = okk && flash_is(&after, &lay) && lay == CFG_LAYOUT_SPLIT && !key_size("cfg", false);
                if (!okk && bad++ < 3) printf("  point %ld : config alteree\n", k);
            }
            printf("  coupure a chaque point de la reprise (%d volets, %ld points) : %ld/%ld\n", cut_k, n + 1, n + 1 - bad, n + 1);
            CHECK(bad == 0, "coupure pendant la reprise apres retour en arriere");
        }
    }

    /* ── T10. Meme retour, mais l'ancien firmware n'a pu ecrire qu'une config vide ── */
    printf("\n[T10] Retour a un ancien firmware qui n'a pu enregistrer qu'une config VIDE\n");
    use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
    {
        store_copy(&g_tmp, &g_ref); g_tmp.nvolets = 0;   /* "cfg" = en-tete + "volets":[] */
        esp_err_t e10 = save_legacy(&g_tmp);
        CHECK(e10 == ESP_OK, "ecriture de la config vide : %s", esp_err_to_name(e10));
        boot_sequence(); reboot();
        bool ok10 = flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT && !key_size("cfg", false);
        CHECK(ok10, "la config vide de l'ancien firmware a efface les volets (%d volets, attendu %d)", g_seen.nvolets, g_ref.nvolets);
        printf("  %s\n", ok10 ? "les volets des cles par volet sont conserves : OK" : "KO");
    }

    /* ── T11. Le fichier du dataset ne peut pas etre ecrit ── */
    printf("\n[T11] Migration alors que le fichier du dataset ne peut pas etre ecrit\n");
    use_snapshot(g_saturated); no_frames_file(); store_copy(&g_ref, &g_sat_ref);
    {
        size_t fr = key_size("framesv2", true);
        s_frames_path = FRAMES_BAD;
        boot_sequence(); reboot();
        bool kept = flash_is(&g_ref, &lay) && key_size("framesv2", true) == fr && fr > 0;
        CHECK(kept, "dataset ou config perdus alors que le fichier n'a pas pu etre ecrit (format %d, dataset %u o)", lay, (unsigned)key_size("framesv2", true));
        printf("  fichier impossible : config intacte (%s), dataset conserve en NVS (%u o) -> %s\n",
               lay == CFG_LAYOUT_LEGACY ? "ancien format garde" : "deja migree", (unsigned)fr, kept ? "OK" : "KO");
        s_frames_path = FRAMES_OK;                      /* le stockage refonctionne */
        boot_sequence(); reboot();
        FILE *f = fopen(FRAMES_OK, "rb");
        long fsz = -1;
        if (f) { fseek(f, 0, SEEK_END); fsz = ftell(f); fclose(f); }
        bool done = flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT && !key_size("framesv2", true) && fsz == (long)fr;
        CHECK(done, "reprise apres retour du stockage (format %d, fichier %ld o, attendu %u)", lay, fsz, (unsigned)fr);
        printf("  stockage revenu : dataset en fichier (%ld o), NVS liberee, config migree -> %s\n", fsz, done ? "OK" : "KO");
    }
    remove(FRAMES_OK);

    printf("\n== %s (%d echec%s) ==\n", s_fail ? "ECHEC" : "TOUT EST BON", s_fail, s_fail > 1 ? "s" : "");
    return s_fail ? 1 : 0;
}
