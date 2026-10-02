/*
 * shutters.c — modele cover OpenProfalux (clone/replay + position time-based).
 * Config persistee en NVS, un document JSON par volet (voir cfg_store.h). Le
 * dataset de trames et le ring RF vivent en SPIFFS (partition "storage").
 */
#include "shutters.h"
#include "cfg_store.h"
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

typedef struct {
    char id[SH_ID_LEN];
    char serials[SH_MAX_SERIALS][SH_SERIAL_LEN];
    int  n_serials;
    char up[SH_BITS_LEN], down[SH_BITS_LEN], stop[SH_BITS_LEN];
    uint8_t up_btn, down_btn, stop_btn;   /* code bouton appris (pour la sync RX) */
    /* Volet VIRTUEL (identite 0x067 enrolee) : au lieu de rejouer up/down/stop captes,
     * on GENERE la trame a chaque appui (keeloq + compteur roulant). */
    bool     virt;
    uint32_t virt_serial;    /* serial 0x067 (contient le slot ; cle via pfx_key_for_serial) */
    uint16_t virt_counter;   /* compteur roulant courant */
    uint16_t virt_te;        /* TE d'emission du modele (415 FranciaFlex, 455 Profalux) */
    /* Centrale virtuelle : diffuse ouvrir/fermer/stop a un groupe de volets membres (CSV d'ids). */
    bool     central;
    char     members[SH_MEMBERS_LEN];   /* "id1,id2,..." */
    uint32_t travel_up_ms, travel_down_ms;
    int   orientation;       /* azimut de la facade (0..359, -1 = non defini) : automatisations soleil HA */
    int   order;             /* rang d'affichage dans l'UI (croissant). -1 = pas encore range :
                              * l'UI place alors le volet a la fin, dans son ordre de creation. */
    float position;          /* 0..100 */
    int   dir;               /* -1 down, 0 stop, +1 up */
    int   target;            /* -1 = aucun, sinon 0..100 */
    bool  own_move;          /* true = notre commande (on stream) ; false = externe (on suit sans emettre) */
    int64_t last_tick_us;
    int   pub_pos;           /* derniere position publiee MQTT (evite le spam) */
    int   pub_dir;           /* derniere direction publiee MQTT */
} volet_t;

/* dframe_t (trame distincte du dataset slide : hop brut + bouton + t) est defini dans shutters.h. */
typedef struct {
    char serial[SH_SERIAL_LEN]; char name[SH_ID_LEN];
    uint32_t  last_hop;      /* fast-path : dernier hop (evite le scan sur les maintiens) */
    dframe_t *hops;          /* SET des trames distinctes (dedup par hop) : hop + bouton + t */
    uint16_t  nhops;         /* = nb de trames DISTINCTES loggees */
    uint16_t  caphops;       /* capacite allouee du set */
} remote_t;

typedef struct { uint32_t t; char serial[SH_SERIAL_LEN]; uint8_t button; uint32_t hop; int8_t rssi; } rfrec_t;
/* LEGER (pas de bits stockes) : les 66 bits sont RECONSTRUITS a la demande depuis serial+bouton+hop
 * (build_frame_bits) pour le rejeu et l'adoption -> permet un ring de 1000 sans exploser la RAM. */

static volet_t  s_volets[SH_MAX_VOLETS];
static int      s_nvolets = 0;
static remote_t s_remotes[16];
static int      s_nremotes = 0;
#define RF_RING 1000          /* trames a l'affichage (paginees) ; l'export /api/frames = l'ensemble du dataset */
#define STATUS_RF_SHOW 20     /* nb de trames recentes mises dans /api/status (leger, poll 3 s) ; l'onglet RF pagine via /api/rf */
static rfrec_t  s_rf[RF_RING];
static int      s_rfhead = 0;
static SemaphoreHandle_t s_lock;
static char     s_device[32] = "op";   /* nom appareil (prefixe topics HA) */
static bool     s_mqtt_ready = false;   /* true apres shutters_mqtt_announce() */
static bool     s_log_frames = false;   /* publie toutes les trames captees en MQTT */
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
/* Un volet en JSON. C'est l'objet de l'export (Systeme > Sauvegarde), au champ
 * pres : la NVS range desormais un document par volet (cfg_store.h), mais le
 * FORMAT ne change pas, d'ou des sauvegardes compatibles dans les deux sens. */
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
static cJSON *hdr_to_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "log_frames", s_log_frames);   /* ecoute permanente : restauree au boot */
    cJSON *rem = cJSON_AddObjectToObject(root, "remotes");
    for (int i = 0; i < s_nremotes; i++) cJSON_AddStringToObject(rem, s_remotes[i].serial, s_remotes[i].name);
    return root;
}
/* Toute la config (telecommandes + noms + trames de reference + calibration) en un
 * document : format de l'export / import, et de l'ancienne cle NVS unique. */
static char *cfg_to_json(void) {
    cJSON *root = hdr_to_json();
    cJSON *vols = cJSON_AddArrayToObject(root, "volets");
    for (int i = 0; i < s_nvolets; i++) cJSON_AddItemToArray(vols, volet_to_json(&s_volets[i]));
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
 * identique. Les echecs sont journalises : une NVS pleine faisait autrefois
 * echouer TOUTES les sauvegardes sans le moindre message. */
static esp_err_t save_cfg(void) {
    cfg_store_tx_t tx;
    if (cfg_store_begin(&tx) == ESP_OK) {
        char *d = print_and_free(hdr_to_json());
        cfg_store_put_hdr(&tx, d); free(d);
        for (int i = 0; i < s_nvolets; i++) {
            d = print_and_free(volet_to_json(&s_volets[i]));
            cfg_store_put_volet(&tx, d); free(d);
        }
    }
    esp_err_t e = cfg_store_end(&tx);
    if (e != ESP_OK)
        ESP_LOGE(TAG, "save_cfg ECHEC sur '%s' : %s (%d volets)", tx.failed_key, esp_err_to_name(e), s_nvolets);
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
/* Au boot, apres load_cfg (rattache les hops aux telecommandes) et AVANT la
 * migration de la config : reprendre l'ancien dataset NVS libere la place dont
 * cette migration a besoin sur une NVS de 16 Ko saturee. */
static void load_frames(void) {
    static uint32_t buf[FRAMES_MAX * 4];
    size_t sz = 0;
    bool in_file = false;
    FILE *f = fopen(FRAMES_FILE, "rb");
    if (f) {
        sz = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        in_file = true;
    } else if ((sz = cfg_store_read_legacy_frames(buf, sizeof(buf))) > 0) {
        FILE *w = fopen(FRAMES_FILE, "wb");
        in_file = w && fwrite(buf, 1, sz, w) == sz;
        if (w) fclose(w);
        if (in_file) ESP_LOGI(TAG, "dataset de trames : NVS -> SPIFFS (%u o)", (unsigned)sz);
        else         ESP_LOGE(TAG, "dataset de trames : ecriture SPIFFS KO, il reste en NVS");
    }
    /* Copie NVS effacee seulement une fois le fichier en place (sans effet si deja
     * absente ; rattrape aussi une coupure entre l'ecriture et l'effacement). */
    if (in_file && cfg_store_drop_legacy_frames() != ESP_OK)
        ESP_LOGW(TAG, "dataset de trames : ancienne copie NVS non effacee");
    if (sz < 16) return;
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
    s_nremotes = 0; s_nvolets = 0;
}
/* Peuple s_remotes depuis l'en-tete (l'etat doit etre remis a zero avant). */
static void parse_hdr_json(cJSON *root) {
    cJSON *lf = cJSON_GetObjectItem(root, "log_frames");
    if (cJSON_IsBool(lf)) s_log_frames = cJSON_IsTrue(lf);   /* restaure l'ecoute permanente au boot */
    cJSON *rem = cJSON_GetObjectItem(root, "remotes");
    /* Objet attendu : dans un tableau, it->string est NULL et strlcpy plantait
     * (redemarrage provoque par une restauration malformee). */
    if (!cJSON_IsObject(rem)) return;
    for (cJSON *it = rem->child; it && s_nremotes < 16; it = it->next) {
        if (!it->string) continue;
        strlcpy(s_remotes[s_nremotes].serial, it->string, SH_SERIAL_LEN);
        strlcpy(s_remotes[s_nremotes].name, cJSON_IsString(it) ? it->valuestring : "", SH_ID_LEN);
        s_remotes[s_nremotes].hops = NULL; s_remotes[s_nremotes].nhops = 0; s_remotes[s_nremotes].caphops = 0;
        s_nremotes++;
    }
}
/* Ajoute UN volet depuis son document JSON. */
static void parse_volet_json(cJSON *o) {
    if (s_nvolets >= SH_MAX_VOLETS) return;
    char id[SH_ID_LEN];
    strlcpy(id, cJSON_GetStringValue(cJSON_GetObjectItem(o, "id")) ?: "", sizeof(id));
    /* Identifiant deja charge : ignore. Ne se produit pas en temps normal, mais une
     * coupure au milieu de la suppression d'un volet (les cles suivantes sont
     * renumerotees) peut laisser deux fois le meme document. */
    for (int i = 0; i < s_nvolets; i++)
        if (!strcmp(s_volets[i].id, id)) { ESP_LOGW(TAG, "volet '%s' en double, ignore", id); return; }
    volet_t *v = &s_volets[s_nvolets++];
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
/* Config complete (export, ou ancienne cle NVS unique). L'etat doit etre remis a zero avant. */
static void parse_cfg_json(cJSON *root) {
    parse_hdr_json(root);
    cJSON *vols = cJSON_GetObjectItem(root, "volets");
    for (cJSON *o = vols ? vols->child : NULL; o; o = o->next) parse_volet_json(o);
}
static void on_cfg_doc(void *ctx, int index, const char *doc) {
    (void)ctx;
    cJSON *j = cJSON_Parse(doc);
    if (!j) { ESP_LOGE(TAG, "load_cfg : document %d illisible", index); return; }
    if (index == CFG_DOC_LEGACY)   parse_cfg_json(j);
    else if (index == CFG_DOC_HDR) parse_hdr_json(j);
    else                           parse_volet_json(j);
    cJSON_Delete(j);
}
static cfg_layout_t load_cfg(void) {
    cfg_layout_t lay = cfg_store_load(on_cfg_doc, NULL);
    for (int i = 0; i < s_nvolets; i++)   /* DIAGNOSTIC : verifie que la calibration est bien relue */
        ESP_LOGI(TAG, "cfg volet '%s' : up=%u ms down=%u ms pos=%d",
                 s_volets[i].id, (unsigned)s_volets[i].travel_up_ms, (unsigned)s_volets[i].travel_down_ms, (int)(s_volets[i].position + 0.5f));
    return lay;
}
/* Ancienne cle unique -> une cle par volet. L'ancienne n'est effacee qu'APRES une
 * ecriture reussie au nouveau format : une coupure a n'importe quel moment laisse
 * une config lisible, et un echec (NVS trop pleine) est retente au demarrage suivant.
 * Au nouveau format, l'effacement rattrape une coupure survenue juste avant lui. */
static void migrate_cfg(cfg_layout_t lay) {
    if (lay == CFG_LAYOUT_LEGACY) {
        if (save_cfg() != ESP_OK) {
            ESP_LOGE(TAG, "migration config : ecriture KO, ancien format conserve");
            return;
        }
        ESP_LOGI(TAG, "migration config : %d volets, une cle NVS par volet", s_nvolets);
    }
    if (lay != CFG_LAYOUT_NONE && cfg_store_drop_legacy() != ESP_OK)
        ESP_LOGW(TAG, "migration config : ancienne cle non effacee");
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
    for (int i = idx; i < s_nvolets - 1; i++) s_volets[i] = s_volets[i + 1];
    s_nvolets--;
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
        char buf[SH_MEMBERS_LEN]; strlcpy(buf, v->members, sizeof(buf));
        char *sv = NULL;
        for (char *tok = strtok_r(buf, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
            while (*tok == ' ') tok++;
            volet_t *m = find_volet(tok);
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
    volet_t *v = get_or_create(id);
    if (!v) { UNLOCK(); return -1; }
    v->central = true; v->virt = false;
    v->position = 50; v->dir = 0; v->target = -1;
    strlcpy(v->members, members_csv ? members_csv : "", SH_MEMBERS_LEN);
    save_cfg();
    announce_one(v);
    UNLOCK();
    pub_flush();
    ESP_LOGW(TAG, "centrale '%s' creee : membres=[%s]", id, v->members);
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
     * un tampon fixe de SH_MEMBERS_LEN la tronquait au-dela de 384 octets. Sur
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
        if (v->central) { cJSON_AddBoolToObject(o, "central", true); cJSON_AddStringToObject(o, "members", v->members); }
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
    char *js = cfg_to_json();
    UNLOCK();
    return js;
}
int shutters_import_json(const char *js) {
    if (!js) return -1;
    cJSON *root = cJSON_Parse(js);
    if (!root) return -1;
    LOCK();
    reset_state();
    parse_cfg_json(root);
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
    load_frames();   /* restaure le dataset borne ; reprend l'ancienne copie NVS si besoin */
    migrate_cfg(lay);
    load_ring();     /* restaure les trames recentes rejouables (bits reconstruits) apres reboot */
    xTaskCreate(tick_task, "sh_tick", 4096, NULL, 5, NULL);
    xTaskCreate(update_check_task, "sh_upd", 6144, NULL, 4, NULL);   /* check GitHub -> entite update HA */
    radio_init();
    radio_start(on_air);   /* tache radio prete (arbitre TX) */
    update_listening();    /* ecoute si des volets sont deja appris (suivi position) ou si capture ON */
    ESP_LOGI(TAG, "init : %d volets, %d telecommandes, ecoute permanente=%s", s_nvolets, s_nremotes, s_log_frames ? "ON" : "OFF");
}
