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
#include <dirent.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/stat.h>
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

/* Coupure pendant l'ecriture d'un FICHIER (le dataset de trames) : la moitie des
 * octets arrive sur le disque, puis plus rien ne se fait, ni renommage, ni
 * suppression, ni ecriture NVS, jusqu'au redemarrage. */
static bool s_file_cut;
static long s_fwrites;   /* ecritures de fichier : un enregistrement ne reecrit pas une capture inchangee */
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
int    __real_rename(const char *, const char *);
int    __real_remove(const char *);
size_t __wrap_fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    if (s_dead) return 0;
    s_fwrites++;
    if (s_file_cut) {
        size_t part = __real_fwrite(p, sz, n / 2, f);
        fflush(f);
        s_file_cut = false; s_dead = true;
        return part;
    }
    return __real_fwrite(p, sz, n, f);
}
/* Comme SPIFFS (SPIFFS_ERR_CONFLICTING_NAME) : pas de renommage vers un fichier qui
 * existe deja. Le rename du PC le remplacerait, et cacherait le cas. */
int __wrap_rename(const char *a, const char *b) {
    if (s_dead) return -1;
    FILE *e = fopen(b, "rb");
    if (e) { fclose(e); return -1; }
    return __real_rename(a, b);
}
int __wrap_remove(const char *a) { return s_dead ? -1 : __real_remove(a); }

/* Compile avec AddressSanitizer (CMakeLists.txt) : un acces hors limites, apres
 * liberation ou une double liberation arrete le banc. Les fuites, elles, sont
 * voulues : le banc jette ses exemplaires de config sans les liberer. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }

/* ════ Outils NVS ═════════════════════════════════════════════════════════ */
/* Le catalogue du firmware, lu depuis le dossier du banc (firmware/test/cfg_store). */
#define CATALOG_PATH "../../main/web/profiles.json"
/* Cles du catalogue que ce banc nomme. */
#define GATE_KEY        "portail"
#define PROFALUX_ENROL  "profalux-enrol"
#define CAME_REPLAY     "came-replay"
#define PROFALUX_COPY   "profalux-copy"
#define UNKNOWN_KEY     "garage"
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
/* Dossier des captures de rejeu des exemplaires (T15) ; NULL : en NVS, comme avant. */
static const char *s_capture_dir;
static void store_bind(store_t *s) {
    s->m = (cfg_model_t){ &s->log_frames, s->remotes, &s->nremotes, s->volets, &s->nvolets, s_capture_dir };
}
static void store_reset(store_t *s) { memset(s, 0, sizeof(*s)); store_bind(s); }
/* Copie profonde : la liste des membres d'une centrale vit sur le tas, et deux
 * exemplaires ne doivent pas la partager (cfg_model_remove la libere). */
static void store_copy(store_t *dst, const store_t *src) {
    *dst = *src; store_bind(dst);
    for (int i = 0; i < dst->nvolets; i++) {
        dst->volets[i].members = NULL;
        cfg_volet_set_members(&dst->volets[i], src->volets[i].members);
        dst->volets[i].rejeu = src->volets[i].rejeu ? strdup(src->volets[i].rejeu) : NULL;
    }
}
static store_t g_ref, g_ram;

static void bits66(char *out, unsigned seed) {
    for (int i = 0; i < 66; i++) { seed = seed * 1103515245u + 12345u; out[i] = (char)('0' + ((seed >> 16) & 1)); }
    out[66] = 0;
}
static volet_t *mk_volet(store_t *s, const char *id, unsigned seed, uint32_t up_ms, uint32_t down_ms) {
    volet_t *v = &s->volets[s->nvolets++];
    memset(v, 0, sizeof(*v));
    snprintf(v->id, sizeof(v->id), "%s", id);
    /* une fiche a toujours un type : celui d'un volet (mode legacy 0), comme une fiche v0.2.16 relue */
    snprintf(v->type, sizeof(v->type), "%s", catalog_type_of_legacy(0) ? catalog_type_of_legacy(0) : "");
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
    cfg_volet_set_members(c, "Volet bureau,Volet du salon,Volet chambre nord,Volet chambre parents,Volet cuisine");
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
        && a->virt_counter == b->virt_counter && a->proto == b->proto
        && a->central == b->central && !strcmp(a->members ? a->members : "", b->members ? b->members : "")
        && a->travel_up_ms == b->travel_up_ms && a->travel_down_ms == b->travel_down_ms
        && a->orientation == b->orientation && a->order == b->order
        && !strcmp(a->type, b->type) && !strcmp(a->profile, b->profile)
        && !strcmp(a->rejeu ? a->rejeu : "", b->rejeu ? b->rejeu : "")
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
/* Volets (non centraux) qu'une liste de membres designe : le parcours de
 * shutters_cmd(), cfg_members_next(), puis la recherche par id. */
static int count_found(const store_t *s, const char *csv) {
    int n = 0; char id[SH_ID_LEN];
    for (const char *p = cfg_members_next(csv, id); p; p = cfg_members_next(p, id))
        for (int i = 0; i < s->nvolets; i++)
            if (!s->volets[i].central && !strcmp(s->volets[i].id, id)) { n++; break; }
    return n;
}
/* Le parcours d'avant (copie dans un tampon, strtok_r, espaces de tete) : la
 * reference a laquelle cfg_members_next() doit se conformer. */
static int count_found_old(const store_t *s, const char *csv) {
    char buf[1024]; snprintf(buf, sizeof(buf), "%s", csv);
    int n = 0; char *sv = NULL;
    for (char *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) {
        while (*t == ' ') t++;
        for (int i = 0; i < s->nvolets; i++)
            if (!s->volets[i].central && !strcmp(s->volets[i].id, t)) { n++; break; }
    }
    return n;
}
static const volet_t *last_central(const store_t *s) {
    const volet_t *c = NULL;
    for (int i = 0; i < s->nvolets; i++) if (s->volets[i].central) c = &s->volets[i];
    return c;
}
/* Membres de la DERNIERE centrale que la diffusion atteint. */
static int members_found(const store_t *s) {
    const volet_t *c = last_central(s);
    return c ? count_found(s, c->members) : 0;
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

/* ════ Captures de rejeu en fichier (T15) ═════════════════════════════════ */
#define CAPTURE_DIR "/tmp/cfg_store_test_captures"
#define CAPTURE_BAD "/nonexistent-cfg-store-test/captures"   /* ecriture impossible */
static void clear_captures(void) {
    DIR *d = opendir(CAPTURE_DIR); struct dirent *e; char p[512];
    while (d && (e = readdir(d))) if (e->d_name[0] != '.') { snprintf(p, sizeof p, "%s/%s", CAPTURE_DIR, e->d_name); remove(p); }
    if (d) closedir(d);
}
static int count_captures(void) {
    DIR *d = opendir(CAPTURE_DIR); struct dirent *e; int n = 0;
    while (d && (e = readdir(d))) if (e->d_name[0] != '.') n++;
    if (d) closedir(d);
    return n;
}
/* Un appareil a rejeu, comme ceux qu'ajoute l'ecran de capture. */
static void add_replay(store_t *s, const char *capture) {
    char id[SH_ID_LEN];
    snprintf(id, sizeof(id), "Portail ajoute %02d", s->nvolets);
    volet_t *v = mk_volet(s, id, 40u + (unsigned)s->nvolets, 0, 0);
    v->n_serials = 0; v->up[0] = v->down[0] = v->stop[0] = 0; v->up_btn = v->down_btn = v->stop_btn = 0;
    snprintf(v->type, sizeof v->type, "%s", GATE_KEY);
    v->rejeu = strdup(capture);
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
static void no_frames_file(void) { remove(FRAMES_OK); remove(FRAMES_OK ".tmp"); s_frames_path = FRAMES_OK; }

/* ════ Tests ══════════════════════════════════════════════════════════════ */
static uint8_t g_saturated[NVS_MAX], g_migrated[NVS_MAX];
static store_t g_sat_ref, g_before, g_tmp;

/* k volets enroles (virtuels, sans trames : les plus petits en NVS) aux
 * identifiants de idlen caracteres (22 ou 23), et la centrale qui les regroupe.
 * Renvoie la longueur de la liste, -1 si elle est refusee. */
static int central_store(store_t *s, int k, int idlen) {
    static char csv[SH_MAX_VOLETS * SH_ID_LEN + 1];
    int p = 0;
    csv[0] = 0;
    store_reset(s); s->log_frames = true;
    for (int i = 0; i < k; i++) {
        char id[SH_ID_LEN];
        if (idlen == SH_ID_LEN - 1) snprintf(id, sizeof(id), "Baie vitree salon n%04u", (unsigned)i % 10000u);
        else                        snprintf(id, sizeof(id), "Baie vitree salon n%03u", (unsigned)i % 1000u);
        volet_t *v = mk_volet(s, id, 40u + (unsigned)i, 21000, 20500);
        v->up[0] = v->down[0] = v->stop[0] = 0; v->up_btn = v->down_btn = v->stop_btn = 0;
        v->virt = true; v->virt_serial = 0x0670200u + (unsigned)i; v->virt_counter = 100;
        snprintf(v->serials[0], SH_SERIAL_LEN, "0x%07X", (unsigned)v->virt_serial);
        p += snprintf(csv + p, sizeof(csv) - p, "%s%s", p ? "," : "", id);
    }
    volet_t *c = mk_volet(s, "Centrale toute la maiso", 99, 0, 0);
    c->n_serials = 0; c->up[0] = c->down[0] = c->stop[0] = 0;
    c->up_btn = c->down_btn = c->stop_btn = 0; c->central = true;
    return cfg_volet_set_members(c, csv) == 0 ? p : -1;
}
/* Membres qu'un tampon de cap octets aurait gardes (384 : l'ancien plafond). */
static int kept_by(const store_t *s, size_t cap) {
    static char cut[SH_MEMBERS_LEN];
    const volet_t *c = last_central(s);
    snprintf(cut, cap < sizeof(cut) ? cap : sizeof(cut), "%s", c && c->members ? c->members : "");
    return count_found(s, cut);
}

int main(void) {
    esp_log_level_set("*", ESP_LOG_NONE);   /* les erreurs provoquees font partie du test */
    ESP_ERROR_CHECK(nvs_flash_init());
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
    if (!s_part || s_part->size > NVS_MAX) { printf("partition 'nvs' absente ou trop grande\n"); return 2; }
    const bool field_size = s_part->size == 0x4000;
    /* Le vrai catalogue : la fiche y lit ses types (mode legacy) et ses profils. */
    char *catalog = cfg_read_file(CATALOG_PATH, 1 << 18, NULL);
    char cat_err[CATALOG_ERR_MAX] = "";
    if (!catalog || catalog_load(catalog, cat_err, sizeof cat_err) != 0) { printf("catalogue illisible : %s %s\n", CATALOG_PATH, cat_err); return 2; }
    free(catalog);
    printf("== Banc cfg_model + cfg_store : NVS ESP-IDF reelle, partition emulee de %u Ko ==\n", (unsigned)(s_part->size / 1024));
    cfg_layout_t lay;
    char fk[8] = "";

    /* ── T0p. Type et profil de la fiche (#134, etape 3), sans NVS ── */
    printf("\n[T0p] Type par sa cle, profil, reglages herites, migration\n");
    {
        const char *shutter = catalog_type_of_legacy(0), *gate = catalog_type_of_legacy(1);
        CHECK(shutter && gate && !strcmp(gate, GATE_KEY), "mode legacy : 0 et 1 sont des types du catalogue");
        cJSON *root = cJSON_Parse("{\"remotes\":{},\"volets\":["
            "{\"id\":\"A\",\"type\":1},{\"id\":\"B\"},{\"id\":\"C\",\"type\":\"portail\",\"profile\":\"came-replay\"},"
            "{\"id\":\"D\",\"type\":\"garage\"},{\"id\":\"E\",\"type\":7}]}");
        store_reset(&g_tmp);
        cfg_model_import(&g_tmp.m, root);
        cJSON_Delete(root);
        const volet_t *a = &g_tmp.volets[0], *b = &g_tmp.volets[1], *c = &g_tmp.volets[2], *d = &g_tmp.volets[3], *e = &g_tmp.volets[4];
        CHECK(!strcmp(a->type, gate), "ancien numero 1 : lu comme la cle du portail (%s)", a->type);
        CHECK(!strcmp(b->type, shutter), "ancienne fiche sans type : celle du numero 0 (%s)", b->type);
        CHECK(!strcmp(c->type, GATE_KEY) && !strcmp(c->profile, CAME_REPLAY), "cle et profil lus tels quels");
        CHECK(!strcmp(d->type, UNKNOWN_KEY), "cle inconnue du catalogue : gardee (version plus recente)");
        CHECK(e->type[0] == 0, "numero qu'aucun type ne porte : type vide, rien d'invente");
        char *js = cfg_model_export(&g_tmp.m);
        cJSON *out = cJSON_Parse(js);
        const cJSON *va = cJSON_GetArrayItem(cJSON_GetObjectItem(out, "volets"), 0);
        CHECK(cJSON_IsString(cJSON_GetObjectItem(va, "type")) && !strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(va, "type")), gate),
              "ecriture : la cle, jamais le numero (mode legacy : lu, jamais ecrit)");
        const cJSON *vb = cJSON_GetArrayItem(cJSON_GetObjectItem(out, "volets"), 1);
        CHECK(!cJSON_GetObjectItem(vb, "type"), "type par defaut : pas ecrit (la NVS n'a que 16 Ko)");
        CHECK(js && !strstr(js, "\"type\":1") && !strstr(js, "\"type\":0"), "aucun numero de type dans l'export");
        const cJSON *vc = cJSON_GetArrayItem(cJSON_GetObjectItem(out, "volets"), 2);
        CHECK(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(vc, "profile")) ?: "", CAME_REPLAY), "le profil est ecrit");
        cJSON_Delete(out); free(js);

        /* Ce qu'emet un appareil : la fiche, sinon le profil */
        const catalog_profile_t *p = catalog_profile(PROFALUX_ENROL);
        CHECK(p && p->freq_khz && p->te_us && p->repeat_bit >= 0 && p->rmt >= 0, "profil %s complet", PROFALUX_ENROL);
        volet_t v; memset(&v, 0, sizeof v);
        cfg_tx_t t = cfg_device_tx(&v, p);
        CHECK(p && t.freq_khz == p->freq_khz && t.te_us == p->te_us && t.rpt == p->repeat_bit && t.rmt == p->rmt,
              "fiche a 0 : les valeurs du profil");
        v.freq_khz = 868330; v.te_us = 390; v.tx_rpt = TXOVR_OFF; v.tx_rmt = TXOVR_OFF;
        t = cfg_device_tx(&v, p);
        CHECK(t.freq_khz == 868330 && t.te_us == 390 && t.rpt == 0 && t.rmt == 0, "fiche reglee : la fiche prime");
        memset(&v, 0, sizeof v);
        t = cfg_device_tx(&v, NULL);
        CHECK(t.freq_khz == 0 && t.te_us == 0 && t.rpt == -1 && t.rmt == -1, "sans profil : rien d'impose, la radio garde son defaut");

        /* Migration : aucune emission ne change. Pour chaque combinaison (fiche a 0 ou reglee, defaut
         * d'aujourd'hui egal ou non au profil), ce que l'appareil emettait avant (la fiche, sinon le defaut
         * d'aujourd'hui) est ce qu'il emet apres (la fiche, sinon le profil). */
        if (p) {
            const uint32_t freqs[] = { 0, p->freq_khz, 868350 };
            const uint16_t tes[] = { 0, p->te_us, 380 };
            const uint8_t ovr[] = { TXOVR_INHERIT, TXOVR_OFF, TXOVR_ON };
            const int8_t nows[] = { 0, 1 };
            int bad = 0, cases = 0;
            for (int fi = 0; fi < 3; fi++) for (int nf = 1; nf < 3; nf++)
            for (int ti = 0; ti < 3; ti++) for (int nt = 1; nt < 3; nt++)
            for (int oi = 0; oi < 3; oi++) for (int no = 0; no < 2; no++) {
                volet_t w; memset(&w, 0, sizeof w);
                w.freq_khz = freqs[fi]; w.te_us = tes[ti]; w.tx_rpt = ovr[oi]; w.tx_rmt = ovr[(oi + 1) % 3];
                const cfg_tx_t now = { .freq_khz = freqs[nf], .te_us = tes[nt], .rpt = nows[no], .rmt = nows[1 - no] };
                cfg_tx_t before = cfg_device_tx(&w, NULL);
                if (!before.freq_khz) before.freq_khz = now.freq_khz;
                if (!before.te_us) before.te_us = now.te_us;
                if (before.rpt < 0) before.rpt = now.rpt;
                if (before.rmt < 0) before.rmt = now.rmt;
                cfg_device_rebase_tx(&w, p, &now);
                cfg_tx_t after = cfg_device_tx(&w, p);
                cases++;
                if (before.freq_khz != after.freq_khz || before.te_us != after.te_us || before.rpt != after.rpt || before.rmt != after.rmt) bad++;
            }
            CHECK(bad == 0, "migration : %d emissions changees sur %d combinaisons", bad, cases);
            volet_t w; memset(&w, 0, sizeof w);
            const cfg_tx_t now = { .freq_khz = p->freq_khz, .te_us = p->te_us, .rpt = p->repeat_bit, .rmt = p->rmt };
            CHECK(cfg_device_rebase_tx(&w, p, &now) == false && w.freq_khz == 0 && w.te_us == 0 && w.tx_rpt == TXOVR_INHERIT,
                  "migration : un appareil egal a son profil le suit (tout a 0, fiche inchangee)");
            w.freq_khz = 868350;
            const cfg_tx_t elsewhere = { .freq_khz = 868350, .te_us = p->te_us, .rpt = p->repeat_bit, .rmt = p->rmt };
            CHECK(cfg_device_rebase_tx(&w, p, &elsewhere) == false && w.freq_khz == 868350, "migration : un reglage different du profil est garde");
            memset(&w, 0, sizeof w);
            CHECK(cfg_device_rebase_tx(&w, p, &elsewhere) == true && w.freq_khz == 868350,
                  "migration : un defaut d'aujourd'hui different du profil est fige dans la fiche");
            w.freq_khz = p->freq_khz; w.te_us = p->te_us; w.tx_rpt = p->repeat_bit ? TXOVR_ON : TXOVR_OFF;
            cfg_device_inherit_equal(&w, p);
            CHECK(w.freq_khz == 0 && w.te_us == 0 && w.tx_rpt == TXOVR_INHERIT, "ecriture : une valeur egale au profil revient a 0");
            const catalog_profile_t *came = catalog_profile(CAME_REPLAY);
            volet_t r; memset(&r, 0, sizeof r);
            char capture[] = "{\"f\":433900}";
            r.rejeu = capture;
            r.freq_khz = came ? came->freq_khz : 0;
            cfg_device_inherit_equal(&r, came);
            CHECK(came && r.freq_khz == came->freq_khz, "rejeu : la porteuse egale au profil reste ecrite (0 = celle de la capture)");
        }

        /* Fiche vide, rejeu sans capture, copie : leur encodeur, et le profil que leur donne la migration.
         * La coquille d'un appareil dont la capture n'a jamais ete enregistree passait pour une copie
         * Profalux, a 868 MHz (constate sur une Super). */
        {
            const cfg_tx_t keeloq = { .freq_khz = 868425, .te_us = 400, .rpt = 1, .rmt = 1 };
            const cfg_tx_t somfy = { .rpt = -1, .rmt = -1 };
            store_reset(&g_tmp);
            volet_t *blank = &g_tmp.volets[g_tmp.nvolets++], *fresh = &g_tmp.volets[g_tmp.nvolets++], *copy = &g_tmp.volets[g_tmp.nvolets++];
            strlcpy(blank->id, "Coquille", SH_ID_LEN); strlcpy(blank->type, GATE_KEY, sizeof blank->type);
            strlcpy(fresh->id, "Rejeu neuf", SH_ID_LEN); strlcpy(fresh->type, GATE_KEY, sizeof fresh->type);
            fresh->rejeu = cfg_rejeu_empty();
            strlcpy(copy->id, "Copie", SH_ID_LEN); strlcpy(copy->up, "1010", SH_BITS_LEN);
            CHECK(cfg_device_is_blank(blank) && cfg_device_encoder(blank) == CAT_ENC_NONE, "fiche vide : aucun encodeur");
            CHECK(fresh->rejeu && cfg_device_encoder(fresh) == CAT_ENC_RAW_REPLAY, "rejeu sans capture : encodeur de rejeu");
            CHECK(cfg_device_encoder(copy) == CAT_ENC_KEELOQ, "trame copiee : KeeLoq");
            int n = cfg_model_migrate_profiles(&g_tmp.m, &keeloq, &somfy);
            CHECK(blank->profile[0] == 0 && blank->freq_khz == 0 && blank->te_us == 0,
                  "migration : une fiche vide reste sans profil ni reglage (profil '%s', %u kHz)", blank->profile, (unsigned)blank->freq_khz);
            CHECK(!strcmp(fresh->profile, CAME_REPLAY), "migration : un rejeu sans capture prend le profil de rejeu (%s)", fresh->profile);
            CHECK(!strcmp(copy->profile, PROFALUX_COPY), "migration : une copie prend le profil de copie (%s)", copy->profile);
            CHECK(n == 2, "migration : %d fiches classees sur 3 (la vide ne l'est pas)", n);
            /* Copie captee sur une radio reglee ailleurs que le profil : elle garde la porteuse de la radio */
            {
                const catalog_profile_t *pc = catalog_profile(PROFALUX_COPY);
                const cfg_tx_t radio = { .freq_khz = 868330, .te_us = pc ? pc->te_us : 0, .rpt = pc ? pc->repeat_bit : -1, .rmt = pc ? pc->rmt : -1 };
                volet_t c2; memset(&c2, 0, sizeof c2); strlcpy(c2.id, "Copie 868330", SH_ID_LEN); strlcpy(c2.up, "1010", SH_BITS_LEN);
                const catalog_profile_t *got = cfg_device_assign_copy_profile(&c2, &radio);
                cfg_tx_t t = cfg_device_tx(&c2, got);
                CHECK(got && !strcmp(c2.profile, PROFALUX_COPY) && t.freq_khz == 868330 && c2.te_us == 0,
                      "copie : profil %s, emet a %u kHz (radio 868330), TE de la fiche %u (egal au profil : 0)", c2.profile, (unsigned)t.freq_khz, (unsigned)c2.te_us);
                c2.freq_khz = 868999;
                cfg_device_assign_copy_profile(&c2, &radio);
                CHECK(c2.freq_khz == 868999, "copie qui avait deja son profil : ses reglages ne bougent pas");
            }
            strlcpy(blank->up, "1010", SH_BITS_LEN);
            CHECK(cfg_device_assign_profile(blank, NULL) && !strcmp(blank->profile, PROFALUX_COPY),
                  "apprentissage : la fiche recoit son profil des qu'elle a une trame (%s)", blank->profile);
            cfg_volet_release(fresh);
        }
    }

    /* ── T0. Serialisation JSON, sans NVS ── */
    printf("\n[T0] Serialisation JSON : export, import, anciens exports\n");
    {
        model_init(&g_ref);
        volet_t *q = mk_volet(&g_ref, "Volet \"sud\" \\ 2", 7, 15000, 14000);   /* guillemets et antislash */
        snprintf(q->serials[1], SH_SERIAL_LEN, "0x0ABCDEF"); q->n_serials = 2; q->orientation = 180;
        volet_t *v = mk_volet(&g_ref, "Virtuel", 8, 0, 0);                        /* volet virtuel */
        v->virt = true; v->virt_serial = 0x0670123; v->virt_counter = 4321;
        v->proto = VOLET_PROTO_SOMFY_RTS;   /* protocole non-Profalux : doit survivre a l'aller-retour */
        volet_t *z = mk_volet(&g_ref, "Zone salon", 9, 0, 0);                     /* zone : volet appris + suiveurs */
        cfg_volet_set_members(z, "Volet du salon,Volet cuisine");
        char *js = cfg_model_export(&g_ref.m);
        cJSON *root = cJSON_Parse(js);
        store_reset(&g_tmp);
        if (root) cfg_model_import(&g_tmp.m, root);
        char *js2 = cfg_model_export(&g_tmp.m);
        bool rt = root && store_equal(&g_tmp, &g_ref) && js2 && !strcmp(js, js2);
        CHECK(rt, "export -> import -> export ne redonne pas la meme config");
        /* proto d'emission : ecrit seulement s'il n'est pas Profalux (0). Un seul volet en
         * porte un ici, donc "proto" doit apparaitre exactement une fois ; les volets
         * Profalux gardent le JSON et les sauvegardes a l'identique. */
        int nproto = 0;
        for (const char *p = js; js && (p = strstr(p, "\"proto\"")) != NULL; p += 7) nproto++;
        CHECK(nproto == 1, "un seul volet doit ecrire \"proto\" (trouve %d)", nproto);
        printf("  aller-retour de %d volets (centrale, virtuel, zone, guillemets, accents) : %s\n", g_ref.nvolets, rt ? "identique" : "DIFFERENT");
        /* Zone : ses suiveurs reviennent, sans en faire une centrale ; un volet sans
         * suiveurs n'ecrit pas de "members" (les sauvegardes restent celles d'avant). */
        const volet_t *zt = NULL, *pl = NULL;
        for (int i = 0; i < g_tmp.nvolets; i++) {
            if (!strcmp(g_tmp.volets[i].id, "Zone salon")) zt = &g_tmp.volets[i];
            if (!strcmp(g_tmp.volets[i].id, "Volet bureau")) pl = &g_tmp.volets[i];
        }
        bool zone_ok = zt && !zt->central && zt->members && !strcmp(zt->members, "Volet du salon,Volet cuisine")
                    && pl && !pl->members;
        cJSON *vo; int with_members = 0;
        cJSON_ArrayForEach(vo, cJSON_GetObjectItem(root, "volets")) with_members += cJSON_HasObjectItem(vo, "members");
        zone_ok = zone_ok && with_members == 2;   /* la centrale et la zone, rien d'autre */
        CHECK(zone_ok, "zone : suiveurs perdus, centrale par erreur, ou \"members\" sur %d volets", with_members);
        printf("  zone (suiveurs relus, pas de \"members\" sur les autres volets) : %s\n", zone_ok ? "OK" : "KO");
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

    /* ── T0b. Identifiant stable d'un appareil ── */
    printf("\n[T0b] Identifiant stable : garde au renommage, absent des sauvegardes tant qu'il vaut le slug\n");
    {
        store_reset(&g_tmp);
        mk_volet(&g_tmp, "Salon", 1, 0, 0);
        mk_volet(&g_tmp, "Chambre Parents", 2, 0, 0);
        int served = cfg_model_ensure_uids(&g_tmp.m);
        CHECK(served == 2 && !strcmp(g_tmp.volets[0].uid, "Salon") && !strcmp(g_tmp.volets[1].uid, "Chambre_Parents"),
              "un appareil d'avant ce champ doit prendre son slug (%d servis, '%s', '%s')", served, g_tmp.volets[0].uid, g_tmp.volets[1].uid);
        CHECK(cfg_model_ensure_uids(&g_tmp.m) == 0, "un second passage ne doit rien changer");
        char *js = cfg_model_export(&g_tmp.m);
        CHECK(js && !strstr(js, "\"uid\""), "jamais renomme : la sauvegarde doit rester celle d'avant (pas de \"uid\")");
        free(js);
        /* renommage, comme shutters_rename_volet : seul le nom change */
        snprintf(g_tmp.volets[0].id, SH_ID_LEN, "S\xC3\xA9jour");
        js = cfg_model_export(&g_tmp.m);
        int nuid = 0;
        for (const char *q = js; js && (q = strstr(q, "\"uid\"")) != NULL; q += 5) nuid++;
        CHECK(nuid == 1 && strstr(js, "\"uid\":\"Salon\""), "renomme : son identifiant d'origine doit etre ecrit, et lui seul (%d)", nuid);
        cJSON *root = cJSON_Parse(js);
        store_reset(&g_ram);
        if (root) cfg_model_import(&g_ram.m, root);
        CHECK(g_ram.nvolets == 2 && !strcmp(g_ram.volets[0].id, "S\xC3\xA9jour") && !strcmp(g_ram.volets[0].uid, "Salon")
              && !strcmp(g_ram.volets[1].uid, "Chambre_Parents"), "relu : l'identifiant d'un appareil renomme est perdu ('%s')", g_ram.volets[0].uid);
        cJSON_Delete(root); free(js);
        /* un NOUVEL appareil reprend le nom libere : il ne doit pas voler l'identite de l'ancien */
        mk_volet(&g_ram, "Salon", 3, 0, 0);
        CHECK(cfg_model_ensure_uids(&g_ram.m) == 1 && !strcmp(g_ram.volets[2].uid, "Salon_2") && !strcmp(g_ram.volets[0].uid, "Salon"),
              "nom repris par un nouvel appareil : identifiants '%s' et '%s'", g_ram.volets[0].uid, g_ram.volets[2].uid);
        /* sauvegarde retouchee : deux fois le meme identifiant, ou un identifiant invalide */
        snprintf(g_ram.volets[1].uid, SH_ID_LEN, "Salon");
        snprintf(g_ram.volets[2].uid, SH_ID_LEN, "pas valable");
        cfg_model_ensure_uids(&g_ram.m);
        CHECK(!strcmp(g_ram.volets[0].uid, "Salon") && !strcmp(g_ram.volets[1].uid, "Chambre_Parents") && !strcmp(g_ram.volets[2].uid, "Salon_2"),
              "doublon ou invalide : le premier garde le sien, les autres reprennent leur slug ('%s', '%s', '%s')",
              g_ram.volets[0].uid, g_ram.volets[1].uid, g_ram.volets[2].uid);
        printf("  renommage, nom repris, doublon, invalide : %s\n", s_fail ? "voir les echecs" : "OK");
        store_reset(&g_tmp); store_reset(&g_ram);
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
    cfg_model_remove(&g_ref.m, gone);
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

        /* Coupure de courant PENDANT l'ecriture du fichier : un fichier incomplet
         * ne doit jamais passer pour le dataset, ni faire effacer sa copie NVS. */
        use_snapshot(g_saturated); no_frames_file();
        s_file_cut = true;
        boot_sequence(); reboot();
        bool safe = flash_is(&g_ref, &lay) && key_size("framesv2", true) == fr;
        CHECK(safe, "coupure pendant l'ecriture du fichier : copie NVS du dataset %u o (attendu %u), format %d", (unsigned)key_size("framesv2", true), (unsigned)fr, lay);
        boot_sequence(); reboot();
        f = fopen(FRAMES_OK, "rb"); fsz = -1;
        if (f) { fseek(f, 0, SEEK_END); fsz = ftell(f); fclose(f); }
        bool whole = flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT && !key_size("framesv2", true) && fsz == (long)fr;
        CHECK(whole, "dataset tronque apres une coupure pendant sa recopie (fichier %ld o, attendu %u)", fsz, (unsigned)fr);
        printf("  coupure pendant l'ecriture du fichier : copie NVS gardee, puis dataset entier (%ld o) -> %s\n", fsz, safe && whole ? "OK" : "KO");
    }
    no_frames_file();

    /* ── T12. Centrale aux membres nombreux et aux identifiants les plus longs ── */
    printf("\n[T12] Centrale aux membres nombreux, aux identifiants les plus longs\n");
    {
        /* Tous les volets dans la centrale : 23 membres et elle, SH_MAX_VOLETS places. */
        const int all = SH_MAX_VOLETS - 1;
        int len = central_store(&g_ref, all, SH_ID_LEN - 1);
        int in_ram = len > 0 ? members_found(&g_ref) : 0, old = kept_by(&g_ref, 384);
        char *js = cfg_model_export(&g_ref.m);
        cJSON *root = js ? cJSON_Parse(js) : NULL;
        store_reset(&g_tmp);
        if (root) cfg_model_import(&g_tmp.m, root);
        int in_backup = members_found(&g_tmp);
        bool rt = root && store_equal(&g_tmp, &g_ref);
        cJSON_Delete(root); free(js);
        CHECK(len > 0 && in_ram == all, "liste de %d membres refusee ou tronquee (limite de %d o) : %d gardes", all, (int)SH_MEMBERS_LEN, in_ram);
        CHECK(rt && in_backup == all, "export -> import : %d membres sur %d", in_backup, all);
        printf("  %d membres, liste de %d o : %d en memoire, %d apres export -> import (le plafond de 384 o en gardait %d)\n",
               all, len, in_ram, in_backup, old);

        /* Limite : SH_MEMBERS_LEN - 1 caracteres acceptes, un de plus refuse, et
         * le refus laisse la liste en place. */
        static char big[SH_MEMBERS_LEN + 1];
        memset(big, 'a', SH_MEMBERS_LEN); big[SH_MEMBERS_LEN] = 0;
        volet_t *c = &g_ref.volets[g_ref.nvolets - 1];
        const char *was = c->members;
        bool refused = was && cfg_volet_set_members(c, big) != 0 && c->members == was && members_found(&g_ref) == all;
        big[SH_MEMBERS_LEN - 1] = 0;
        volet_t probe = {0};
        bool at_max = cfg_volet_set_members(&probe, big) == 0 && probe.members && strlen(probe.members) == SH_MEMBERS_LEN - 1;
        bool none = cfg_volet_set_members(&probe, "") == 0 && !probe.members;
        cfg_volet_release(&probe);
        CHECK(refused && at_max && none, "limite de la liste : refus %d, maximum accepte %d, liste vide %d", refused, at_max, none);
        printf("  limite : %d caracteres acceptes, %d refuses sans toucher a la liste en place -> %s\n",
               SH_MEMBERS_LEN - 1, SH_MEMBERS_LEN, refused && at_max && none ? "OK" : "KO");

        /* Le parcours sans tampon designe les memes volets que l'ancien. */
        static const char *const lists[] = {
            "Baie vitree salon n0000,Baie vitree salon n0001", ",,Baie vitree salon n0002,,", "  Baie vitree salon n0003, Baie vitree salon n0004",
            "Baie vitree salon n0005 ,x", " , ,", "", "Baie vitree salon n0006XYZ,Baie vitree salon n0007",
            "Centrale toute la maiso,Baie vitree salon n0008", "Inconnu,Baie vitree salon n0009,Baie vitree salon n0009" };
        int same = 0, nl = (int)(sizeof(lists) / sizeof(lists[0]));
        for (int i = 0; i < nl; i++) same += count_found(&g_ref, lists[i]) == count_found_old(&g_ref, lists[i]);
        CHECK(same == nl, "parcours des membres different de l'ancien : %d/%d listes", same, nl);
        printf("  parcours sans tampon : memes volets que l'ancien sur %d/%d listes piegees -> %s\n", same, nl, same == nl ? "OK" : "KO");

        /* Suppressions, la ou un pointeur se perd : un volet place AVANT une
         * centrale (sa liste glisse d'une place), puis une centrale suivie d'une
         * autre (la seconde doit garder la sienne). La place liberee en fin de
         * tableau ne doit plus designer aucune liste. */
        char *keep = c->members ? strdup(c->members) : NULL;
        cfg_model_remove(&g_ref.m, 0);
        c = &g_ref.volets[g_ref.nvolets - 1];
        bool del_m = keep && c->central && c->members && !strcmp(c->members, keep)
                     && members_found(&g_ref) == all - 1 && !g_ref.volets[g_ref.nvolets].members;
        static const char bis[] = "Baie vitree salon n0001,Baie vitree salon n0002";
        volet_t *d = mk_volet(&g_ref, "Centrale bis", 98, 0, 0);
        d->central = true; d->n_serials = 0; d->up[0] = d->down[0] = d->stop[0] = 0;
        cfg_volet_set_members(d, bis);
        cfg_model_remove(&g_ref.m, g_ref.nvolets - 2);              /* la premiere centrale */
        d = &g_ref.volets[g_ref.nvolets - 1];
        bool del_c = d->central && d->members && !strcmp(d->members, bis) && !g_ref.volets[g_ref.nvolets].members;
        cfg_model_remove(&g_ref.m, g_ref.nvolets - 1);              /* la seconde */
        del_c = del_c && g_ref.nvolets == all - 1 && !last_central(&g_ref) && !g_ref.volets[g_ref.nvolets].members;
        free(keep);
        CHECK(del_m && del_c, "suppression : d'un volet avant une centrale %d, d'une centrale avant une autre %d", del_m, del_c);
        printf("  suppressions (volet avant une centrale, centrale avant une autre) : %s\n", del_m && del_c ? "OK" : "KO");

        /* Le pire cas qu'une NVS de terrain contient : autant de volets enroles
         * (les plus petits) que possible, et leur centrale. */
        int k = all; esp_err_t e = ESP_FAIL;
        for (; k >= 1; k--) {
            wipe(); system_state();
            if ((len = central_store(&g_ref, k, SH_ID_LEN - 1)) < 0) break;
            if ((e = save_ref(fk)) == ESP_OK) break;
        }
        old = kept_by(&g_ref, 384);
        reboot();
        bool back = e == ESP_OK && flash_is(&g_ref, &lay) && lay == CFG_LAYOUT_SPLIT;
        int in_nvs = back ? members_found(&g_seen) : 0;
        CHECK(back && in_nvs == k, "NVS -> redemarrage : %s, %d membres sur %d", esp_err_to_name(e), in_nvs, k);
        printf("  NVS de %u Ko : au plus %d volets enroles et leur centrale ; liste de %d o -> %d membres sur %d apres redemarrage (le plafond de 384 o en gardait %d)\n",
               (unsigned)(s_part->size / 1024), k, len, in_nvs, k, old);
        stats("ce pire cas");

        /* Et avant la v0.2.5 ? Toute la config tenait dans UNE cle de 4000 octets
         * au plus : la plus petite config dont la liste depasse 383 octets (17
         * volets enroles aux identifiants de 22 caracteres, sans telecommande
         * nommee) n'y entrait pas. Aucune sauvegarde d'alors n'a de liste tronquee. */
        wipe(); system_state();
        int l22 = central_store(&g_ref, 17, 22);
        char *d22 = cfg_model_export(&g_ref.m);
        size_t z22 = d22 ? strlen(d22) + 1 : 0; free(d22);
        esp_err_t el = save_legacy(&g_ref);
        CHECK(l22 > 383 && el != ESP_OK, "ancien format : liste de %d o, document de %u o, ecriture %s", l22, (unsigned)z22, esp_err_to_name(el));
        printf("  ancien format (cle unique) : liste de %d o, document de %u o -> %s\n",
               l22, (unsigned)z22, el != ESP_OK ? "impossible a enregistrer, OK" : "ENREGISTRE");
    }

    /* ── T13. Place des volets en NVS (Systeme > Etat) ── */
    printf("\n[T13] Place des volets en NVS, et estimation de la place restante\n");
    {
        wipe(); system_state(); model_init(&g_ref);
        esp_err_t e = save_ref(fk);
        int n = 0; size_t ent = cfg_store_volet_entries(&n);
        CHECK(e == ESP_OK && n == g_ref.nvolets && ent > 0, "%d volet(s) comptes sur %d (%u entrees)", n, g_ref.nvolets, (unsigned)ent);

        /* La formule colle a la vraie NVS : un volet de plus prend exactement les
         * entrees que la fonction lui attribue (les autres cles ne changent pas). */
        nvs_stats_t a, b;
        nvs_get_stats(NULL, &a);
        add_volet(&g_ref); e = save_ref(fk);
        nvs_get_stats(NULL, &b);
        int n2 = 0; size_t ent2 = cfg_store_volet_entries(&n2);
        long mesure = (long)b.used_entries - (long)a.used_entries, prevu = (long)ent2 - (long)ent;
        CHECK(e == ESP_OK && n2 == n + 1 && mesure == prevu, "volet ajoute : %ld entrees mesurees, %ld prevues", mesure, prevu);
        printf("  %d volets : %u entrees ; un de plus : %ld entrees mesurees, %ld prevues -> %s\n",
               n, (unsigned)ent, mesure, prevu, mesure == prevu ? "OK" : "KO");

        /* L'estimation de l'interface (place disponible / taille moyenne d'un volet), a
         * chaque remplissage, contre le nombre de volets qui entrent vraiment ensuite. */
        int est[SH_MAX_VOLETS + 1], steps = 0, added = 0; bool full = false;
        while (g_ref.nvolets < SH_MAX_VOLETS) {
            nvs_stats_t s; nvs_get_stats(NULL, &s);
            int c = 0; size_t en = cfg_store_volet_entries(&c);
            est[steps++] = en ? (int)((double)s.available_entries * c / en) : -1;
            add_volet(&g_ref);
            if (save_ref(fk) != ESP_OK) { full = true; break; }
            added++;
        }
        int worst = 0, over = 0;
        for (int k = 0; k < steps; k++) {
            int d = est[k] - (added - k);                /* > 0 : promet plus qu'il n'entre */
            if (abs(d) > abs(worst)) worst = d;
            if (d > over) over = d;
        }
        printf("  %s apres %d volets de plus ; estimation au depart : %d ; pire ecart : %+d (surestimation max %d)\n",
               full ? "NVS pleine" : "plafond de volets atteint", added, est[0], worst, over);
        CHECK(full ? abs(worst) <= 1 : est[0] >= added, "estimation : pire ecart %+d volet(s)", worst);
    }

    /* ── T14. Restauration : seul le format de sauvegarde passe, copie de la config d'avant ── */
    printf("\n[T14] Restauration : format de sauvegarde, copie de la config d'avant\n");
    {
#define COPY "/tmp/cfg_store_test_copie.json"
        wipe(); system_state(); model_init(&g_ref);
        char *exp = cfg_model_export(&g_ref.m);
        /* Ce qu'un utilisateur peut choisir par erreur dans l'onglet Sauvegarde. */
        static const struct { const char *nom, *js; bool attendu; } cas[] = {
            { "export des trames",            "{\"trames\":{\"0x0B20000\":{\"count\":1,\"frames\":[]}}}", false },
            { "objet vide",                   "{}", false },
            { "tableau",                      "[]", false },
            { "texte",                        "\"volets\"", false },
            { "volets en objet",              "{\"volets\":{}}", false },
            { "telecommandes en tableau",     "{\"volets\":[],\"remotes\":[]}", false },
            { "sauvegarde sans volet",        "{\"remotes\":{},\"volets\":[]}", true },
            { "sauvegarde sans telecommande", "{\"log_frames\":true,\"volets\":[]}", true },
        };
        int n = (int)(sizeof(cas) / sizeof(cas[0])), bons = 0;
        cJSON *j = cJSON_Parse(exp); bool vraie = cfg_model_is_backup(j); cJSON_Delete(j);
        for (int i = 0; i < n; i++) {
            cJSON *c = cJSON_Parse(cas[i].js); bool got = cfg_model_is_backup(c); cJSON_Delete(c);
            if (got == cas[i].attendu) bons++; else printf("  format mal trie : %s -> %d\n", cas[i].nom, got);
        }
        CHECK(vraie && bons == n, "format : vraie sauvegarde %d, %d/%d cas", vraie, bons, n);
        printf("  vraie sauvegarde acceptee, %d/%d cas pieges tries -> %s\n", bons, n, vraie && bons == n ? "OK" : "KO");

        /* La copie egale l'export, et se remplace : SPIFFS refuse de renommer sur un fichier
         * existant (simule par __wrap_rename), une seconde copie echouait sans le detour. */
        remove(COPY); remove(COPY ".tmp");
        bool c1 = cfg_model_save_copy(&g_ref.m, COPY);
        char *lu = cfg_read_file(COPY, 64 * 1024, NULL);
        bool same1 = c1 && lu && !strcmp(lu, exp); free(lu);
        add_volet(&g_ref);
        char *exp2 = cfg_model_export(&g_ref.m);
        bool c2 = cfg_model_save_copy(&g_ref.m, COPY);
        lu = cfg_read_file(COPY, 64 * 1024, NULL);
        bool same2 = c2 && lu && !strcmp(lu, exp2); free(lu);
        CHECK(same1 && same2, "copie : premiere %d, remplacement %d", same1, same2);
        printf("  copie identique a l'export, puis remplacee par la suivante -> %s\n", same1 && same2 ? "OK" : "KO");

        /* Coupure pendant l'ecriture d'une nouvelle copie : la precedente reste entiere. */
        model_init(&g_ref);
        s_file_cut = true;
        bool c3 = cfg_model_save_copy(&g_ref.m, COPY);
        s_dead = false;
        lu = cfg_read_file(COPY, 64 * 1024, NULL);
        bool intact = !c3 && lu && !strcmp(lu, exp2); free(lu);
        CHECK(intact, "coupure pendant la copie : ecriture %d, precedente %s", c3, intact ? "intacte" : "PERDUE");
        printf("  coupure pendant une nouvelle copie : la precedente reste entiere -> %s\n", intact ? "OK" : "KO");

        /* Coupure entre l'effacement de l'ancienne et le renommage : seul le temporaire,
         * complet, existe ; il est repris. */
        remove(COPY); remove(COPY ".tmp");
        FILE *t = fopen(COPY ".tmp", "wb"); fputs(exp, t); fclose(t);
        lu = cfg_read_file(COPY, 64 * 1024, NULL);
        bool repris = lu && !strcmp(lu, exp); free(lu);
        CHECK(repris, "temporaire seul non repris");
        printf("  coupure entre effacement et renommage : temporaire complet repris -> %s\n", repris ? "OK" : "KO");
        remove(COPY); remove(COPY ".tmp");
        free(exp); free(exp2);
    }


    /* ── T15. Captures de rejeu en fichier, hors de la NVS ── */
    printf("\n[T15] Captures de rejeu en fichier (SPIFFS sur le boitier), hors de la NVS\n");
    {
        /* Une capture comme celles d'une Super : 3 trames de 49 impulsions (~2,3 Ko en clair). */
        char cap[4096]; int p = snprintf(cap, sizeof cap, "{\"f\":433920,\"up\":\"");
        for (int i = 0; i < 300 && p < (int)sizeof cap - 16; i++) p += snprintf(cap + p, sizeof cap - p, "%s%d", i ? "," : "", i % 2 ? -500 - i : 170 + i);
        snprintf(cap + p, sizeof cap - p, "\"}");
        mkdir(CAPTURE_DIR, 0755);

        /* a. Ce que tient la NVS d'un boitier de terrain, captures en NVS puis en fichier */
        int fit[2] = { 0, 0 };
        for (int f = 0; f < 2; f++) {
            s_capture_dir = f ? CAPTURE_DIR : NULL; clear_captures();
            use_snapshot(g_migrated); store_copy(&g_ref, &g_sat_ref);
            while (g_ref.nvolets < SH_MAX_VOLETS) {
                add_replay(&g_ref, cap);
                if (save_ref(fk) != ESP_OK) { cfg_model_remove(&g_ref.m, g_ref.nvolets - 1); break; }
                fit[f]++;
            }
        }
        printf("  appareils a capture ajoutes : %d en NVS, %d en fichier\n", fit[0], fit[1]);
        CHECK(fit[1] >= 3 * (fit[0] + 1), "captures en fichier : %d appareils, en NVS : %d (gain attendu)", fit[1], fit[0]);

        /* b. Relecture : la meme config, la capture lue dans son fichier, le document sans elle. La NVS
         *    vient d'etre remplie jusqu'au refus : on lui rend de la place pour les essais qui suivent. */
        for (int k = 0; k < 3; k++) cfg_model_remove(&g_ref.m, g_ref.nvolets - 1);
        CHECK(save_ref(fk) == ESP_OK, "enregistrement avec de la marge : refuse (cle '%s')", fk);
        reboot();
        bool same = flash_is(&g_ref, NULL);
        CHECK(same, "relecture des captures en fichier");
        nvs_handle_t h; char doc[1024]; size_t dl = sizeof doc; bool slim = false;
        char key[8]; snprintf(key, sizeof key, "v%u", (unsigned)(g_ref.nvolets - 1) % 100u);
        if (nvs_open("shutters", NVS_READONLY, &h) == ESP_OK) {
            slim = nvs_get_str(h, key, doc, &dl) == ESP_OK && strstr(doc, "\"rejeu_file\"") && !strstr(doc, "\"rejeu\"");
            nvs_close(h);
        }
        CHECK(slim, "document NVS : le nom du fichier, pas la capture");
        char *exp = cfg_model_export(&g_ref.m);
        CHECK(exp && strstr(exp, "\"rejeu\"") && !strstr(exp, "\"rejeu_file\""), "export : la capture en clair, comme avant");
        free(exp);
        printf("  relecture %s, document NVS %s\n", same ? "identique" : "DIFFERENTE", slim ? "sans la capture" : "AVEC la capture");

        /* c. Un mouvement ne reecrit aucune capture ; une capture changee, la sienne seulement */
        long w0 = s_fwrites; int refused = 0;
        for (int m = 0; m < 100; m++) { move_one(&g_ref, m); refused += save_ref(fk) != ESP_OK; }
        long moves = s_fwrites - w0;
        CHECK(refused == 0, "%d mouvements refuses sur 100", refused);
        volet_t *last = &g_ref.volets[g_ref.nvolets - 1];
        free(last->rejeu); last->rejeu = strdup("{\"f\":433920,\"up\":\"170,-500\"}");
        w0 = s_fwrites; save_ref(fk); long changed = s_fwrites - w0;
        reboot();
        bool same2 = flash_is(&g_ref, NULL);
        CHECK(moves == 0 && changed == 1 && same2, "ecritures : %ld pour 100 mouvements, %ld pour une capture changee, relecture %d", moves, changed, same2);
        printf("  100 mouvements : %ld ecriture de fichier ; une capture changee : %ld -> %s\n", moves, changed, moves == 0 && changed == 1 ? "OK" : "KO");

        /* d. Appareil supprime : son fichier part, apres l'enregistrement */
        int before = count_captures();
        cfg_model_remove(&g_ref.m, g_ref.nvolets - 1);
        save_ref(fk);
        int after = count_captures();
        CHECK(after == before - 1 && after == fit[1] - 4, "suppression : %d fichiers avant, %d apres", before, after);
        printf("  appareil supprime : %d -> %d fichiers -> %s\n", before, after, after == before - 1 ? "OK" : "KO");

        /* e. Coupure pendant l'ecriture d'une capture : l'etat d'avant se relit, capture comprise */
        store_t was; store_copy(&was, &g_ref);
        last = &g_ref.volets[g_ref.nvolets - 1];
        free(last->rejeu); last->rejeu = strdup("{\"f\":433920,\"up\":\"180,-510,180,-510\"}");   /* une autre capture */
        last->position = (float)(((int)last->position + 50) % 101);
        s_file_cut = true;
        save_ref(fk);
        CHECK(!s_file_cut, "la capture changee n'a pas ete ecrite : la coupure n'a pas eu lieu");
        s_file_cut = false;
        reboot();
        bool kept = flash_is(&was, NULL);
        CHECK(kept, "coupure pendant l'ecriture d'une capture : etat d'avant %s", kept ? "relu" : "PERDU");
        printf("  coupure pendant l'ecriture d'une capture : etat d'avant relu -> %s\n", kept ? "OK" : "KO");

        /* g. Une capture relue ne garde que sa taille en RAM, pas le plafond de lecture : douze tampons de
         *    64 Ko epuisaient un ESP32 (revue de la PR #156) */
        load_seen();
        const volet_t *sv = &g_seen.volets[g_seen.nvolets - 1];
        size_t want = sv->rejeu ? strlen(sv->rejeu) + 1 : 0, held = sv->rejeu ? malloc_usable_size(sv->rejeu) : 0;
        CHECK(want && held < want + 64, "capture relue : %zu octets tenus pour %zu utiles", held, want);
        printf("  capture relue : %zu octets en RAM pour %zu -> %s\n", held, want, want && held < want + 64 ? "OK" : "KO");

        /* h. Une capture qu'on n'a pas pu relire garde son fichier et son nom : le prochain enregistrement
         *    ne l'efface pas, et elle revient au demarrage suivant */
        char path[512], *saved = NULL;
        snprintf(path, sizeof path, "%s/%s", CAPTURE_DIR, sv->rejeu_file);   /* l'etat relu, celui d'avant la coupure (e) */
        size_t sl = 0; saved = cfg_read_file(path, 1 << 20, &sl);
        FILE *tf = fopen(path, "wb"); if (tf) fclose(tf);   /* fichier vide : illisible */
        int files0 = count_captures();
        load_seen();
        bool lost = !g_seen.volets[g_seen.nvolets - 1].rejeu;
        esp_err_t es = cfg_model_save(&g_seen.m, NULL);
        bool file_kept = count_captures() == files0 && access(path, F_OK) == 0;
        tf = fopen(path, "wb"); if (tf && saved) fwrite(saved, 1, sl, tf); if (tf) fclose(tf);
        free(saved);
        reboot();
        bool back = flash_is(&was, NULL);
        CHECK(saved && lost && es == ESP_OK && file_kept && back,
              "capture non relue : perdue a la lecture %d, fichier garde %d, revenue %d", lost, file_kept, back);
        printf("  capture non relue : fichier garde %d, revenue au demarrage suivant %d -> %s\n", file_kept, back, file_kept && back ? "OK" : "KO");

        /* f. Fichier impossible a ecrire : la capture reste dans la NVS, et se relit */
        s_capture_dir = CAPTURE_BAD;
        wipe(); system_state(); model_init(&g_ref);
        add_replay(&g_ref, "{\"f\":433920,\"up\":\"170,-500\"}");
        esp_err_t e13 = save_ref(fk);
        reboot();
        bool inl = e13 == ESP_OK && flash_is(&g_ref, NULL);
        CHECK(inl, "dossier impossible : capture gardee en NVS (%s)", esp_err_to_name(e13));
        printf("  dossier impossible : capture gardee dans la NVS -> %s\n", inl ? "OK" : "KO");
        s_capture_dir = NULL; clear_captures();
    }

    printf("\n== %s (%d echec%s) ==\n", s_fail ? "ECHEC" : "TOUT EST BON", s_fail, s_fail > 1 ? "s" : "");
    return s_fail ? 1 : 0;
}
