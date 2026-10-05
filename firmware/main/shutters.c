/*
 * shutters.c — modele cover OpenProfalux (clone/replay + position time-based).
 * Config persistee en NVS, un document JSON par volet (voir cfg_store.h). Le
 * dataset de trames et le ring RF vivent en SPIFFS (partition "storage").
 */
#include "shutters.h"
#include "cfg_store.h"
#include "cfg_model.h"
#include "pfx_enrol.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "cc1101.h"
#include "radio.h"
#include "hardware_config.h"
#include "mqtt_bridge.h"
#include "ota.h"
#include "esp_app_desc.h"   /* esp_app_get_description()->version = version installee */
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "shutters";
#define TICK_MS 150
#define SH_MAX_HOPS 1024   /* set de hops distincts memorises par serial (4 Ko/serial max) */

/* volet_t et remote_t : definis dans cfg_model.h (modele persistant + serialisation). */
typedef struct { uint32_t t; char serial[SH_SERIAL_LEN]; uint8_t button; uint32_t hop; int8_t rssi; } rfrec_t;
/* LEGER (pas de bits stockes) : les 66 bits sont RECONSTRUITS a la demande depuis serial+bouton+hop
 * (build_frame_bits) pour le rejeu et l'adoption -> permet un ring de 1000 sans exploser la RAM. */

static volet_t  s_volets[SH_MAX_VOLETS];
static int      s_nvolets = 0;
static remote_t s_remotes[SH_MAX_REMOTES];
static int      s_nremotes = 0;
#define RF_RING 1000          /* trames a l'affichage (paginees) ; l'export /api/frames = l'ensemble du dataset */
#define STATUS_RF_SHOW 20     /* nb de trames recentes mises dans /api/status (leger, poll 3 s) ; l'onglet RF pagine via /api/rf */
static rfrec_t  s_rf[RF_RING];
static int      s_rfhead = 0;
static SemaphoreHandle_t s_lock;
static char     s_device[32] = "op";   /* nom appareil (prefixe topics HA) */
static bool     s_mqtt_ready = false;   /* true apres shutters_mqtt_announce() */
static bool     s_log_frames = false;   /* publie toutes les trames captees en MQTT */
/* La config persistante, telle que cfg_model.c la lit et l'ecrit. */
static const cfg_model_t s_model = { &s_log_frames, s_remotes, &s_nremotes, s_volets, &s_nvolets };
static bool     s_frames_dirty = false; /* dataset modifie -> a resauvegarder (SPIFFS) */
static bool     s_ring_dirty = false;    /* ring RF modifie -> a resauvegarder (SPIFFS) */
static bool     s_pos_dirty = false;     /* position d'un volet a changer/figer -> sauver la cfg (retenir la position au reboot) */
#define FRAMES_MAX 256                  /* hops distincts persistes (4 Ko, en SPIFFS). Le gros dataset slide s'accumule cote MQTT/HA. */

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* ── Publication MQTT DIFFEREE (anti-deadlock) ──────────────────────────────
 * REGLE ABSOLUE : ne JAMAIS appeler mqtt_pub_raw sous le LOCK. Un handler MQTT
 * entrant (cover/set, set_position...) reprend ce meme LOCK ; publier sous LOCK
 * peut bloquer (outbox pleine, ou la tache MQTT est justement bloquee) -> attente
 * mutuelle = deadlock (httpd/tick/commandes tous figes). On EMPILE le message sous
 * LOCK (malloc seulement, jamais bloquant) puis on PUBLIE apres le UNLOCK via
 * pub_flush(). Voir aussi shutters_on_rx (meme principe, deja applique). */
typedef struct pubmsg { struct pubmsg *next; int qos, retain; char *topic; char *payload; } pubmsg_t;
static pubmsg_t *s_pub_head, *s_pub_tail;   /* file simple, touchee UNIQUEMENT sous LOCK */

/* Empile une publication. Appele SOUS LOCK. Copie topic+payload sur le tas. */
static void pub_defer(const char *topic, const char *payload, int qos, int retain) {
    pubmsg_t *m = malloc(sizeof(*m));
    if (!m) return;
    m->next = NULL; m->qos = qos; m->retain = retain;
    m->topic = strdup(topic); m->payload = strdup(payload);
    if (!m->topic || !m->payload) { free(m->topic); free(m->payload); free(m); return; }
    if (s_pub_tail) s_pub_tail->next = m; else s_pub_head = m;
    s_pub_tail = m;
}

/* Vide la file en publiant HORS LOCK. A appeler APRES chaque UNLOCK qui a empile
 * (jamais en tenant le LOCK : pub_flush reprend le LOCK pour detacher la file). */
static void pub_flush(void) {
    if (!s_pub_head) return;                 /* rapide : rien a publier (course benigne) */
    LOCK(); pubmsg_t *m = s_pub_head; s_pub_head = s_pub_tail = NULL; UNLOCK();
    while (m) {
        pubmsg_t *n = m->next;
        mqtt_pub_raw(m->topic, m->payload, m->qos, m->retain);
        free(m->topic); free(m->payload); free(m);
        m = n;
    }
}

static void update_listening(void);   /* (defini plus bas) : allume le RX si volet appris OU option capture */

static volet_t *find_volet(const char *id) {
    for (int i = 0; i < s_nvolets; i++) if (!strcmp(s_volets[i].id, id)) return &s_volets[i];
    return NULL;
}
static volet_t *get_or_create(const char *id) {
    volet_t *v = find_volet(id);
    if (v) return v;
    if (s_nvolets >= SH_MAX_VOLETS) return NULL;
    /* Pas de virgule dans un NOUVEAU nom : les membres d'une centrale (et l'ordre
     * d'affichage) sont des listes d'ids separes par des virgules. Coupe en deux,
     * le nom n'y designerait plus le volet, qui ne recevrait jamais les commandes
     * de sa centrale. Un volet existant qui en a une reste utilisable. */
    if (strchr(id, ',')) { ESP_LOGW(TAG, "volet '%s' refuse : virgule dans le nom", id); return NULL; }
    v = &s_volets[s_nvolets++];
    memset(v, 0, sizeof(*v));
    strlcpy(v->id, id, SH_ID_LEN);
    v->position = 50; v->target = -1; v->pub_pos = -1; v->pub_dir = -9; v->orientation = -1;
    v->order = -1;   /* non range : l'UI l'affiche a la fin, dans l'ordre de creation */
    return v;
}

/* nom volet -> slug HA-safe ([a-zA-Z0-9_-]) : espaces/accents interdits dans les topics
 * (decouverte HA ET command/state), sinon commandes/decouverte cassees. */
static void slugify(char *dst, int cap, const char *src) {
    int p = 0;
    for (int k = 0; src[k] && p < cap - 1; k++) {
        char c = src[k];
        dst[p++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? c : '_';
    }
    dst[p] = 0;
    if (!p && cap > 6) strlcpy(dst, "volet", cap);
}
static volet_t *find_volet_by_slug(const char *slug) {
    char s[SH_ID_LEN];
    for (int i = 0; i < s_nvolets; i++) { slugify(s, sizeof(s), s_volets[i].id); if (!strcmp(s, slug)) return &s_volets[i]; }
    return NULL;
}

/* ── Accesseurs dataset (export des trames captees : hops distincts par telecommande) ── */
int shutters_remote_count(void) { LOCK(); int n = s_nremotes; UNLOCK(); return n; }
int shutters_remote_dump(int i, char *serial, int sser, char *name, int sname, dframe_t *frames, int maxframes) {
    LOCK();
    if (i < 0 || i >= s_nremotes) { UNLOCK(); return -1; }
    strlcpy(serial, s_remotes[i].serial, sser);
    strlcpy(name, s_remotes[i].name, sname);
    int nh = s_remotes[i].nhops; if (nh > maxframes) nh = maxframes;
    if (nh > 0 && s_remotes[i].hops) memcpy(frames, s_remotes[i].hops, (size_t)nh * sizeof(dframe_t));
    UNLOCK();
    return nh < 0 ? 0 : nh;
}

/* ── Persistance JSON <-> NVS ── */
/* La serialisation et le rangement sont dans cfg_model.c / cfg_store.c. Les
 * echecs sont journalises : une NVS pleine faisait autrefois echouer TOUTES les
 * sauvegardes sans le moindre message. */
static esp_err_t save_cfg(void) {
    char key[8] = "";
    esp_err_t e = cfg_model_save(&s_model, key);
    if (e != ESP_OK)
        ESP_LOGE(TAG, "save_cfg ECHEC sur '%s' : %s (%d volets)", key, esp_err_to_name(e), s_nvolets);
    return e;
}
/* Persistance BORNEE du dataset de trames (hops distincts) : survit au reboot, mais
 * plafonnee (FRAMES_MAX). Le GROS dataset (65536) doit vivre cote HA (recorder).
 * Sauvegarde periodique (pas a chaque trame -> menage la flash).
 *
 * En SPIFFS, a cote du ring RF. Il vivait en NVS ("framesv2", jusqu'a 4 Ko) : le plus
 * gros occupant d'une NVS de 16 Ko qui saturait. Donnee NON critique (jeu de trames
 * pour la recherche) : une coupure en pleine ecriture n'en perd au pire que la fin. */
#define FRAMES_FILE "/spiffs/frames.bin"
static void save_frames(void) {
    static uint32_t buf[FRAMES_MAX * 4];   /* records {serial, hop, t, bouton} */
    int n = 0;
    LOCK();
    for (int i = 0; i < s_nremotes && n < FRAMES_MAX; i++) {
        uint32_t ser = (uint32_t)strtoul(s_remotes[i].serial, NULL, 16);
        for (int k = 0; k < s_remotes[i].nhops && n < FRAMES_MAX; k++) {
            dframe_t *d = &s_remotes[i].hops[k];
            buf[n * 4] = ser; buf[n * 4 + 1] = d->hop; buf[n * 4 + 2] = d->t; buf[n * 4 + 3] = d->button; n++;
        }
    }
    s_frames_dirty = false;
    UNLOCK();
    FILE *f = fopen(FRAMES_FILE, "wb");
    if (!f) { ESP_LOGW(TAG, "save_frames: fopen KO"); return; }
    fwrite(buf, 1, (size_t)n * 4 * sizeof(uint32_t), f);
    fclose(f);
}
/* Au boot, apres load_cfg (rattache les hops aux telecommandes). Renvoie true si
 * le dataset est en fichier : son ancienne copie NVS peut alors etre effacee. */
static bool load_frames(void) {
    static uint32_t buf[FRAMES_MAX * 4];
    size_t sz = 0;
    /* Fichier, sinon ancienne copie NVS rangee en fichier. La copie NVS n'est
     * effacee (par cfg_store_finish_boot) qu'une fois le fichier en place. */
    bool in_file = cfg_frames_load(FRAMES_FILE, buf, sizeof(buf), &sz);
    if (sz < 16) return in_file;
    int n = sz / (4 * sizeof(uint32_t));
    for (int i = 0; i < n; i++) {
        char shex[SH_SERIAL_LEN]; snprintf(shex, sizeof(shex), "0x%07X", (unsigned)buf[i * 4]);
        remote_t *rm = NULL;
        for (int j = 0; j < s_nremotes; j++) if (!strcmp(s_remotes[j].serial, shex)) { rm = &s_remotes[j]; break; }
        if (!rm && s_nremotes < 16) { rm = &s_remotes[s_nremotes++]; memset(rm, 0, sizeof(*rm)); strlcpy(rm->serial, shex, SH_SERIAL_LEN); }
        if (!rm) continue;
        if (rm->nhops >= rm->caphops && rm->caphops < SH_MAX_HOPS) {
            uint16_t nc = rm->caphops ? (uint16_t)(rm->caphops * 2) : 32; if (nc > SH_MAX_HOPS) nc = SH_MAX_HOPS;
            dframe_t *np = realloc(rm->hops, (size_t)nc * sizeof(dframe_t));
            if (np) { rm->hops = np; rm->caphops = nc; }
        }
        if (rm->nhops < rm->caphops) rm->hops[rm->nhops++] = (dframe_t){ .hop = buf[i * 4 + 1], .t = buf[i * 4 + 2], .button = (uint8_t)buf[i * 4 + 3] };
    }
    return in_file;
}

/* Reconstruit la trame 66 bits a partir de serial(28)+bouton(4)+hop(32) : format deterministe
 * (radio.c : hop=bits[0..31], serial=bits[32..59], bouton=bits[60..63], LSB-first). Les 2 bits
 * de statut (VLOW/RPT, bits[64..65]) ne sont pas transmis -> 0 (sans effet sur le rejeu). */
static void build_frame_bits(char *out, uint32_t serial, uint8_t button, uint32_t hop) {
    for (int i = 0; i < 32; i++) out[i]      = ((hop    >> i) & 1u) ? '1' : '0';
    for (int i = 0; i < 28; i++) out[32 + i] = ((serial >> i) & 1u) ? '1' : '0';
    for (int i = 0; i < 4;  i++) out[60 + i] = ((button >> i) & 1u) ? '1' : '0';
    out[64] = '0'; out[65] = '0'; out[66] = 0;
}

/* Persistance BORNEE du ring RF en NVS : on ne stocke que les METADONNEES {serial,bouton,hop,t,rssi}
 * (16 o/trame) et on RECONSTRUIT les bits au boot -> les trames restent REJOUABLES apres reboot/flash. */
typedef struct { uint32_t serial, hop, t; uint8_t button; int8_t rssi; uint16_t _pad; } ringrec_t;
#define RING_FILE "/spiffs/rfring.bin"
static ringrec_t s_ringbuf[RF_RING];   /* buffer partage save/load (hors pile, 1 seule copie) */
static void spiffs_mount(void) {   /* la NVS (16 Ko) ne tient pas le ring 300 -> SPIFFS (partition storage, 960 Ko) */
    esp_vfs_spiffs_conf_t c = { .base_path = "/spiffs", .partition_label = "storage", .max_files = 4, .format_if_mount_failed = true };
    esp_err_t e = esp_vfs_spiffs_register(&c);
    ESP_LOGI(TAG, "spiffs mount: %s", esp_err_to_name(e));
}
/* Le (serial, hop) est-il deja dans le ring ? (dedup : une pression = ~10 repetitions du meme hop).
 * Le hop encode le bouton -> (serial, hop) identique => meme bouton. Caller detient le LOCK (ou boot). */
static bool ring_contains(const char *shex, uint32_t hop) {
    for (int k = 0; k < RF_RING; k++)
        if (s_rf[k].serial[0] && s_rf[k].hop == hop && !strcmp(s_rf[k].serial, shex)) return true;
    return false;
}
static void save_ring(void) {
    int head;
    LOCK();
    head = s_rfhead;
    for (int i = 0; i < RF_RING; i++) {
        rfrec_t *r = &s_rf[i];
        s_ringbuf[i].serial = r->serial[0] ? (uint32_t)strtoul(r->serial, NULL, 16) : 0;
        s_ringbuf[i].hop = r->hop; s_ringbuf[i].t = r->t; s_ringbuf[i].button = r->button; s_ringbuf[i].rssi = r->rssi; s_ringbuf[i]._pad = 0;
    }
    s_ring_dirty = false;
    UNLOCK();
    FILE *f = fopen(RING_FILE, "wb");
    if (!f) { ESP_LOGW(TAG, "save_ring: fopen KO"); return; }
    fwrite(&head, sizeof(head), 1, f);
    size_t w = fwrite(s_ringbuf, 1, sizeof(s_ringbuf), f);
    fclose(f);
    ESP_LOGI(TAG, "save_ring: %d o ecrits (head=%d)", (int)w, head);
}
static void load_ring(void) {   /* boot : restaure le ring + reconstruit les bits (trames rejouables) */
    FILE *f = fopen(RING_FILE, "rb");
    if (!f) { ESP_LOGW(TAG, "load_ring: pas de fichier (rien a charger)"); return; }
    int head = 0;
    if (fread(&head, sizeof(head), 1, f) != 1) { fclose(f); return; }
    size_t r = fread(s_ringbuf, 1, sizeof(s_ringbuf), f);
    fclose(f);
    (void)head;
    int n = r / sizeof(ringrec_t); if (n > RF_RING) n = RF_RING;
    int loaded = 0;   /* on compacte + on deduplique (nettoie d'eventuels doublons deja en SPIFFS) */
    for (int i = 0; i < n && loaded < RF_RING; i++) {
        if (!s_ringbuf[i].serial && !s_ringbuf[i].hop) continue;
        char shex[SH_SERIAL_LEN]; snprintf(shex, sizeof(shex), "0x%07X", (unsigned)s_ringbuf[i].serial);
        if (ring_contains(shex, s_ringbuf[i].hop)) continue;   /* dedup (serial, hop) */
        rfrec_t *rr = &s_rf[loaded];
        strlcpy(rr->serial, shex, SH_SERIAL_LEN);
        rr->hop = s_ringbuf[i].hop; rr->t = s_ringbuf[i].t; rr->button = s_ringbuf[i].button; rr->rssi = s_ringbuf[i].rssi;
        loaded++;
    }
    s_rfhead = loaded % RF_RING;   /* prochaine place libre apres compactage */
    ESP_LOGI(TAG, "load_ring: %d trames rechargees/dedupliquees (%d o)", loaded, (int)r);
}

/* Repeuplement depuis MQTT (frames/log/<slot>) : remplit le slot UNIQUEMENT s'il est vide (NVS vide)
 * -> restaure affichage RF + rejeu + dataset slide. Ne republie rien. Reconcilie aussi la NVS. */
static void ring_fill_from_mqtt(int slot, const char *ser, uint8_t button, uint32_t hop, uint32_t t, int8_t rssi) {
    if (slot < 0 || slot >= RF_RING || !ser || !ser[0]) return;
    LOCK();
    rfrec_t *r = &s_rf[slot];
    if (!r->serial[0] && !ring_contains(ser, hop)) {   /* slot vide ET pas deja present ailleurs (dedup) */
        strlcpy(r->serial, ser, SH_SERIAL_LEN);
        r->button = button; r->hop = hop; r->t = t; r->rssi = rssi;
        s_ring_dirty = true;
        remote_t *rm = NULL;
        for (int i = 0; i < s_nremotes; i++) if (!strcmp(s_remotes[i].serial, ser)) { rm = &s_remotes[i]; break; }
        if (!rm && s_nremotes < 16) { rm = &s_remotes[s_nremotes++]; memset(rm, 0, sizeof(*rm)); strlcpy(rm->serial, ser, SH_SERIAL_LEN); }
        if (rm) {
            bool isnew = true;
            for (int i = 0; i < rm->nhops; i++) if (rm->hops[i].hop == hop) { isnew = false; break; }
            if (isnew) {
                if (rm->nhops >= rm->caphops && rm->caphops < SH_MAX_HOPS) {
                    uint16_t nc = rm->caphops ? (uint16_t)(rm->caphops * 2) : 32; if (nc > SH_MAX_HOPS) nc = SH_MAX_HOPS;
                    dframe_t *np = realloc(rm->hops, (size_t)nc * sizeof(dframe_t)); if (np) { rm->hops = np; rm->caphops = nc; }
                }
                if (rm->nhops < rm->caphops) { rm->hops[rm->nhops++] = (dframe_t){ .hop = hop, .t = t, .button = button }; s_frames_dirty = true; }
            }
        }
    }
    UNLOCK();
}

/* Remet la config a zero (libere les sets de hops avant reload/import). */
static void reset_state(void) {
    for (int i = 0; i < s_nremotes; i++) { free(s_remotes[i].hops); s_remotes[i].hops = NULL; s_remotes[i].nhops = s_remotes[i].caphops = 0; }
    for (int i = 0; i < s_nvolets; i++) cfg_volet_release(&s_volets[i]);   /* listes des centrales */
    s_nremotes = 0; s_nvolets = 0;
}
static cfg_layout_t load_cfg(void) {
    cfg_layout_t lay = cfg_model_load(&s_model);
    for (int i = 0; i < s_nvolets; i++)   /* DIAGNOSTIC : verifie que la calibration est bien relue */
        ESP_LOGI(TAG, "cfg volet '%s' : up=%u ms down=%u ms pos=%d",
                 s_volets[i].id, (unsigned)s_volets[i].travel_up_ms, (unsigned)s_volets[i].travel_down_ms, (int)(s_volets[i].position + 0.5f));
    return lay;
}

/* ── RF ── */
#define PRESS_REPEATS 10   /* nb de trames par "appui" : le bit-bang peut etre preempte par le WiFi
                              (trame corrompue) ; plus de repetitions = plus de chances qu'une passe propre */
/* Emet une RAFALE = 1 appui de telecommande, en UNE session TX (trames dos-a-dos, une seule
 * calibration) via l'arbitre radio. Le moteur part ensuite tout seul jusqu'au STOP/butee. */
static void emit_press(const char *bits) {
    if (!bits || !bits[0]) return;
    /* Log de CE QU'ON EMET (serial/bouton/hop decodes de la trame), symetrique du log RX.
     * Permet de verifier qu'on rejoue bien la trame captee (et de voir le hop emis). */
    int n = (int)strlen(bits);
    if (n >= 64) {
        uint32_t hop = 0, ser = 0; uint8_t btn = 0;
        for (int i = 0; i < 32; i++) if (bits[i]      == '1') hop |= (1u << i);
        for (int i = 0; i < 28; i++) if (bits[32 + i] == '1') ser |= (1u << i);
        for (int i = 0; i < 4;  i++) if (bits[60 + i] == '1') btn |= (1u << i);
        ESP_LOGW(TAG, "TX serial=0x%07X bouton=0x%X hop=0x%08X (%d bits, rafale x%d)",
                 (unsigned)ser, btn, (unsigned)hop, n, PRESS_REPEATS);
    }
    radio_tx(bits, PRESS_REPEATS);
}
/* Rejoue TELLE QUELLE une trame captee (identifiee par serial+hop dans l'anneau RF).
 * Sert au bouton "rejouer" du debug RF : on renvoie la trame brute, sans passer par un volet. */
int shutters_replay_frame(const char *serial, uint32_t hop) {
    if (!serial || !serial[0]) return -1;
    char bits[SH_BITS_LEN] = {0};
    LOCK();
    for (int k = 0; k < RF_RING; k++) {
        rfrec_t *r = &s_rf[k];
        if (r->serial[0] && !strcmp(r->serial, serial) && r->hop == hop) {   /* reconstruit les 66 bits a la demande */
            build_frame_bits(bits, (uint32_t)strtoul(serial, NULL, 16), r->button, hop); break;
        }
    }
    UNLOCK();
    if (!bits[0]) return -2;   /* plus dans l'anneau (ecrasee) */
    emit_press(bits);
    return 0;
}
/* Dump du ring RF pour /api/rf : k=0 = plus recente. Renvoie 0 si trame presente, -1 sinon. */
int shutters_rf_get(int k, char *serial, int sser, uint8_t *button, uint32_t *hop, uint32_t *t, int8_t *rssi) {
    if (k < 0 || k >= RF_RING) return -1;
    LOCK();
    int idx = (s_rfhead - 1 - k + 2 * RF_RING) % RF_RING;
    rfrec_t *r = &s_rf[idx];
    if (!r->serial[0]) { UNLOCK(); return -1; }
    strlcpy(serial, r->serial, sser);
    *button = r->button; *hop = r->hop; *t = r->t; *rssi = r->rssi;
    UNLOCK();
    return 0;
}
int shutters_rf_capacity(void) { return RF_RING; }
/* bouton (4 bits LSB) d'une trame captee (bits[60..63]). */
static uint8_t bits_button(const char *b) {
    uint8_t v = 0;
    if ((int)strlen(b) >= 64) for (int i = 0; i < 4; i++) if (b[60 + i] == '1') v |= (1u << i);
    return v;
}

/* TE d'emission configure (cfg/tx_te) pour restaurer apres une emission virtuelle. */
static uint32_t sh_cfg_tx_te(void) {
    uint32_t te = 455; nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
        uint32_t t; if (nvs_get_u32(h, "tx_te", &t) == ESP_OK && t) te = t;
        nvs_close(h);
    }
    return te;
}
/* Volet VIRTUEL : GENERE la trame (keeloq + compteur roulant) au TE du modele, l'emet,
 * puis avance et persiste le compteur. */
static void emit_virt(volet_t *v, uint8_t button) {
    char bits[68];
    uint16_t cnt = v->virt_counter;
    if (pfx_enrol_frame(v->virt_serial, button, cnt, bits) != 0) return;
    /* Réserve-puis-émets : on persiste le PROCHAIN compteur AVANT d'émettre. Un crash entre les
     * deux fait au pire sauter un compteur (toléré par la fenêtre KeeLoq), jamais un rejeu. */
    v->virt_counter = cnt + 1;
    save_cfg();
    uint32_t prev_te = sh_cfg_tx_te();
    cc1101_set_tx_te(v->virt_te ? v->virt_te : 455);
    radio_tx(bits, PRESS_REPEATS);
    cc1101_set_tx_te(prev_te);
    ESP_LOGW(TAG, "TX VIRT serial=0x%07X btn=0x%X cnt=%u te=%u",
             (unsigned)v->virt_serial, button, cnt, v->virt_te);
}

/* Demarre un mouvement (dir=+1 up / -1 down) : UNE rafale (appui), puis le moteur part seul.
 * On integre juste la position ensuite ; l'arret se fait par do_stop (rafale STOP) au bon moment. */
static void start_move(volet_t *v, int dir) {
    v->dir = dir; v->own_move = true;
    v->last_tick_us = esp_timer_get_time();
    radio_pause_rx(true);   /* le temps de nos rafales, l'ecoute permanente ne doit pas bloquer le TX */
    if (v->virt) emit_virt(v, dir > 0 ? PFX_BTN_UP : PFX_BTN_DOWN);
    else         emit_press(dir > 0 ? v->up : v->down);
}
static void do_stop(volet_t *v) {
    v->dir = 0; v->target = -1; v->own_move = false; s_pos_dirty = true;   /* fin mouvement : suivi telecommande OK + retenir la position */
    if (v->virt) emit_virt(v, PFX_BTN_STOP);
    else         emit_press(v->stop);    /* rafale STOP = fige le moteur a la position voulue */
    radio_pause_rx(false);  /* fin de notre commande : on reprend l'ecoute (sync + frame-log) */
}
/* Sync depuis une commande EXTERNE (vraie telecommande) : suit sans emettre.
 * Le moteur part vers la butee (target) ; l'arret intermediaire vient du STOP (freeze). */
static void track_move(volet_t *v, int dir) {
    v->dir = dir; v->own_move = false; v->target = (dir > 0) ? 0 : 100;   /* monter -> fermeture 0, descendre -> 100 */
    v->last_tick_us = esp_timer_get_time();
}
static void freeze(volet_t *v) { v->dir = 0; v->target = -1; v->own_move = false; s_pos_dirty = true; }

/* ── Home Assistant : discovery + etat (MQTT) ── */
static void ha_state_str(const volet_t *v, char *out) {
    if      (v->dir > 0)          strcpy(out, "opening");   /* monte = ouvre */
    else if (v->dir < 0)          strcpy(out, "closing");   /* descend = ferme */
    else if (v->position <= 1)    strcpy(out, "open");      /* fermeture 0 = ouvert (haut) */
    else if (v->position >= 99)   strcpy(out, "closed");    /* fermeture 100 = ferme (bas) */
    else                          strcpy(out, "stopped");
}
/* Publie position + etat du volet (retained). Appele sous LOCK. */
static void publish_volet_state(volet_t *v) {
    if (!s_mqtt_ready) return;
    char topic[96], pl[16], st[10], slug[SH_ID_LEN];
    slugify(slug, sizeof(slug), v->id);   /* topics slugifies (coherents avec la decouverte) */
    if (s_log_frames) {   /* position publiee seulement si suivi actif (coherent avec la decouverte + l'UI) */
        snprintf(topic, sizeof(topic), "openprofalux/cover/%s/position", slug);
        snprintf(pl, sizeof(pl), "%d", 100 - (int)(v->position + 0.5f));   /* HA = % d'OUVERTURE (100=ouvert) ; interne = fermeture */
        pub_defer(topic, pl, 0, 1);
    }
    ha_state_str(v, st);
    snprintf(topic, sizeof(topic), "openprofalux/cover/%s/state", slug);
    pub_defer(topic, st, 0, 1);
    v->pub_pos = (int)(v->position + 0.5f); v->pub_dir = v->dir;
}
/* Publie la config HA discovery d'un cover (retained). Appele sous LOCK. */
static void announce_one(volet_t *v) {
    if (!s_mqtt_ready) return;
    char topic[160], *pl = malloc(1536);
    if (!pl) return;
    /* configuration_url = UI web du boitier (via son IP) pour "Visiter l'appareil" dans HA ;
     * repli sur la page projet si l'IP n'est pas connue. */
    char cu[52];
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr)
        snprintf(cu, sizeof(cu), "http://" IPSTR, IP2STR(&ip.ip));
    else
        strlcpy(cu, "https://www.isno.fr/projets/openprofalux", sizeof(cu));
    /* Position exposee a HA UNIQUEMENT si l'ecoute permanente est active (comme dans l'UI web) :
     * sinon un coup de vraie telecommande desynchronise l'estimation, un % faux est pire qu'aucun.
     * Sans l'option, le cover reste pilotable (ouvrir/fermer/stop) mais sans position. */
    /* slug HA-safe : le nom du volet peut avoir espaces/accents (ex "Chambre Parent"),
     * INTERDITS dans les topics (decouverte ET command/state). On slugifie PARTOUT et on
     * resout le volet par slug a la reception (find_volet_by_slug). */
    char slug[SH_ID_LEN]; slugify(slug, sizeof(slug), v->id);
    const char *devname = (!s_device[0] || !strcmp(s_device, "openprofalux")) ? "OpenProfalux" : s_device;
    /* Position seulement si ecoute permanente (sinon % non fiable) */
    char pos[240] = "";
    if (s_log_frames && !v->central)   /* une centrale n'a pas de position (juste ouvrir/fermer/stop) */
        snprintf(pos, sizeof(pos),
            "\"position_topic\":\"openprofalux/cover/%s/position\","
            "\"set_position_topic\":\"openprofalux/cover/%s/set_position\","
            "\"position_open\":100,\"position_closed\":0,", slug, slug);
    snprintf(topic, sizeof(topic), "homeassistant/cover/openprofalux_%s/config", slug);
    /* Cles en TOUTES LETTRES (alignees sur OpenXtraflame) ; device PARTAGE (un seul appareil
     * "OpenProfalux" regroupe tous les volets) ; topics command/state SLUGIFIES. */
    snprintf(pl, 1536,
        "{\"name\":\"%s\",\"unique_id\":\"openprofalux_%s\",\"object_id\":\"openprofalux_%s\",\"device_class\":\"shutter\","
        "\"command_topic\":\"openprofalux/cover/%s/set\","
        "\"payload_open\":\"OPEN\",\"payload_close\":\"CLOSE\",\"payload_stop\":\"STOP\","
        "%s"
        "\"state_topic\":\"openprofalux/cover/%s/state\","
        "\"availability_topic\":\"openprofalux/%s/status\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
        "\"device\":{\"identifiers\":[\"openprofalux\"],\"name\":\"%s\","
        "\"manufacturer\":\"isno.fr\",\"model\":\"OpenProfalux\",\"configuration_url\":\"%s\"}}",
        v->id, slug, slug, slug, pos, slug, s_device, devname, cu);
    pub_defer(topic, pl, 1, 1);
    free(pl);
    publish_volet_state(v);
}

/* Entites HA supplementaires (une seule fois, sous le meme appareil) :
 * - un SWITCH "Ecoute RF permanente" (on/off depuis HA),
 * - un CAPTEUR "Derniere trame RF" (voir les trames passer dans HA). */
static void announce_extras(void) {
    if (!s_mqtt_ready) return;
    const char *devname = (!s_device[0] || !strcmp(s_device, "openprofalux")) ? "OpenProfalux" : s_device;
    char dev[320];
    snprintf(dev, sizeof(dev),
        "\"availability_topic\":\"openprofalux/%s/status\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
        "\"device\":{\"identifiers\":[\"openprofalux\"],\"name\":\"%s\",\"manufacturer\":\"isno.fr\",\"model\":\"OpenProfalux\"}",
        s_device, devname);
    char *pl = malloc(700); if (!pl) return;
    snprintf(pl, 700,
        "{\"name\":\"Ecoute RF permanente\",\"unique_id\":\"openprofalux_listen\",\"object_id\":\"openprofalux_listen\","
        "\"command_topic\":\"openprofalux/listen/set\",\"state_topic\":\"openprofalux/listen/state\","
        "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"icon\":\"mdi:radio-tower\",%s}", dev);
    pub_defer("homeassistant/switch/openprofalux_listen/config", pl, 1, 1);
    snprintf(pl, 700,
        "{\"name\":\"Derniere trame RF\",\"unique_id\":\"openprofalux_last_frame\",\"object_id\":\"openprofalux_last_frame\","
        "\"state_topic\":\"openprofalux/frames/last\",\"value_template\":\"{{ value_json.hop }}\","
        "\"json_attributes_topic\":\"openprofalux/frames/last\",\"icon\":\"mdi:remote\",%s}", dev);
    pub_defer("homeassistant/sensor/openprofalux_last_frame/config", pl, 1, 1);
    free(pl);
    pub_defer("openprofalux/listen/state", s_log_frames ? "ON" : "OFF", 0, 1);
}

/* ── API commande ── */
int shutters_delete_volet(const char *id) {
    if (!id || !*id) return -1;
    LOCK();
    int idx = -1;
    for (int i = 0; i < s_nvolets; i++) if (!strcmp(s_volets[i].id, id)) { idx = i; break; }
    if (idx < 0) { UNLOCK(); return -1; }
    cfg_model_remove(&s_model, idx);   /* libere la liste d'une centrale */
    save_cfg();
    update_listening();   /* plus aucun volet -> coupe l'ecoute permanente */
    UNLOCK();
    ESP_LOGW(TAG, "volet '%s' supprime", id);
    return 0;
}

int shutters_cmd(const char *id, const char *cmd, int value) {
    LOCK();
    volet_t *v = find_volet(id);
    if (!v) { ESP_LOGW(TAG, "CMD '%s' : volet '%s' introuvable", cmd, id ? id : "(null)"); UNLOCK(); return -1; }
    ESP_LOGW(TAG, "CMD %s '%s' (up=%dc stop=%dc down=%dc)", cmd, id,
             (int)strlen(v->up), (int)strlen(v->stop), (int)strlen(v->down));
    if (v->central) {   /* centrale : diffuse la commande a chaque volet membre (CSV) */
        char mid[SH_ID_LEN];
        for (const char *p = cfg_members_next(v->members, mid); p; p = cfg_members_next(p, mid)) {
            volet_t *m = find_volet(mid);
            if (!m || m == v || m->central) continue;
            if      (!strcmp(cmd, "up"))   { m->target = 0;   start_move(m, +1); }
            else if (!strcmp(cmd, "down")) { m->target = 100; start_move(m, -1); }
            else if (!strcmp(cmd, "stop")) { do_stop(m); }
            publish_volet_state(m);
        }
        if (!strcmp(cmd, "up")) v->position = 0; else if (!strcmp(cmd, "down")) v->position = 100;
        v->dir = 0;
        publish_volet_state(v);
        UNLOCK(); pub_flush();
        return 0;
    }
    /* position = % de FERMETURE (0 = ouvert/haut, 100 = ferme/bas). */
    if (!strcmp(cmd, "up"))        { v->target = 0;   start_move(v, +1); }   /* ouvrir : monter vers 0 */
    else if (!strcmp(cmd, "down")) { v->target = 100; start_move(v, -1); }   /* fermer : descendre vers 100 */
    else if (!strcmp(cmd, "stop")) { do_stop(v); }
    else if (!strcmp(cmd, "pos"))  {   /* value = % de fermeture voulu */
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        v->target = value;
        if (value > v->position + 1)      start_move(v, -1);   /* plus ferme -> descendre */
        else if (value < v->position - 1) start_move(v, +1);   /* plus ouvert -> monter */
        else v->dir = 0;
    }
    publish_volet_state(v);
    UNLOCK();
    pub_flush();
    return 0;
}

/* ── Tache : suivi position + streaming du maintien ── */
static void tick_task(void *arg) {
    (void)arg;
    int save_ticks = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        LOCK();
        int64_t now = esp_timer_get_time();
        for (int i = 0; i < s_nvolets; i++) {
            volet_t *v = &s_volets[i];
            if (v->dir == 0) continue;
            uint32_t travel = v->dir > 0 ? v->travel_up_ms : v->travel_down_ms;
            if (travel < 500) travel = 18000;   /* defaut si non calibre */
            float dms = (now - v->last_tick_us) / 1000.0f;
            v->last_tick_us = now;
            /* Convention : position = % de FERMETURE. 0 = ouvert (haut), 100 = ferme (bas).
             * Monter (dir=+1) DIMINUE la position ; descendre (dir=-1) l'augmente -> d'ou le -=. */
            v->position -= v->dir * (dms / travel) * 100.0f;
            if (v->position < 0) v->position = 0;
            if (v->position > 100) v->position = 100;
            /* cible atteinte ou butee -> stop (monter -> vers 0 ; descendre -> vers 100) */
            int reached = (v->dir > 0 && (v->position <= 0   || (v->target >= 0 && v->position <= v->target)))
                       || (v->dir < 0 && (v->position >= 100 || (v->target >= 0 && v->position >= v->target)));
            if (reached) {
                if (!v->own_move) freeze(v);                                /* mouvement externe : on fige le suivi */
                else if (v->target > 0 && v->target < 100) do_stop(v);      /* notre commande vers une position INTERMEDIAIRE : STOP pour figer */
                else { v->dir = 0; v->target = -1; v->own_move = false; s_pos_dirty = true; radio_pause_rx(false); } /* pleine course : butee, own_move false, retenir la position */
            }
            /* sinon mouvement en cours : le moteur (le notre, lance par une rafale, ou l'externe)
             * tourne tout seul jusqu'au STOP / butee ; on integre juste la position sans re-emettre */
            /* publie l'etat HA quand la position (entiere) ou la direction change */
            if (reached || (int)(v->position + 0.5f) != v->pub_pos || v->dir != v->pub_dir)
                publish_volet_state(v);
        }
        UNLOCK();
        pub_flush();   /* publie les changements de position/etat HORS LOCK */
        if (s_pos_dirty) { s_pos_dirty = false; LOCK(); save_cfg(); UNLOCK(); }   /* fige la nouvelle position en NVS (retenue au reboot) */
        /* sauvegarde periodique du dataset et du ring en SPIFFS (hors LOCK ; menage la flash : ~60 s si modifie) */
        if (++save_ticks * TICK_MS >= 60000) {
            save_ticks = 0;
            if (s_frames_dirty) save_frames();
            if (s_ring_dirty)   save_ring();   /* trames recentes rejouables persistees */
        }
    }
}

/* ── Apprentissage / calibration / nommage ── */
int shutters_learn_assign(const char *id, const char *action, const char *bits) {
    if (!id || !*id || !bits) return -1;
    LOCK();
    volet_t *v = get_or_create(id);
    if (!v) { UNLOCK(); return -1; }
    uint8_t btn = bits_button(bits);
    if      (!strcmp(action, "up"))   { strlcpy(v->up, bits, SH_BITS_LEN);   v->up_btn = btn; }
    else if (!strcmp(action, "down")) { strlcpy(v->down, bits, SH_BITS_LEN); v->down_btn = btn; }
    else if (!strcmp(action, "stop")) { strlcpy(v->stop, bits, SH_BITS_LEN); v->stop_btn = btn; }
    else { UNLOCK(); return -1; }
    /* memorise le serial de cette telecommande pour ce volet (sync position) */
    uint32_t ser = 0; if ((int)strlen(bits) >= 60) for (int i = 0; i < 28; i++) if (bits[32 + i] == '1') ser |= (1u << i);
    char shex[SH_SERIAL_LEN]; snprintf(shex, sizeof(shex), "0x%07X", (unsigned)ser);
    bool has = false;
    for (int i = 0; i < v->n_serials; i++) if (!strcmp(v->serials[i], shex)) { has = true; break; }
    if (!has && v->n_serials < SH_MAX_SERIALS) strlcpy(v->serials[v->n_serials++], shex, SH_SERIAL_LEN);
    save_cfg();
    announce_one(v);       /* (re)publie le cover HA des qu'il est appris */
    update_listening();    /* 1er volet appris -> allume l'ecoute pour le suivi de position */
    UNLOCK();
    pub_flush();
    return 0;
}

/* true si un volet virtuel utilise deja ce serial 0x067 (evite les conflits d'allocation de slot). */
bool shutters_virt_serial_used(uint32_t serial) {
    for (int i = 0; i < s_nvolets; i++)
        if (s_volets[i].virt && s_volets[i].virt_serial == serial) return true;
    return false;
}
/* Compteur roulant courant du volet virtuel de ce serial (0 si aucun). Sert a resynchroniser
 * l'identite d'enrolement sur la valeur reellement emise par le volet (toujours devant le moteur). */
uint16_t shutters_virt_counter_for_serial(uint32_t serial) {
    for (int i = 0; i < s_nvolets; i++)
        if (s_volets[i].virt && s_volets[i].virt_serial == serial) return s_volets[i].virt_counter;
    return 0;
}

/* Cree (ou convertit) un volet VIRTUEL a partir d'une identite 0x067 enrolee : il apparait
 * dans l'onglet Volets + en cover HA, et pilote le moteur par generation (compteur roulant). */
int shutters_create_virtual(const char *id, uint32_t serial, uint16_t counter, uint16_t te) {
    if (!id || !*id) return -1;
    LOCK();
    volet_t *v = get_or_create(id);
    if (!v) { UNLOCK(); return -1; }
    v->virt = true;
    v->virt_serial = serial;
    v->virt_counter = counter;
    v->virt_te = te;
    char shex[SH_SERIAL_LEN]; snprintf(shex, sizeof(shex), "0x%07X", (unsigned)serial);
    bool has = false;
    for (int i = 0; i < v->n_serials; i++) if (!strcmp(v->serials[i], shex)) { has = true; break; }
    if (!has && v->n_serials < SH_MAX_SERIALS) strlcpy(v->serials[v->n_serials++], shex, SH_SERIAL_LEN);
    /* Nomme la télécommande virtuelle -> l'UI affiche son NOM (pas le serial brut). */
    int ri = -1;
    for (int i = 0; i < s_nremotes; i++) if (!strcmp(s_remotes[i].serial, shex)) { ri = i; break; }
    if (ri < 0 && s_nremotes < 16) { ri = s_nremotes++; strlcpy(s_remotes[ri].serial, shex, SH_SERIAL_LEN); }
    if (ri >= 0) strlcpy(s_remotes[ri].name, id, SH_ID_LEN);
    save_cfg();
    announce_one(v);
    UNLOCK();
    pub_flush();
    ESP_LOGW(TAG, "volet virtuel '%s' cree : identite 0x%07X cnt=%u te=%u",
             id, (unsigned)serial, counter, te);
    return 0;
}

/* Cree/convertit une CENTRALE : commande diffusee a un groupe de volets (members_csv = "id1,id2,..."). */
int shutters_create_central(const char *id, const char *members_csv) {
    if (!id || !*id) return -1;
    LOCK();
    int before = s_nvolets;
    volet_t *v = get_or_create(id);
    if (!v) { UNLOCK(); return -1; }
    /* Liste refusee plutot que tronquee (trop longue, ou plus de memoire) : un
     * membre coupe ne recevrait plus rien. Rien ne change, pas meme un volet cree. */
    if (cfg_volet_set_members(v, members_csv) != 0) {
        if (s_nvolets > before) cfg_model_remove(&s_model, s_nvolets - 1);
        UNLOCK();
        ESP_LOGE(TAG, "centrale '%s' refusee : liste de membres de %u o (plus de %d, ou plus de memoire)",
                 id, members_csv ? (unsigned)strlen(members_csv) : 0u, SH_MEMBERS_LEN - 1);
        return -1;
    }
    v->central = true; v->virt = false;
    v->position = 50; v->dir = 0; v->target = -1;
    save_cfg();
    announce_one(v);
    ESP_LOGW(TAG, "centrale '%s' creee : membres=[%s]", id, v->members ? v->members : "");
    UNLOCK();
    pub_flush();
    return 0;
}

/* Champs (bits + bouton) d'une action pour un volet. NULL si action inconnue. */
static char *action_bits(volet_t *v, const char *a, uint8_t **btn) {
    if (!strcmp(a, "up"))   { *btn = &v->up_btn;   return v->up; }
    if (!strcmp(a, "stop")) { *btn = &v->stop_btn; return v->stop; }
    if (!strcmp(a, "down")) { *btn = &v->down_btn; return v->down; }
    return NULL;
}

/* Permute deux commandes apprises (corrige une trame rangee dans le mauvais slot,
 * sans avoir a re-presser la telecommande). Si la cible est vide, revient a un deplacement. */
int shutters_reassign(const char *id, const char *from, const char *to) {
    if (!id || !from || !to) return -1;
    if (!strcmp(from, to)) return 0;
    LOCK();
    volet_t *v = find_volet(id);
    if (!v) { UNLOCK(); return -1; }
    uint8_t *fb, *tb; char *fbits = action_bits(v, from, &fb), *tbits = action_bits(v, to, &tb);
    if (!fbits || !tbits) { UNLOCK(); return -1; }
    char tmp[SH_BITS_LEN]; uint8_t tmpb;
    strlcpy(tmp, tbits, SH_BITS_LEN); tmpb = *tb;
    strlcpy(tbits, fbits, SH_BITS_LEN); *tb = *fb;
    strlcpy(fbits, tmp, SH_BITS_LEN); *fb = tmpb;
    save_cfg();
    announce_one(v);
    UNLOCK();
    pub_flush();
    ESP_LOGW(TAG, "reaffectation '%s' %s <-> %s", id, from, to);
    return 0;
}

int shutters_adopt(const char *id, const char *action, const char *serial, uint32_t hop) {
    if (!id || !action || !serial) return -1;
    char bits[SH_BITS_LEN] = {0};
    LOCK();
    for (int k = 0; k < RF_RING; k++) {
        rfrec_t *r = &s_rf[k];
        if (r->serial[0] && !strcmp(r->serial, serial) && r->hop == hop) {   /* reconstruit les 66 bits */
            build_frame_bits(bits, (uint32_t)strtoul(serial, NULL, 16), r->button, hop); break;
        }
    }
    UNLOCK();
    if (!bits[0]) return -1;
    return shutters_learn_assign(id, action, bits);   /* prend son propre LOCK */
}

int shutters_calibrate(const char *id, uint32_t up_ms, uint32_t down_ms) {
    LOCK();
    volet_t *v = find_volet(id);
    if (!v) { UNLOCK(); return -1; }
    v->travel_up_ms = up_ms; v->travel_down_ms = down_ms;
    save_cfg();
    UNLOCK();
    return 0;
}
int shutters_set_orientation(const char *id, int orientation) {
    LOCK();
    volet_t *v = find_volet(id);
    if (!v) { UNLOCK(); return -1; }
    v->orientation = (orientation < 0) ? -1 : (orientation % 360);   /* azimut facade (-1 = non defini) */
    save_cfg();
    UNLOCK();
    return 0;
}
/* Applique un ordre d'affichage : ids_csv = liste d'ids separes par des virgules,
 * dans l'ordre voulu. Chaque volet nomme recoit son rang ; ceux qui ne sont pas
 * dans la liste passent a -1 (affiches en fin, dans leur ordre de creation).
 *
 * N'affecte QUE l'affichage de l'UI web : aucun impact sur la radio, le pilotage,
 * la position ou la decouverte Home Assistant. Une seule ecriture NVS, a la
 * demande de l'utilisateur (jamais pendant un mouvement). */
int shutters_set_order(const char *ids_csv) {
    if (!ids_csv) return -1;
    /* Copie de travail (strtok_r modifie sa chaine) a la taille de la liste :
     * un tampon fixe de 384 octets la tronquait au-dela. Sur
     * le tas : rien de plus sur la pile de la tache HTTP. */
    char *buf = strdup(ids_csv);
    if (!buf) return -1;
    LOCK();
    for (int i = 0; i < s_nvolets; i++) s_volets[i].order = -1;   /* remise a plat */
    int rank = 0;
    char *sv = NULL;
    for (char *tok = strtok_r(buf, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
        while (*tok == ' ') tok++;
        volet_t *v = find_volet(tok);
        if (v) v->order = rank++;
    }
    save_cfg();
    UNLOCK();
    free(buf);
    ESP_LOGI(TAG, "ordre d'affichage mis a jour (%d volets ranges)", rank);
    return 0;
}

int shutters_remote_name(const char *serial, const char *name) {
    LOCK();
    for (int i = 0; i < s_nremotes; i++) if (!strcmp(s_remotes[i].serial, serial)) {
        strlcpy(s_remotes[i].name, name, SH_ID_LEN); save_cfg(); UNLOCK(); return 0;
    }
    if (s_nremotes < 16) {
        strlcpy(s_remotes[s_nremotes].serial, serial, SH_SERIAL_LEN);
        strlcpy(s_remotes[s_nremotes].name, name, SH_ID_LEN);
        s_nremotes++; save_cfg();
    }
    UNLOCK();
    return 0;
}

/* ══ FONCTION 1 — Dataset slide (option MQTT "capture toutes les trames") ══
 * Publie un hop UNIQUEMENT s'il n'est pas deja dans le set enregistre de ce serial.
 * Sert a collecter les 65536 hops distincts pour l'attaque slide-MITM. Appele sous LOCK. */
static void dataset_log_frame(const char *shex, uint8_t button, uint32_t hop, int8_t rssi, uint32_t t) {
    remote_t *rm = NULL;
    for (int i = 0; i < s_nremotes; i++) if (!strcmp(s_remotes[i].serial, shex)) { rm = &s_remotes[i]; break; }
    if (!rm) return;
    bool is_new = true;
    if (rm->nhops && rm->last_hop == hop) is_new = false;                      /* fast-path maintien */
    else for (int i = 0; i < rm->nhops; i++) if (rm->hops[i].hop == hop) { is_new = false; break; }
    rm->last_hop = hop;
    if (!is_new) return;                                                       /* deja enregistre -> rien */
    /* ajoute au set (croissance bornee a SH_MAX_HOPS) */
    if (rm->nhops >= rm->caphops && rm->caphops < SH_MAX_HOPS) {
        uint16_t nc = rm->caphops ? (uint16_t)(rm->caphops * 2) : 32;
        if (nc > SH_MAX_HOPS) nc = SH_MAX_HOPS;
        dframe_t *np = realloc(rm->hops, (size_t)nc * sizeof(dframe_t));
        if (np) { rm->hops = np; rm->caphops = nc; }
    }
    if (rm->nhops < rm->caphops) { rm->hops[rm->nhops++] = (dframe_t){ .hop = hop, .t = t, .button = button }; s_frames_dirty = true; }
    /* Plus de publication frames/<serial> ni /count : une seule structure MQTT = l'anneau frames/log/<n>.
     * Le dataset slide (hops distincts) reste construit ici en RAM/NVS pour l'export /api/frames. */
}

/* ══ FONCTION 2 — Suivi de position via telecommandes ENREGISTREES ══
 * Independante de l'option MQTT : des qu'un volet a appris ce serial+bouton, on suit
 * sa position quand la VRAIE telecommande l'actionne. Appele sous LOCK. */
static void track_shutter_position(const char *shex, uint8_t button) {
    for (int i = 0; i < s_nvolets; i++) {
        volet_t *v = &s_volets[i];
        bool mine = false;
        for (int j = 0; j < v->n_serials; j++) if (!strcmp(v->serials[j], shex)) { mine = true; break; }
        if (!mine) continue;
        /* Profalux = appui + stop : ▲/▼ lance le moteur vers la butee, STOP le fige.
         * On demarre le suivi au 1er appui (repetitions du burst ignorees), on fige au STOP. */
        if (v->stop_btn && button == v->stop_btn) {
            freeze(v);
        } else if (v->up_btn && button == v->up_btn) {
            if (!v->own_move && v->dir != +1) track_move(v, +1);
        } else if (v->down_btn && button == v->down_btn) {
            if (!v->own_move && v->dir != -1) track_move(v, -1);
        }
        /* v->own_move : notre propre echo (meme serial rejoue) -> ignore.
         * PAS de break : un meme serial (telecommande GENERALE) peut piloter plusieurs volets
         * -> on met a jour l'etat de TOUS les volets qui portent ce serial (sync HA correcte). */
    }
}

/* ── RX : chaque trame recue -> journal + les 2 fonctions ci-dessus ── */
void shutters_on_rx(const char *bits, uint32_t serial, uint8_t button, int8_t rssi, uint32_t hop) {
    ESP_LOGI(TAG, "RX serial=0x%07X bouton=0x%X hop=0x%08X rssi=%d dBm",
             (unsigned)serial, button, (unsigned)hop, rssi);
    bool do_pub = false; int pub_slot = -1; char pub_lf[128];   /* publie APRES UNLOCK (anti-deadlock) */
    LOCK();
    char shex[SH_SERIAL_LEN]; snprintf(shex, sizeof(shex), "0x%07X", (unsigned)serial);
    (void)bits;   /* plus stocke : les 66 bits sont reconstruits a la demande (rejeu/adoption) */
    /* DEDUP : meme (serial, hop) deja vu -> repetition d'un meme appui, on n'ajoute rien (ni ring ni MQTT). */
    if (!ring_contains(shex, hop)) {
        int slot = s_rfhead;   /* emplacement -> topic MQTT recuperable frames/log/<slot> */
        rfrec_t *r = &s_rf[s_rfhead];
        time_t now = time(NULL);
        uint32_t t = (now > 1600000000) ? (uint32_t)now : 0;   /* epoch si SNTP synchro, sinon 0 */
        r->t = t; r->button = button; r->hop = hop; r->rssi = rssi;
        strlcpy(r->serial, shex, SH_SERIAL_LEN);
        s_rfhead = (s_rfhead + 1) % RF_RING;
        s_ring_dirty = true;
        /* auto-enregistre le serial vu (nom vide) */
        bool known = false;
        for (int i = 0; i < s_nremotes; i++) if (!strcmp(s_remotes[i].serial, shex)) { known = true; break; }
        if (!known && s_nremotes < 16) { strlcpy(s_remotes[s_nremotes].serial, shex, SH_SERIAL_LEN); s_remotes[s_nremotes].name[0] = 0; s_nremotes++; }
        if (s_log_frames && s_mqtt_ready) {
            dataset_log_frame(shex, button, hop, rssi, t);  /* dataset slide (dedup par hop, bouton+t) */
            /* On PREPARE le payload ici, mais on PUBLIE hors du LOCK (voir plus bas). */
            snprintf(pub_lf, sizeof(pub_lf), "{\"serial\":\"%s\",\"button\":\"0x%X\",\"hop\":\"0x%08X\",\"rssi\":%d,\"t\":%u}",
                     shex, button, (unsigned)hop, rssi, (unsigned)t);
            pub_slot = slot; do_pub = true;
        }
    }
    track_shutter_position(shex, button);   /* toujours (idempotent sur les repetitions) */
    UNLOCK();
    /* DEADLOCK FIX : ne JAMAIS publier en MQTT sous le LOCK. Un publish QoS1 retained peut
     * bloquer (outbox pleine pendant une rafale d'apprentissage) ; or l'ESP est abonne a
     * frames/log/# et recoit ses propres publications -> la tache MQTT reprend ce meme LOCK
     * pour les traiter. Publier apres UNLOCK casse le cycle d'attente mutuelle. */
    if (do_pub) {
        mqtt_pub_raw("openprofalux/frames/last", pub_lf, 0, 1);   /* capteur HA "Derniere trame" */
        char lt[48]; snprintf(lt, sizeof(lt), "openprofalux/frames/log/%d", pub_slot);
        mqtt_pub_raw(lt, pub_lf, 1, 1);   /* QoS 1 + retained : recuperable via frames/log/# */
    }
}

/* Ajoute au JSON une commande apprise : { "b": <bouton>, "s": "0x<serial>" }.
 * Le serial est extrait de la trame stockee (bits[32..59], LSB), pour verifier
 * dans l'UI que les 3 commandes viennent bien de la meme telecommande. */
static void add_cmd_json(cJSON *cmd, const char *key, const char *bits, uint8_t btn) {
    if (!bits[0]) return;
    uint32_t ser = 0;
    if ((int)strlen(bits) >= 60) for (int i = 0; i < 28; i++) if (bits[32 + i] == '1') ser |= (1u << i);
    char sx[12]; snprintf(sx, sizeof(sx), "0x%07X", (unsigned)ser);
    cJSON *c = cJSON_CreateObject();
    cJSON_AddNumberToObject(c, "b", btn);
    cJSON_AddStringToObject(c, "s", sx);
    cJSON_AddItemToObject(cmd, key, c);
}

/* ── Status JSON ── */
char *shutters_status_json(void) {
    LOCK();
    cJSON *root = cJSON_CreateObject();
    cJSON *rem = cJSON_AddObjectToObject(root, "remotes");
    for (int i = 0; i < s_nremotes; i++) cJSON_AddStringToObject(rem, s_remotes[i].serial, s_remotes[i].name);
    cJSON *vols = cJSON_AddArrayToObject(root, "volets");
    for (int i = 0; i < s_nvolets; i++) {
        volet_t *v = &s_volets[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", v->id);
        if (v->virt) cJSON_AddBoolToObject(o, "virt", true);   /* volet a telecommande virtuelle -> pas d'apprentissage */
        if (v->central) { cJSON_AddBoolToObject(o, "central", true); cJSON_AddStringToObject(o, "members", v->members ? v->members : ""); }
        cJSON_AddNumberToObject(o, "position", (int)(v->position + 0.5f));
        cJSON_AddNumberToObject(o, "travel_up_ms", v->travel_up_ms);     /* pour reafficher la calibration dans l'UI */
        cJSON_AddNumberToObject(o, "travel_down_ms", v->travel_down_ms);
        cJSON_AddNumberToObject(o, "orientation", v->orientation);       /* azimut facade (-1 = non defini) */
        cJSON_AddNumberToObject(o, "order", v->order);                   /* rang d'affichage UI (-1 = non range) */
        cJSON *sr = cJSON_AddArrayToObject(o, "serials");
        for (int j = 0; j < v->n_serials; j++) cJSON_AddItemToArray(sr, cJSON_CreateString(v->serials[j]));
        /* etat des 3 commandes : bouton appris (nibble) si le slot est rempli, absent sinon.
         * Permet a l'UI d'apprentissage centree-volet d'afficher appris / a capturer. */
        cJSON *cmd = cJSON_AddObjectToObject(o, "cmd");
        add_cmd_json(cmd, "up",   v->up,   v->up_btn);
        add_cmd_json(cmd, "stop", v->stop, v->stop_btn);
        add_cmd_json(cmd, "down", v->down, v->down_btn);
        cJSON_AddItemToArray(vols, o);
    }
    cJSON *rf = cJSON_AddArrayToObject(root, "rf");   /* recentes seulement ; les 300 via /api/rf */
    for (int k = 0; k < STATUS_RF_SHOW; k++) {
        int idx = (s_rfhead - 1 - k + 2 * RF_RING) % RF_RING;
        rfrec_t *r = &s_rf[idx];
        if (!r->serial[0]) continue;
        cJSON *f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "t", r->t);
        cJSON_AddStringToObject(f, "serial", r->serial);
        char hx[10]; snprintf(hx, sizeof(hx), "%X", r->button); cJSON_AddStringToObject(f, "button", hx);
        snprintf(hx, sizeof(hx), "%08X", (unsigned)r->hop); cJSON_AddStringToObject(f, "hop", hx);
        cJSON_AddNumberToObject(f, "rssi", r->rssi);
        cJSON_AddItemToArray(rf, f);
    }
    UNLOCK();
    /* Statut Wi-Fi + MQTT (pour l'UI : pastilles + force du signal) */
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddBoolToObject(wifi, "connected", true);
        cJSON_AddStringToObject(wifi, "ssid", (char *)ap.ssid);
        cJSON_AddNumberToObject(wifi, "rssi", ap.rssi);
        esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip;
        if (nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK && ip.ip.addr) {
            char ips[16]; snprintf(ips, sizeof(ips), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(wifi, "ip", ips);
        }
    } else {
        cJSON_AddBoolToObject(wifi, "connected", false);
    }
    cJSON_AddBoolToObject(root, "mqtt", s_mqtt_ready);
    cJSON_AddBoolToObject(root, "listening", s_log_frames);   /* ecoute permanente = position fiable */
    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000));   /* s depuis boot : date les trames RF cote UI */
    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return js;
}

/* ── Sauvegarde / restauration (telecommandes + noms + trames de reference + calibration) ── */
char *shutters_export_json(void) {
    LOCK();
    char *js = cfg_model_export(&s_model);
    UNLOCK();
    return js;
}
int shutters_import_json(const char *js) {
    if (!js) return -1;
    cJSON *root = cJSON_Parse(js);
    if (!root) return -1;
    LOCK();
    reset_state();
    cfg_model_import(&s_model, root);
    /* L'echec d'ecriture remonte a l'UI : une restauration repondait "ok" meme
     * quand la NVS pleine l'empechait de survivre au redemarrage. */
    esp_err_t saved = save_cfg();
    for (int i = 0; i < s_nvolets; i++) announce_one(&s_volets[i]);   /* re-publie les covers HA restaures */
    update_listening();
    UNLOCK();
    pub_flush();
    cJSON_Delete(root);
    ESP_LOGI(TAG, "config restauree : %d volets, %d telecommandes%s", s_nvolets, s_nremotes,
             saved == ESP_OK ? "" : " (NON enregistree en NVS)");
    return saved == ESP_OK ? 0 : -1;
}

/* ── Integration Home Assistant (MQTT) ── */
/* Etat de l'entite update HA : version installee + derniere version GitHub connue. */
static void publish_update_state(void) {
    if (!s_mqtt_ready) return;
    const esp_app_desc_t *d = esp_app_get_description();
    const char *inst = d ? d->version : "?";
    const char *latest = ota_latest_version();
    if (!latest || !latest[0]) latest = inst;   /* pas encore verifie -> aucune MAJ signalee */
    char pl[288];
    snprintf(pl, sizeof(pl),
        "{\"installed_version\":\"%s\",\"latest_version\":\"%s\",\"title\":\"OpenProfalux\","
        "\"release_url\":\"https://github.com/Isno-Open/OpenProfalux/releases\"}", inst, latest);
    pub_defer("openprofalux/update/state", pl, 0, 1);
}
/* Decouverte HA de l'entite update (plateforme MQTT update, meme device "openprofalux"). */
static void announce_update(void) {
    if (!s_mqtt_ready) return;
    char *pl = malloc(768); if (!pl) return;
    const char *devname = (!s_device[0] || !strcmp(s_device, "openprofalux")) ? "OpenProfalux" : s_device;
    snprintf(pl, 768,
        "{\"name\":\"Firmware\",\"unique_id\":\"openprofalux_update\",\"object_id\":\"openprofalux_update\","
        "\"device_class\":\"firmware\",\"state_topic\":\"openprofalux/update/state\","
        "\"command_topic\":\"openprofalux/update/install\",\"payload_install\":\"INSTALL\","
        "\"availability_topic\":\"openprofalux/%s/status\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
        "\"device\":{\"identifiers\":[\"openprofalux\"],\"name\":\"%s\",\"manufacturer\":\"isno.fr\",\"model\":\"OpenProfalux\"}}",
        s_device, devname);
    pub_defer("homeassistant/update/openprofalux/config", pl, 1, 1);
    free(pl);
    publish_update_state();
}

void shutters_mqtt_announce(const char *device) {
    LOCK();
    strlcpy(s_device, (device && *device) ? device : "op", sizeof(s_device));
    s_mqtt_ready = true;
    for (int i = 0; i < s_nvolets; i++) announce_one(&s_volets[i]);
    announce_extras();           /* switch "Ecoute RF permanente" + capteur "Derniere trame" */
    announce_update();           /* entite update HA (firmware) */
    UNLOCK();
    pub_flush();                 /* publie la decouverte HORS LOCK (anti-deadlock a la connexion) */
    /* NB : plus de flush en masse du ring ici. Les frames/log/<slot> sont QoS1 RETAINED
     * (le broker les garde deja) + persistees en SPIFFS ; re-pousser tout le ring (jusqu'a
     * 1000 QoS1) SOUS LOCK dans la tache MQTT tenait le LOCK trop longtemps -> httpd/commandes
     * figes ("plante"). Les nouvelles trames sont publiees en direct par shutters_on_rx. */
    ESP_LOGI(TAG, "HA discovery publiee pour %d cover(s), device=%s", s_nvolets, s_device);
}

void shutters_mqtt_lost(void) {
    LOCK();
    s_mqtt_ready = false;   /* broker perdu : le statut UI repasse a deconnecte */
    UNLOCK();
    ESP_LOGW(TAG, "MQTT perdu : statut hors ligne");
}

void shutters_mqtt_on_message(const char *topic, const char *data, int len) {
    /* Entite update HA : HA publie "INSTALL" -> on lance l'OTA de la derniere version connue. */
    if (!strcmp(topic, "openprofalux/update/install")) {
        if (ota_latest_url()[0]) {
            ESP_LOGW(TAG, "HA -> installer MAJ %s : %s", ota_latest_version(), ota_latest_url());
            ota_pull_from_url(ota_latest_url());   /* telecharge + reboot (rollback auto si echec) */
        } else {
            ESP_LOGW(TAG, "HA -> install demande mais aucune version GitHub connue");
        }
        return;
    }
    /* Switch HA "Ecoute RF permanente" (commande) */
    if (!strcmp(topic, "openprofalux/listen/set")) {
        shutters_set_log_frames(len >= 2 && !strncasecmp(data, "ON", 2));   /* publie l'etat + re-annonce */
        return;
    }
    /* Etat retained (publie par nous) : restaure l'ecoute au boot si differente. Garde anti-boucle. */
    if (!strcmp(topic, "openprofalux/listen/state")) {
        bool on = (len >= 2 && !strncasecmp(data, "ON", 2));
        if (on != s_log_frames) shutters_set_log_frames(on);   /* re-echo ignore (meme valeur) */
        return;
    }
    /* Repeuplement du ring depuis MQTT (retained frames/log/<slot>) : rempli si slot vide (NVS vide). */
    if (!strncmp(topic, "openprofalux/frames/log/", 24)) {
        int slot = atoi(topic + 24);
        cJSON *j = cJSON_ParseWithLength(data, len);
        if (j) {
            const char *ser = cJSON_GetStringValue(cJSON_GetObjectItem(j, "serial"));
            const char *bs  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "button"));
            const char *hs  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "hop"));
            cJSON *tj = cJSON_GetObjectItem(j, "t"), *rj = cJSON_GetObjectItem(j, "rssi");
            if (ser && hs)
                ring_fill_from_mqtt(slot, ser,
                    bs ? (uint8_t)strtoul(bs, NULL, 16) : 0, (uint32_t)strtoul(hs, NULL, 16),
                    tj ? (uint32_t)cJSON_GetNumberValue(tj) : 0, rj ? (int8_t)cJSON_GetNumberValue(rj) : 0);
            cJSON_Delete(j);
        }
        return;
    }
    static const char P[] = "openprofalux/cover/";
    if (strncmp(topic, P, sizeof(P) - 1)) return;
    const char *after = topic + sizeof(P) - 1;
    const char *slash = strchr(after, '/');
    if (!slash) return;
    char slug[SH_ID_LEN]; int n = slash - after; if (n >= SH_ID_LEN) n = SH_ID_LEN - 1;
    memcpy(slug, after, n); slug[n] = 0;
    const char *sub = slash + 1;
    /* le topic porte le SLUG -> on retrouve le vrai volet, puis on commande avec son id reel */
    volet_t *v = find_volet_by_slug(slug);
    if (!v) { ESP_LOGW(TAG, "MQTT cmd : volet slug '%s' inconnu", slug); return; }
    char id[SH_ID_LEN]; strlcpy(id, v->id, sizeof(id));
    char payload[16]; int m = len < 15 ? len : 15; memcpy(payload, data, m); payload[m] = 0;
    if (!strcmp(sub, "set")) {
        if      (!strcasecmp(payload, "OPEN"))  shutters_cmd(id, "up", 0);
        else if (!strcasecmp(payload, "CLOSE")) shutters_cmd(id, "down", 0);
        else if (!strcasecmp(payload, "STOP"))  shutters_cmd(id, "stop", 0);
    } else if (!strcmp(sub, "set_position")) {
        shutters_cmd(id, "pos", 100 - atoi(payload));   /* HA envoie le % d'ouverture -> on convertit en fermeture */
    }
}

/* L'ecoute radio est necessaire pour DEUX raisons independantes :
 *  - suivre la position via une vraie telecommande  -> des qu'au moins un volet est appris,
 *  - collecter le dataset slide                      -> si l'option "capture" est ON.
 * On ecoute donc si l'une OU l'autre est vraie. */
static void update_listening(void) {
    /* Ecoute permanente = uniquement si l'option est activee. Evite la saturation
     * (boucle RMT sur le bruit OOK) en usage normal. L'apprentissage et le rejeu
     * n'en dependent pas ; seuls le RF debug live, le suivi de position via la vraie
     * telecommande et le dataset MQTT necessitent cette ecoute. */
    radio_set_listening(s_log_frames);
}

void shutters_set_log_frames(bool on) {
    s_log_frames = on;
    update_listening();
    LOCK();
    save_cfg();                                        /* persiste l'ecoute permanente -> restauree au boot */
    for (int i = 0; i < s_nvolets; i++) announce_one(&s_volets[i]);   /* re-publie la decouverte : position apparait/disparait selon l'option */
    if (s_mqtt_ready) pub_defer("openprofalux/listen/state", on ? "ON" : "OFF", 0, 1);   /* etat du switch HA */
    UNLOCK();
    pub_flush();
    ESP_LOGI(TAG, "log_frames=%d", on);
}

/* Dispatch d'une trame recue par la tache radio -> sync position + frame-log. */
static void on_air(const char *bits, uint32_t serial, uint8_t button, uint32_t hop, int8_t rssi) {
    shutters_on_rx(bits, serial, button, rssi, hop);
}

/* Verifie GitHub au demarrage puis toutes les 6 h, et republie l'etat de l'entite update HA.
 * Tache dediee : ota_check_github() est bloquant (~s), a ne pas mettre dans tick_task. */
static void update_check_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(30000));   /* laisse WiFi + MQTT monter */
    while (1) {
        if (s_mqtt_ready) { ota_check_github(); publish_update_state(); pub_flush(); }
        vTaskDelay(pdMS_TO_TICKS(6UL * 3600UL * 1000UL));   /* toutes les 6 h */
    }
}

void shutters_init(void) {
    s_lock = xSemaphoreCreateMutex();
    /* L'ORDRE compte sur une NVS de 16 Ko saturee : le dataset de trames doit
     * quitter la NVS (load_frames) AVANT que la migration de la config ait besoin
     * de cette place pour ecrire ses cles par volet. */
    spiffs_mount();  /* EN PREMIER : dataset de trames et ring RF y vivent */
    cfg_layout_t lay = load_cfg();
    bool frames_stored = load_frames();   /* restaure le dataset borne ; reprend l'ancienne copie NVS si besoin */
    /* Ancien rangement -> nouveau : l'ordre et la regle d'effacement sont dans
     * cfg_store_finish_boot(), ou le banc de test les exerce tels quels. */
    esp_err_t mig = cfg_store_finish_boot(lay, frames_stored, save_cfg);
    if (mig != ESP_OK) ESP_LOGE(TAG, "migration config : ecriture KO (%s), ancien format conserve", esp_err_to_name(mig));
    else if (lay == CFG_LAYOUT_LEGACY) ESP_LOGI(TAG, "migration config : %d volets, une cle NVS par volet", s_nvolets);
    load_ring();     /* restaure les trames recentes rejouables (bits reconstruits) apres reboot */
    xTaskCreate(tick_task, "sh_tick", 4096, NULL, 5, NULL);
    xTaskCreate(update_check_task, "sh_upd", 6144, NULL, 4, NULL);   /* check GitHub -> entite update HA */
    radio_init();
    radio_start(on_air);   /* tache radio prete (arbitre TX) */
    update_listening();    /* ecoute si des volets sont deja appris (suivi position) ou si capture ON */
    ESP_LOGI(TAG, "init : %d volets, %d telecommandes, ecoute permanente=%s", s_nvolets, s_nremotes, s_log_frames ? "ON" : "OFF");
}
