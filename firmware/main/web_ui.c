/*
 * web_ui.c — serveur HTTP OpenProfalux : sert l'UI embarquee + endpoints sous /api.
 */
#include "web_ui.h"
#include "shutters.h"
#include "pfx_enrol.h"
#include "radio.h"
#include "ota.h"
#include "hardware_config.h"   /* TARGET_NAME (expose la variante a l'UI OTA) */
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "mdns.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "cc1101.h"
#include "esp_random.h"
#include <inttypes.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "web_ui";

/* ── Authentification par SESSION (cookie) ──────────────────────────────────
 * MODELE : tant qu'AUCUN mot de passe n'est enregistre (cle NVS cfg/ui_pass vide
 * ou absente), l'UI est OUVERTE — c'est l'etat du 1er demarrage et celui des
 * boitiers deja deployes : aucune regression, aucun mot de passe par defaut a
 * deviner. Des que l'utilisateur en definit un, TOUTES les routes "/api/" l'exigent.
 * La desactivation se fait par un bouton dedie de l'UI (envoie ui_pass vide).
 *
 * POURQUOI PAS HTTP Basic : la popup native du navigateur ne permet ni de se
 * deconnecter, ni d'expliquer quoi que ce soit, et le navigateur rejoue les
 * identifiants indefiniment. On utilise donc une page de login servie par l'UI :
 * POST /api/login verifie le mot de passe et pose un cookie de session.
 *
 * JETONS EN RAM UNIQUEMENT : ils ne sont jamais ecrits en flash (pas d'usure NVS,
 * rien a lire sur une flash extraite). Consequence assumee : un reboot du boitier
 * invalide les sessions ouvertes -> il faut se reconnecter. Un boitier reboote
 * rarement, et c'est le comportement le plus sur.
 *
 * PORTEE : seul le HTTP est concerne. Home Assistant pilote le boitier par MQTT
 * (cover/set, listen/set, update/install...) et n'appelle jamais ces routes :
 * activer l'auth n'a AUCUN impact sur l'integration HA, OTA par HA comprise.
 *
 * Le cookie transite en clair (pas de TLS sur l'UI) : cela protege d'un acces
 * opportuniste sur le LAN, pas d'une ecoute du reseau. C'est le niveau adapte
 * a la menace reelle ici ; le point dur restant est /api/ota/upload, qui permet
 * sinon de pousser un firmware arbitraire depuis n'importe quel appareil. */
#define UI_PASS_MAX   64
#define UI_SESS_MAX    4     /* sessions simultanees (navigateurs/appareils) */
#define UI_TOKEN_LEN  32     /* longueur du jeton en caracteres hex */
#define UI_SESS_TTL_S (7 * 24 * 3600)   /* validite d'une session : 7 jours */

/* Mot de passe courant en cache RAM (evite un acces NVS a chaque requete).
 * Vide = authentification desactivee. Recharge par ui_auth_reload(). */
static char s_ui_pass[UI_PASS_MAX] = "";

/* Table des sessions actives (RAM). exp = date d'expiration (s depuis boot). */
typedef struct { char tok[UI_TOKEN_LEN + 1]; int64_t exp; } uisess_t;
static uisess_t s_sess[UI_SESS_MAX];

static void ui_auth_reload(void) {
    nvs_handle_t h;
    s_ui_pass[0] = 0;
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_ui_pass);
        if (nvs_get_str(h, "ui_pass", s_ui_pass, &sz) != ESP_OK) s_ui_pass[0] = 0;
        nvs_close(h);
    }
}

/* Comparaison a TEMPS CONSTANT : un strcmp sort au 1er caractere different et
 * laisse fuir la longueur du prefixe correct, ce qui permet de reconstruire le
 * secret caractere par caractere. On compare toujours tout le buffer. Utilise
 * pour le mot de passe ET pour les jetons de session. */
static bool ui_secret_equal(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < la; i++) diff |= (unsigned char)(a[i] ^ b[i < lb ? i : (lb ? lb - 1 : 0)]);
    return diff == 0;
}

static int64_t ui_now_s(void) { return esp_timer_get_time() / 1000000; }

/* Cree une session et ecrit son jeton dans out (UI_TOKEN_LEN+1 octets).
 * Jeton tire d'esp_random() (RNG materiel). Si la table est pleine, recycle
 * l'entree la plus proche de l'expiration. */
static void ui_sess_create(char *out) {
    uint32_t r[UI_TOKEN_LEN / 8];
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++) r[i] = esp_random();
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++)
        snprintf(out + i * 8, 9, "%08" PRIx32, r[i]);
    out[UI_TOKEN_LEN] = 0;

    int slot = 0; int64_t oldest = INT64_MAX; int64_t now = ui_now_s();
    for (int i = 0; i < UI_SESS_MAX; i++) {
        if (!s_sess[i].tok[0] || s_sess[i].exp <= now) { slot = i; break; }   /* libre/expire */
        if (s_sess[i].exp < oldest) { oldest = s_sess[i].exp; slot = i; }
    }
    strlcpy(s_sess[slot].tok, out, sizeof(s_sess[slot].tok));
    s_sess[slot].exp = now + UI_SESS_TTL_S;
}

static void ui_sess_drop(const char *tok) {
    for (int i = 0; i < UI_SESS_MAX; i++)
        if (s_sess[i].tok[0] && ui_secret_equal(s_sess[i].tok, tok)) { memset(&s_sess[i], 0, sizeof(s_sess[i])); return; }
}
static void ui_sess_drop_all(void) { memset(s_sess, 0, sizeof(s_sess)); }

static bool ui_sess_valid(const char *tok) {
    int64_t now = ui_now_s();
    for (int i = 0; i < UI_SESS_MAX; i++)
        if (s_sess[i].tok[0] && s_sess[i].exp > now && ui_secret_equal(s_sess[i].tok, tok)) return true;
    return false;
}

/* Extrait la valeur du cookie de session de la requete. false si absent. */
static bool ui_cookie_get(httpd_req_t *r, char *out, size_t cap) {
    char c[256];
    size_t n = sizeof(c);
    if (httpd_req_get_cookie_val(r, "opfx_sess", c, &n) != ESP_OK) return false;
    strlcpy(out, c, cap);
    return true;
}

/* true si la requete est autorisee. Repond 401 (JSON) sinon — SANS en-tete
 * WWW-Authenticate : c'est precisement lui qui declencherait la popup native
 * du navigateur. L'UI intercepte le 401 et affiche sa propre page de login. */
static bool ui_auth_ok(httpd_req_t *r) {
    if (!s_ui_pass[0]) return true;   /* pas de mot de passe defini -> UI ouverte */

    char tok[UI_TOKEN_LEN + 1] = "";
    if (ui_cookie_get(r, tok, sizeof(tok)) && ui_sess_valid(tok)) return true;

    httpd_resp_set_status(r, "401 Unauthorized");
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr(r, "{\"ok\":0,\"err\":\"auth\"}");
    return false;
}

/* Fichiers UI embarques (voir EMBED_FILES du CMakeLists). */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

/* ── Etat apprentissage ── */
static char           s_lbits[SH_BITS_LEN];
static uint32_t       s_lserial;
static uint8_t        s_lbtn;
static int8_t         s_lrssi;
static volatile bool  s_lready;
static volatile bool  s_lactive;

/* ── Capture télécommande pour enrôlement PFX (appui télécommande -> serial -> identité) ── */
static volatile bool     s_pcap_active, s_pcap_done;
static volatile int      s_pcap_rc, s_pcap_new, s_pcap_model;
static volatile uint32_t s_pcap_remote;

static uint32_t bits_lsb(const char *b, int from, int len) {
    uint32_t v = 0;
    for (int i = 0; i < len; i++) if (b[from + i] == '1') v |= (1u << i);
    return v;
}

static void learn_task(void *arg) {
    (void)arg;
    char bits[SH_BITS_LEN];
    /* Ecoute dediee 15 s : on garde la PREMIERE trame valide, quel que soit le bouton.
     * C'est l'action choisie dans l'UI + le bouton physique presse qui determinent la
     * commande. Filtrer par valeur de bouton etait une erreur (codes reels differents de
     * ce qu'on supposait) : ca empechait d'apprendre stop et descente. */
    int n = radio_listen_once(15000, bits, SH_BITS_LEN - 1);
    if (n >= 60) {
        s_lrssi   = cc1101_get_rssi();
        strlcpy(s_lbits, bits, SH_BITS_LEN);
        s_lserial = bits_lsb(bits, 32, 28);
        s_lbtn    = (uint8_t)bits_lsb(bits, 60, 4);
        s_lready  = true;
        /* fait aussi apparaitre la trame captee dans le journal RF debug */
        shutters_on_rx(bits, s_lserial, s_lbtn, s_lrssi, bits_lsb(bits, 0, 32));
    }
    s_lactive = false;
    vTaskDelete(NULL);
}

/* ── Helpers HTTP ── */
static esp_err_t send_asset(httpd_req_t *r, const char *type, const uint8_t *s, const uint8_t *e) {
    httpd_resp_set_type(r, type);
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");   /* toujours revalider : pas d'UI perimee apres MAJ */
    return httpd_resp_send(r, (const char *)s, e - s);
}
static char *read_body(httpd_req_t *r) {
    int len = r->content_len;
    if (len <= 0 || len > 8192) return NULL;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    int got = 0;
    while (got < len) {
        int k = httpd_req_recv(r, buf + got, len - got);
        if (k <= 0) { free(buf); return NULL; }
        got += k;
    }
    buf[len] = 0;
    return buf;
}
static const char *jstr(cJSON *o, const char *k) {
    cJSON *i = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(i) ? i->valuestring : NULL;
}

/* ── Handlers statiques ── */
static esp_err_t h_index(httpd_req_t *r) { return send_asset(r, "text/html",       index_html_start, index_html_end); }
static esp_err_t h_css  (httpd_req_t *r) { return send_asset(r, "text/css",        style_css_start,  style_css_end);  }
static esp_err_t h_js   (httpd_req_t *r) { return send_asset(r, "application/javascript", app_js_start, app_js_end);  }

/* ── /api/status ── */
static esp_err_t h_status(httpd_req_t *r) {
    static char buf[4096];
    int n = shutters_status_json(buf, sizeof(buf));
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n > 0 ? n : 0);
}

/* ── /api/shutter ── */
static esp_err_t h_shutter(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    const char *id = jstr(j, "id"), *cmd = jstr(j, "cmd");
    cJSON *val = cJSON_GetObjectItem(j, "value");
    int rc = (id && cmd) ? shutters_cmd(id, cmd, val ? (int)val->valuedouble : 0) : -1;
    cJSON_Delete(j);
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":%d,\"tx_marc\":%d}", rc == 0 ? 1 : 0, g_tx_marc);
    httpd_resp_sendstr(r, resp);
    return ESP_OK;
}

/* ── /api/volet/delete : supprime un volet (permet de reapprendre proprement) ── */
static esp_err_t h_volet_delete(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    int rc = shutters_delete_volet(jstr(j, "id"));
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}

/* ── /api/learn/start + /poll ── */
static esp_err_t h_learn_start(httpd_req_t *r) {
    char *body = read_body(r); if (body) free(body);   /* le corps {action} n'est plus utilise cote firmware */
    if (!s_lactive) { s_lready = false; s_lactive = true; xTaskCreate(learn_task, "learn", 4096, NULL, 6, NULL); }
    httpd_resp_sendstr(r, "{\"ok\":1}");
    return ESP_OK;
}
static esp_err_t h_learn_poll(httpd_req_t *r) {
    httpd_resp_set_type(r, "application/json");
    if (!s_lready) { httpd_resp_sendstr(r, "{}"); return ESP_OK; }
    char out[SH_BITS_LEN + 96];
    snprintf(out, sizeof(out), "{\"bits\":\"%s\",\"serial\":\"0x%07X\",\"button\":\"%X\",\"rssi\":%d}",
             s_lbits, (unsigned)s_lserial, s_lbtn, s_lrssi);
    httpd_resp_sendstr(r, out);
    return ESP_OK;
}
static esp_err_t h_learn_assign(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    int rc = shutters_learn_assign(jstr(j, "id"), jstr(j, "action"), jstr(j, "bits"));
    cJSON_Delete(j); s_lready = false;
    if (rc == 0) {   /* AUTO : cale le TE d'emission sur le tempo de la trame captee (le champ UI reste un override) */
        int te = cc1101_last_te();
        if (te >= 350 && te <= 550) {   /* TE plausible d'une telecommande PFX (Profalux 455, FranciaFlex ~415) */
            cc1101_set_tx_te((uint32_t)te);
            nvs_handle_t h;
            if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u32(h, "tx_te", (uint32_t)te); nvs_commit(h); nvs_close(h); }
            ESP_LOGI(TAG, "TE d'emission auto-cale a %d us (mesure sur la trame captee)", te);
        }
    }
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}
static esp_err_t h_learn_reassign(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    int rc = shutters_reassign(jstr(j, "id"), jstr(j, "from"), jstr(j, "to"));
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}
static esp_err_t h_learn_adopt(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    const char *hs = jstr(j, "hop");
    uint32_t hop = hs ? (uint32_t)strtoul(hs, NULL, 16) : 0;
    int rc = shutters_adopt(jstr(j, "id"), jstr(j, "action"), jstr(j, "serial"), hop);
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}

static esp_err_t h_rf_replay(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    const char *hs = jstr(j, "hop");
    uint32_t hop = hs ? (uint32_t)strtoul(hs, NULL, 16) : 0;
    int rc = shutters_replay_frame(jstr(j, "serial"), hop);
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}

/* ── /api/calibrate + /api/remote ── */
static esp_err_t h_calibrate(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    int rc = shutters_calibrate(jstr(j, "id"),
        (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "travel_up_ms")),
        (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "travel_down_ms")));
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}
static esp_err_t h_orientation(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    cJSON *ori = cJSON_GetObjectItem(j, "orientation");
    int rc = shutters_set_orientation(jstr(j, "id"), ori ? (int)cJSON_GetNumberValue(ori) : -1);
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}
static esp_err_t h_remote(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    int rc = shutters_remote_name(jstr(j, "serial"), jstr(j, "name") ?: "");
    cJSON_Delete(j);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}

/* ── /api/config : Wi-Fi + MQTT + nom (namespace NVS "cfg", lu par main.c) ── */
static void cfg_get(nvs_handle_t h, const char *k, char *out, size_t cap) {
    size_t sz = cap; out[0] = 0; nvs_get_str(h, k, out, &sz);
}
static esp_err_t h_config_get(httpd_req_t *r) {
    char dev[32] = "", ssid[33] = "", uri[160] = "", user[128] = "", pass[256] = "";
    uint8_t logf = 0, dbg = 0, rg = 0x27;
    uint32_t txte = 455;
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
        cfg_get(h, "device", dev, sizeof(dev)); cfg_get(h, "wifi_ssid", ssid, sizeof(ssid));
        cfg_get(h, "mqtt_uri", uri, sizeof(uri)); cfg_get(h, "mqtt_user", user, sizeof(user));
        cfg_get(h, "mqtt_pass", pass, sizeof(pass));   /* longueur seulement, JAMAIS renvoye en clair */
        nvs_get_u8(h, "log_frames", &logf);
        nvs_get_u8(h, "debug", &dbg);
        uint8_t tmp; if (nvs_get_u8(h, "rx_gain", &tmp) == ESP_OK && tmp) rg = tmp;
        uint32_t t; if (nvs_get_u32(h, "tx_te", &t) == ESP_OK && t) txte = t;
        nvs_close(h);
    }
    char out[560];
    /* ui_auth : etat de la protection (jamais le mot de passe lui-meme, meme tronque). */
    snprintf(out, sizeof(out),
             "{\"device\":\"%s\",\"wifi_ssid\":\"%s\",\"mqtt_uri\":\"%s\",\"mqtt_user\":\"%s\","
             "\"mqtt_user_len\":%d,\"mqtt_pass_len\":%d,\"log_frames\":%d,\"debug\":%d,\"rx_gain\":%d,\"tx_te\":%u,"
             "\"ui_auth\":%d}",
             dev, ssid, uri, user, (int)strlen(user), (int)strlen(pass), logf ? 1 : 0, dbg ? 1 : 0, rg, (unsigned)txte,
             s_ui_pass[0] ? 1 : 0);
    memset(pass, 0, sizeof(pass));   /* on n'oublie pas d'effacer le mdp de la pile */
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr(r, out);
    return ESP_OK;
}
static void cfg_set_if(nvs_handle_t h, cJSON *j, const char *field, const char *key) {
    const char *v = jstr(j, field);
    if (v) nvs_set_str(h, key, v);
}
static esp_err_t h_config_post(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    nvs_handle_t h;
    if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
        cfg_set_if(h, j, "device", "device");
        cfg_set_if(h, j, "wifi_ssid", "wifi_ssid"); cfg_set_if(h, j, "wifi_pass", "wifi_pass");
        cfg_set_if(h, j, "mqtt_uri", "mqtt_uri"); cfg_set_if(h, j, "mqtt_user", "mqtt_user");
        cfg_set_if(h, j, "mqtt_pass", "mqtt_pass");
        cJSON *lf = cJSON_GetObjectItem(j, "log_frames");
        if (cJSON_IsBool(lf) || cJSON_IsNumber(lf)) {
            uint8_t on = cJSON_IsTrue(lf) || (cJSON_IsNumber(lf) && lf->valuedouble != 0);
            nvs_set_u8(h, "log_frames", on);
            shutters_set_log_frames(on);   /* prise en compte immediate (le RX permanent s'active au reboot) */
        }
        cJSON *dbg = cJSON_GetObjectItem(j, "debug");
        if (cJSON_IsBool(dbg) || cJSON_IsNumber(dbg)) {
            uint8_t on = cJSON_IsTrue(dbg) || (cJSON_IsNumber(dbg) && dbg->valuedouble != 0);
            nvs_set_u8(h, "debug", on);
            cc1101_set_rx_debug(on);   /* switch DEBUG console : prise en compte immediate */
        }
        cJSON *rgj = cJSON_GetObjectItem(j, "rx_gain");
        if (cJSON_IsNumber(rgj)) {
            uint8_t rg = (uint8_t)rgj->valuedouble;   /* AGCCTRL2 : plafond de gain RX */
            nvs_set_u8(h, "rx_gain", rg);
            cc1101_set_rx_gain(rg);    /* pris en compte a la prochaine fenetre d'ecoute */
        }
        cJSON *tj = cJSON_GetObjectItem(j, "tx_te");
        if (cJSON_IsNumber(tj)) {
            uint32_t te = (uint32_t)tj->valuedouble;  /* TE d'emission (us) */
            nvs_set_u32(h, "tx_te", te);
            cc1101_set_tx_te(te);      /* pris en compte a la prochaine emission */
        }
        /* Mot de passe de l'UI. Trois cas DISTINCTS, d'ou le test sur cJSON_IsString
         * plutot que sur jstr() : champ ABSENT = on ne touche a rien (tout POST de
         * config ne doit pas effacer le mot de passe) ; chaine VIDE = desactivation
         * explicite demandee depuis l'UI ; sinon = nouveau mot de passe. */
        cJSON *up = cJSON_GetObjectItem(j, "ui_pass");
        if (cJSON_IsString(up)) {
            const char *v = up->valuestring;
            if (!v[0]) {
                nvs_erase_key(h, "ui_pass");   /* vide -> protection desactivee */
                ESP_LOGW(TAG, "mot de passe UI EFFACE : interface de nouveau ouverte");
            } else if (strlen(v) < UI_PASS_MAX) {
                nvs_set_str(h, "ui_pass", v);
                ESP_LOGW(TAG, "mot de passe UI defini : /api/* protege");
            }
            nvs_commit(h);
            ui_auth_reload();   /* prise en compte immediate, sans reboot */
            /* Tout changement de mot de passe invalide les sessions ouvertes :
             * sinon un navigateur deja connecte garderait l'acces alors que le
             * mot de passe qui l'avait autorise n'existe plus. */
            ui_sess_drop_all();
        }
        nvs_commit(h); nvs_close(h);
    }
    bool reboot = cJSON_IsTrue(cJSON_GetObjectItem(j, "reboot"));
    cJSON_Delete(j);
    httpd_resp_sendstr(r, "{\"ok\":1}");
    if (reboot) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
    return ESP_OK;
}

/* ── /api/frames : export du dataset slide (par telecommande : {hop, button, t} par trame), chunke.
 * hop = ciphertext C (brut) ; button + t (ordre = compteur) permettent de reconstruire le plaintext P. ── */
#define SH_MAX_HOPS_EXPORT 1024   /* aligne sur SH_MAX_HOPS (cap par telecommande) */
static esp_err_t h_frames(httpd_req_t *r) {
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=openprofalux-trames.json");
    static dframe_t fbuf[SH_MAX_HOPS_EXPORT];
    char serial[SH_SERIAL_LEN], name[SH_ID_LEN], line[600];
    httpd_resp_sendstr_chunk(r, "{\"trames\":{");
    int nr = shutters_remote_count();
    for (int i = 0; i < nr; i++) {
        int nh = shutters_remote_dump(i, serial, sizeof(serial), name, sizeof(name), fbuf, SH_MAX_HOPS_EXPORT);
        if (nh < 0) continue;
        int p = snprintf(line, sizeof(line), "%s\"%s\":{\"name\":\"%s\",\"count\":%d,\"frames\":[",
                         i ? "," : "", serial, name, nh);
        httpd_resp_send_chunk(r, line, p);
        for (int k = 0; k < nh; ) {
            p = 0;
            while (k < nh && p < (int)sizeof(line) - 64) {
                p += snprintf(line + p, sizeof(line) - p, "%s{\"hop\":\"0x%08X\",\"button\":\"0x%X\",\"t\":%u}",
                              k ? "," : "", (unsigned)fbuf[k].hop, fbuf[k].button, (unsigned)fbuf[k].t);
                k++;
            }
            httpd_resp_send_chunk(r, line, p);
        }
        httpd_resp_sendstr_chunk(r, "]}");
    }
    httpd_resp_sendstr_chunk(r, "}}");
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

/* ── /api/rf?offset=&limit= : trames du ring triees par date (recentes d'abord), paginees ── */
#define RF_MAX_ITEMS 1000
typedef struct { char serial[SH_SERIAL_LEN]; uint32_t hop, t; uint8_t button; int8_t rssi; } rfitem_t;
static int rf_cmp(const void *a, const void *b) {   /* t decroissant (plus recent d'abord) */
    uint32_t ta = ((const rfitem_t *)a)->t, tb = ((const rfitem_t *)b)->t;
    return (tb > ta) - (tb < ta);
}
static esp_err_t h_rf(httpd_req_t *req) {
    int offset = 0, limit = 50;
    char q[80], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "offset", v, sizeof(v)) == ESP_OK) offset = atoi(v);
        if (httpd_query_key_value(q, "limit", v, sizeof(v)) == ESP_OK) limit = atoi(v);
    }
    if (limit < 1) limit = 1;
    if (limit > 100) limit = 100;
    if (offset < 0) offset = 0;
    /* collecte toutes les trames du ring puis tri par date (le ring peut etre desordonne apres repeuplement MQTT) */
    static rfitem_t items[RF_MAX_ITEMS];
    int cap = shutters_rf_capacity(); if (cap > RF_MAX_ITEMS) cap = RF_MAX_ITEMS;
    int n = 0;
    for (int k = 0; k < cap; k++)
        if (shutters_rf_get(k, items[n].serial, sizeof(items[n].serial), &items[n].button, &items[n].hop, &items[n].t, &items[n].rssi) == 0) n++;
    qsort(items, n, sizeof(rfitem_t), rf_cmp);
    httpd_resp_set_type(req, "application/json");
    char line[176];
    int p = snprintf(line, sizeof(line), "{\"total\":%d,\"offset\":%d,\"frames\":[", n, offset);
    httpd_resp_send_chunk(req, line, p);
    for (int i = offset, c = 0; i < n && c < limit; i++, c++) {
        rfitem_t *it = &items[i];
        p = snprintf(line, sizeof(line),
            "%s{\"serial\":\"%s\",\"button\":\"%X\",\"hop\":\"%08X\",\"rssi\":%d,\"t\":%u}",
            c ? "," : "", it->serial, it->button, (unsigned)it->hop, it->rssi, (unsigned)it->t);
        httpd_resp_send_chunk(req, line, p);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ── /api/backup (export) + /api/restore (import) : telecommandes + trames de reference ── */
static esp_err_t h_backup(httpd_req_t *r) {
    static char buf[8192];
    int n = shutters_export_json(buf, sizeof(buf));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=openprofalux-backup.json");
    return httpd_resp_send(r, buf, n > 0 ? n : 0);
}
static esp_err_t h_restore(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    int rc = shutters_import_json(body);
    free(body);
    httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
    return ESP_OK;
}

/* ── /api/mqtt/discover : cherche un broker MQTT en mDNS (_mqtt._tcp) ── */
static esp_err_t h_mqtt_discover(httpd_req_t *r) {
    mdns_result_t *res = NULL;
    char out[128] = "{}";
    bool found = false;
    if (mdns_query_ptr("_mqtt", "_tcp", 3000, 5, &res) == ESP_OK) {
        for (mdns_result_t *cur = res; cur && !found; cur = cur->next) {
            for (mdns_ip_addr_t *a = cur->addr; a && !found; a = a->next) {
                if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                    char ip[16]; esp_ip4addr_ntoa(&a->addr.u_addr.ip4, ip, sizeof(ip));
                    snprintf(out, sizeof(out), "{\"uri\":\"mqtt://%s:%u\"}", ip, cur->port);
                    found = true;
                }
            }
        }
    }
    if (res) mdns_query_results_free(res);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr(r, out);
    return ESP_OK;
}

/* ── /api/ota/status + /api/ota/upload ── */
static esp_err_t h_ota_status(httpd_req_t *r) {
    ota_status_t st; ota_get_status(&st);
    char out[384];   /* message[128] + version[64] + target + scaffolding JSON */
    snprintf(out, sizeof(out), "{\"state\":%d,\"total\":%u,\"written\":%u,\"msg\":\"%s\",\"version\":\"%s\",\"target\":\"" TARGET_NAME "\"}",
             st.state, (unsigned)st.total_bytes, (unsigned)st.written_bytes, st.message, st.active_version);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr(r, out);
    return ESP_OK;
}
static esp_err_t h_ota_upload(httpd_req_t *r) {
    int total = r->content_len;
    if (total <= 0) return httpd_resp_send_err(r, 400, "empty");
    if (ota_upload_begin(total) != ESP_OK) return httpd_resp_send_err(r, 500, "ota begin");
    wifi_ps_type_t ps = WIFI_PS_MIN_MODEM; esp_wifi_get_ps(&ps); esp_wifi_set_ps(WIFI_PS_NONE);
    char *buf = malloc(1460);
    if (!buf) { esp_wifi_set_ps(ps); ota_upload_abort(); return httpd_resp_send_err(r, 500, "malloc"); }
    int rem = total, cc = 0; esp_err_t res = ESP_OK;
    while (rem > 0) {
        int k = httpd_req_recv(r, buf, MIN(rem, 1460));
        if (k == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (k <= 0 || ota_upload_data(buf, k) != ESP_OK) { res = ESP_FAIL; break; }
        rem -= k;
        if ((++cc & 0x0F) == 0) vTaskDelay(1);
    }
    esp_wifi_set_ps(ps); free(buf);
    if (res != ESP_OK) { ota_upload_abort(); return httpd_resp_send_err(r, 500, "upload"); }
    httpd_resp_sendstr(r, "{\"ok\":1}");
    ota_upload_end();   /* vérifie + reboot */
    return ESP_OK;
}
/* ── /api/ota/rollback : rebascule sur la partition precedente (comme OpenXtraflame) ── */
static esp_err_t h_ota_rollback(httpd_req_t *r) {
    /* On repond AVANT : ota_rollback() reboote immediatement si l'etat le permet. */
    httpd_resp_sendstr(r, "{\"ok\":1}");
    vTaskDelay(pdMS_TO_TICKS(300));
    ota_rollback();   /* esp_ota_mark_app_invalid_rollback_and_reboot : reboote, ou echoue si pas d'image precedente valide */
    return ESP_OK;
}

/* ── /api/ota/pull : telecharge + flashe une image depuis une URL HTTPS (release GitHub) ── */
static void ota_pull_bg_task(void *arg) {
    char *url = (char *)arg;
    esp_err_t err = ota_pull_from_url(url);
    if (err != ESP_OK) ESP_LOGE(TAG, "ota_pull_from_url(%s) KO: %s", url, esp_err_to_name(err));
    free(url);
    vTaskDelete(NULL);
}
static esp_err_t h_ota_pull(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    const char *url = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "url")) : NULL;
    char *dup = (url && !strncmp(url, "https://", 8)) ? strdup(url) : NULL;   /* HTTPS uniquement */
    if (j) cJSON_Delete(j);
    httpd_resp_set_type(r, "application/json");
    if (dup && xTaskCreate(ota_pull_bg_task, "ota_pull", 8192, dup, 5, NULL) == pdPASS)
        return httpd_resp_sendstr(r, "{\"ok\":1}");
    free(dup);
    return httpd_resp_sendstr(r, "{\"ok\":0}");
}

/* ── /api/rx/calibrate : auto-calibrage du gain RX ──
 * POST lance le balayage (l'utilisateur appuie sur sa telecommande), GET renvoie l'etat. */
static esp_err_t h_rx_calibrate(httpd_req_t *r) {
    int rc = radio_calibrate_start();
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : (rc == 1 ? "{\"ok\":1,\"busy\":1}" : "{\"ok\":0}"));
}
static esp_err_t h_rx_calibrate_status(httpd_req_t *r) {
    int st = 0; uint8_t testing = 0, result = 0;
    radio_calibrate_status(&st, &testing, &result);
    char out[96];
    snprintf(out, sizeof(out), "{\"state\":%d,\"testing\":%d,\"result\":%d}", st, testing, result);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}

/* ── /api/diag : diagnostic radio (puce + self-test TX) pour l'UI ── */
static esp_err_t h_diag(httpd_req_t *r) {
    int tx_ok = -1; uint8_t partnum = 0xFF, version = 0xFF;
    cc1101_get_diag(&tx_ok, &partnum, &version);
    char out[128];
    snprintf(out, sizeof(out), "{\"tx_ok\":%d,\"partnum\":%d,\"version\":%d}", tx_ok, partnum, version);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}
static esp_err_t h_diag_tx(httpd_req_t *r) {   /* re-lance le self-test TX a la demande */
    int rc = radio_tx_selftest();
    int tx_ok = -1; cc1101_get_diag(&tx_ok, NULL, NULL);
    char out[48];
    snprintf(out, sizeof(out), "{\"ok\":%d,\"tx_ok\":%d}", rc == 0 ? 1 : 0, tx_ok);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}

/* ── /api/pfx : enrôlement d'une identité virtuelle 0x067 ── */
/* Capture RX de la vraie télécommande -> alloue/rappelle l'identité du moteur (tâche dédiée). */
static void pfx_capture_task(void *arg) {
    (void)arg;
    char bits[SH_BITS_LEN];
    ESP_LOGW(TAG, "PFX CAPTURE: ecoute RX 15s (appuie sur la telecommande)...");
    int n = radio_listen_once(15000, bits, SH_BITS_LEN - 1);   /* 15 s pour appuyer */
    ESP_LOGW(TAG, "PFX CAPTURE: radio_listen_once -> n=%d bits", n);
    if (n >= 60) {
        uint32_t rem = bits_lsb(bits, 32, 28);                 /* serial de la télécommande */
        bool isnew = false;
        s_pcap_rc     = pfx_enrol_capture(rem, s_pcap_model, &isnew);
        s_pcap_remote = rem;
        s_pcap_new    = isnew ? 1 : 0;
        ESP_LOGW(TAG, "PFX CAPTURE: trame captee serial=0x%07X -> rc=%d new=%d", (unsigned)rem, s_pcap_rc, s_pcap_new);
    } else {
        s_pcap_rc = -3;   /* aucune trame captée */
        s_pcap_remote = 0;
        ESP_LOGW(TAG, "PFX CAPTURE: aucune trame captee (n=%d)", n);
    }
    s_pcap_done = true; s_pcap_active = false;
    vTaskDelete(NULL);
}
static esp_err_t h_pfx_capture(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    cJSON *mo = cJSON_GetObjectItem(j, "model");
    s_pcap_model = cJSON_IsNumber(mo) ? (int)mo->valuedouble : -1;
    cJSON_Delete(j);
    ESP_LOGW(TAG, "PFX CAPTURE: requete recue (model=%d, active=%d)", s_pcap_model, s_pcap_active);
    if (!s_pcap_active) {
        s_pcap_done = false; s_pcap_rc = 0; s_pcap_remote = 0; s_pcap_new = 0; s_pcap_active = true;
        BaseType_t ok = xTaskCreate(pfx_capture_task, "pfxcap", 4096, NULL, 6, NULL);
        if (ok != pdPASS) { s_pcap_active = false; ESP_LOGE(TAG, "PFX CAPTURE: xTaskCreate ECHEC"); }
    }
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, "{\"ok\":1}");
}
static esp_err_t h_pfx_capture_poll(httpd_req_t *r) {
    pfx_ident_t id; pfx_enrol_get(&id);
    char out[144];
    snprintf(out, sizeof(out),
        "{\"active\":%d,\"done\":%d,\"rc\":%d,\"new\":%d,\"remote\":\"0x%07X\",\"serial\":\"0x%07X\",\"counter\":%d}",
        s_pcap_active ? 1 : 0, s_pcap_done ? 1 : 0, s_pcap_rc, s_pcap_new,
        (unsigned)s_pcap_remote, (unsigned)id.serial, id.counter);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}
static esp_err_t h_pfx_get(httpd_req_t *r) {
    pfx_ident_t id; bool active = pfx_enrol_get(&id);
    char out[720]; int p = 0;
    p += snprintf(out + p, sizeof(out) - p,
                  "{\"active\":%d,\"model\":%d,\"serial\":\"0x%07X\",\"idx\":%d,\"counter\":%d,\"remote\":\"0x%07X\",\"models\":[",
                  active ? 1 : 0, id.model, (unsigned)id.serial, id.idx, id.counter, (unsigned)id.remote);
    int nm = pfx_enrol_model_count();
    for (int i = 0; i < nm; i++) {
        const pfx_model_t *m = pfx_enrol_model(i);
        p += snprintf(out + p, sizeof(out) - p, "%s{\"name\":\"%s\",\"te\":%d,\"routine\":\"%s\"}",
                      i ? "," : "", m->name, m->te_us, m->routine);
    }
    snprintf(out + p, sizeof(out) - p, "]}");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}
static esp_err_t h_pfx_learn(httpd_req_t *r) {
    int rc = pfx_enrol_emit_learn();
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
}
static esp_err_t h_pfx_cmd(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    int rc = pfx_enrol_cmd(jstr(j, "cmd"));
    cJSON_Delete(j);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
}
static esp_err_t h_pfx_forget(httpd_req_t *r) {
    pfx_enrol_forget();
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, "{\"ok\":1}");
}
/* Enregistre l'identite virtuelle enrolee comme volet (onglet Volets + cover HA). */
/* Cree une centrale : {id, members:[id1,id2,...]} -> diffuse aux volets membres. */
static esp_err_t h_central(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    const char *id = jstr(j, "id");
    char csv[SH_MEMBERS_LEN] = ""; int p = 0;
    cJSON *mem = cJSON_GetObjectItem(j, "members"), *m;
    if (cJSON_IsArray(mem)) cJSON_ArrayForEach(m, mem) {
        const char *s = cJSON_GetStringValue(m);
        if (s && *s && p < (int)sizeof(csv) - 1) p += snprintf(csv + p, sizeof(csv) - p, "%s%s", p ? "," : "", s);
    }
    int rc = (id && *id) ? shutters_create_central(id, csv) : -1;
    cJSON_Delete(j);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
}
static esp_err_t h_pfx_save_volet(httpd_req_t *r) {
    char *body = read_body(r); if (!body) return httpd_resp_send_err(r, 400, "body");
    cJSON *j = cJSON_Parse(body); free(body);
    if (!j) return httpd_resp_send_err(r, 400, "json");
    const char *name = jstr(j, "id");
    pfx_ident_t id; bool active = pfx_enrol_get(&id);
    int rc = -1;
    if (active && name && *name)
        rc = shutters_create_virtual(name, id.serial, id.counter, pfx_enrol_model_te(id.model));
    if (rc == 0) pfx_enrol_forget();   /* l'identite vit desormais dans le volet */
    cJSON_Delete(j);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, rc == 0 ? "{\"ok\":1}" : "{\"ok\":0}");
}

/* ── /api/login + /api/logout + /api/session ────────────────────────────────
 * Ces trois routes sont enregistrees en ACCES LIBRE (reg_open) : exiger une
 * session pour pouvoir en ouvrir une serait circulaire. Elles ne divulguent
 * rien — /api/session ne dit que si une protection existe et si l'appelant
 * est deja connecte. */
static esp_err_t h_login(httpd_req_t *r) {
    char *body = read_body(r);
    httpd_resp_set_type(r, "application/json");
    if (!body) return httpd_resp_sendstr(r, "{\"ok\":0}");
    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return httpd_resp_sendstr(r, "{\"ok\":0}");
    const char *p = jstr(j, "pass");

    bool ok = s_ui_pass[0] && p && ui_secret_equal(s_ui_pass, p);
    cJSON_Delete(j);

    if (!s_ui_pass[0]) return httpd_resp_sendstr(r, "{\"ok\":1,\"open\":1}");   /* pas de protection */
    if (!ok) {
        /* Freine le bourrage de mots de passe : ~1 essai/s max par requete.
         * Suffisant ici (LAN, mot de passe choisi par l'utilisateur) et sans
         * etat a maintenir par IP. */
        vTaskDelay(pdMS_TO_TICKS(1000));
        httpd_resp_set_status(r, "401 Unauthorized");
        return httpd_resp_sendstr(r, "{\"ok\":0,\"err\":\"bad_pass\"}");
    }
    char tok[UI_TOKEN_LEN + 1];
    ui_sess_create(tok);
    char ck[128];
    /* HttpOnly : le jeton n'est pas lisible par du JS (limite le vol par XSS).
     * SameSite=Lax : pas d'envoi sur une requete cross-site (anti-CSRF de base).
     * Pas de Secure : l'UI est en HTTP simple, le cookie ne partirait jamais. */
    snprintf(ck, sizeof(ck), "opfx_sess=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax", tok, UI_SESS_TTL_S);
    httpd_resp_set_hdr(r, "Set-Cookie", ck);
    ESP_LOGI(TAG, "connexion UI reussie (session ouverte)");
    return httpd_resp_sendstr(r, "{\"ok\":1}");
}

static esp_err_t h_logout(httpd_req_t *r) {
    char tok[UI_TOKEN_LEN + 1] = "";
    if (ui_cookie_get(r, tok, sizeof(tok))) ui_sess_drop(tok);
    httpd_resp_set_hdr(r, "Set-Cookie", "opfx_sess=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, "{\"ok\":1}");
}

/* Etat d'authentification, consulte par l'UI au chargement pour decider
 * d'afficher la page de login ou l'application. */
static esp_err_t h_session(httpd_req_t *r) {
    char tok[UI_TOKEN_LEN + 1] = "";
    bool authed = !s_ui_pass[0] || (ui_cookie_get(r, tok, sizeof(tok)) && ui_sess_valid(tok));
    char out[64];
    snprintf(out, sizeof(out), "{\"protected\":%d,\"authed\":%d}", s_ui_pass[0] ? 1 : 0, authed ? 1 : 0);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}

/* Wrapper d'authentification : toute route passee a reg() est protegee via ce
 * trampoline. Centraliser ici garantit qu'aucune route ajoutee plus tard ne soit
 * oubliee — c'est le seul endroit ou les handlers sont enregistres. Les assets
 * statiques (/, style.css, app.js) passent par reg_open() : le navigateur doit
 * pouvoir afficher la page qui DEMANDE le mot de passe. */
#define MAX_ROUTES 48
static esp_err_t (*s_route_h[MAX_ROUTES])(httpd_req_t *);
static int s_nroutes = 0;

static esp_err_t auth_trampoline(httpd_req_t *r) {
    int idx = (int)(intptr_t)r->user_ctx;
    if (idx < 0 || idx >= s_nroutes) return ESP_FAIL;
    if (!ui_auth_ok(r)) return ESP_OK;   /* 401 deja envoye */
    return s_route_h[idx](r);
}

/* Enregistre une route SANS authentification (assets statiques uniquement). */
static void reg_open(httpd_handle_t s, const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *)) {
    httpd_uri_t u = { .uri = uri, .method = m, .handler = h };
    esp_err_t e = httpd_register_uri_handler(s, &u);
    if (e != ESP_OK) ESP_LOGE(TAG, "route NON enregistree: %s (%s) -> augmenter max_uri_handlers", uri, esp_err_to_name(e));
}

/* Enregistre une route PROTEGEE (toutes les routes sous "/api/"). */
static void reg(httpd_handle_t s, const char *uri, httpd_method_t m, esp_err_t (*h)(httpd_req_t *)) {
    if (s_nroutes >= MAX_ROUTES) { ESP_LOGE(TAG, "route NON enregistree: %s (table pleine)", uri); return; }
    s_route_h[s_nroutes] = h;
    httpd_uri_t u = { .uri = uri, .method = m, .handler = auth_trampoline,
                      .user_ctx = (void *)(intptr_t)s_nroutes };
    esp_err_t e = httpd_register_uri_handler(s, &u);
    if (e != ESP_OK) ESP_LOGE(TAG, "route NON enregistree: %s (%s) -> augmenter max_uri_handlers", uri, esp_err_to_name(e));
    else s_nroutes++;
}

void web_ui_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 56;   /* marge > nb de routes (38 : 35 protegees + 3 assets). Si trop bas, les dernieres
                                    routes echouent EN SILENCE (ex /api/restore mort a 28). reg() loggue
                                    desormais les echecs pour ne plus jamais rater ca. */
    cfg.stack_size = 6144;   /* esp_ota_end() consomme la pile en fin d'upload */
    /* ROOT CAUSE "web UI morte au bout d'un moment" : sans lru_purge, une fois les
     * max_open_sockets pris (SPA qui poll en keep-alive), le httpd REFUSE toute nouvelle
     * connexion au lieu de recycler -> port 80 en connection-refused (MQTT non touche).
     * On recycle la socket la moins recente + un peu plus de sockets. */
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;   /* max = CONFIG_LWIP_MAX_SOCKETS(10) - 3 */
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t s = NULL;
    if (httpd_start(&s, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start KO"); return; }
    ui_auth_reload();   /* charge le mot de passe UI (vide = UI ouverte) */
    /* Assets statiques NON proteges : sans eux le navigateur ne peut pas afficher
     * la page qui demande le mot de passe. Ils ne divulguent aucune donnee. */
    reg_open(s, "/",          HTTP_GET, h_index);
    reg_open(s, "/style.css", HTTP_GET, h_css);
    reg_open(s, "/app.js",    HTTP_GET, h_js);
    /* Routes de session : libres par necessite (voir commentaire plus haut). */
    reg_open(s, "/api/login",   HTTP_POST, h_login);
    reg_open(s, "/api/logout",  HTTP_POST, h_logout);
    reg_open(s, "/api/session", HTTP_GET,  h_session);
    reg(s, "/api/status",  HTTP_GET,  h_status);
    reg(s, "/api/shutter", HTTP_POST, h_shutter);
    reg(s, "/api/volet/delete", HTTP_POST, h_volet_delete);
    reg(s, "/api/learn/start",  HTTP_POST, h_learn_start);
    reg(s, "/api/learn/poll",   HTTP_GET,  h_learn_poll);
    reg(s, "/api/learn/assign", HTTP_POST, h_learn_assign);
    reg(s, "/api/learn/reassign", HTTP_POST, h_learn_reassign);
    reg(s, "/api/learn/adopt", HTTP_POST, h_learn_adopt);
    reg(s, "/api/rf/replay", HTTP_POST, h_rf_replay);
    reg(s, "/api/calibrate", HTTP_POST, h_calibrate);
    reg(s, "/api/volet/orientation", HTTP_POST, h_orientation);
    reg(s, "/api/remote",    HTTP_POST, h_remote);
    reg(s, "/api/config",    HTTP_GET,  h_config_get);
    reg(s, "/api/config",    HTTP_POST, h_config_post);
    reg(s, "/api/ota/status",   HTTP_GET,  h_ota_status);
    reg(s, "/api/ota/upload",   HTTP_POST, h_ota_upload);
    reg(s, "/api/ota/rollback", HTTP_POST, h_ota_rollback);
    reg(s, "/api/ota/pull",     HTTP_POST, h_ota_pull);
    reg(s, "/api/frames",       HTTP_GET,  h_frames);
    reg(s, "/api/rf",           HTTP_GET,  h_rf);
    reg(s, "/api/rx/calibrate", HTTP_POST, h_rx_calibrate);
    reg(s, "/api/rx/calibrate", HTTP_GET,  h_rx_calibrate_status);
    reg(s, "/api/diag",         HTTP_GET,  h_diag);
    reg(s, "/api/diag/tx",      HTTP_POST, h_diag_tx);
    reg(s, "/api/backup",       HTTP_GET,  h_backup);
    reg(s, "/api/restore",      HTTP_POST, h_restore);
    reg(s, "/api/mqtt/discover", HTTP_GET, h_mqtt_discover);
    reg(s, "/api/pfx",        HTTP_GET,  h_pfx_get);
    reg(s, "/api/pfx/capture", HTTP_POST, h_pfx_capture);
    reg(s, "/api/pfx/capture/poll", HTTP_GET, h_pfx_capture_poll);
    reg(s, "/api/pfx/learn",  HTTP_POST, h_pfx_learn);
    reg(s, "/api/pfx/cmd",    HTTP_POST, h_pfx_cmd);
    reg(s, "/api/pfx/forget", HTTP_POST, h_pfx_forget);
    reg(s, "/api/pfx/save_volet", HTTP_POST, h_pfx_save_volet);
    reg(s, "/api/central", HTTP_POST, h_central);
    ESP_LOGI(TAG, "HTTP UI demarree");
}
