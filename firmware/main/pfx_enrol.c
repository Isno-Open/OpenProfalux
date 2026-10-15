/*
 * pfx_enrol.c — enrôlement d'une identité virtuelle 0x067 (voir pfx_enrol.h).
 *
 * La génération de trame réplique EXACTEMENT le banc (firmware-capture-test : pfx_frame_build +
 * tx067_one) : plaintext = button<<28 | discrim<<16 | counter ; hop = KeeLoq(plaintext, clé du
 * slot) ; hop et serial sérialisés LSB-first ; statut '01' (RPT=1). L'émission passe par le même
 * chemin PWM que le rejeu (radio_tx -> cc1101_tx_raw_bits), au TE du modèle choisi.
 */
#include "pfx_enrol.h"
#include "keeloq.h"
#include "pfx_keys.h"
#include "shutters.h"
#include "radio.h"
#include "cc1101.h"
#include <string.h>
#include "nvs.h"
#include "cJSON.h"
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG    = "pfx_enrol";
static const char *NVS_NS = "pfx_enrol";

/* Rafale par appui = ~9 trames (mesuré sur trames réelles Lecanard). Compteur figé sur la rafale. */
#define PFX_BURST     9
/* Apprentissage tenu ~5 s (comme STOP-en-P maintenu 5 s) : le moteur ne confirme
 * (va-et-vient) qu'apres ~5 s. ~5 salves de PFX_BURST ~= 5 s. */
#define PFX_LEARN_ROUNDS 5
/* Directions (montée/arrêt/descente) = PFX_BTN_* dans pfx_enrol.h (partagées avec shutters). */
#define BTN_LEARN   0x5   /* trame d'apprentissage radio */

/* Catalogue : tous routés canal PFX 0x636f, famille 0x067, même table de clés.
 * Seuls le TE et la chorégraphie (R6/R7/R8) diffèrent par modèle. */
static const pfx_model_t MODELS[] = {
    { "Profalux MAI-EMPX / MUR / NOE", 455, "R6" },
    { "FranciaFlex M4G",               415, "R6" },
    { "Profalux NeoSol",               455, "R8" },
    { "Profalux MAI-RDPX / RDTELPX",   455, "R7" },
};
#define N_MODELS ((int)(sizeof(MODELS) / sizeof(MODELS[0])))

static pfx_ident_t s_id;

/* Table persistante des moteurs enrôlés : chaque vraie télécommande (remote serial) est
 * associée DÉFINITIVEMENT à un slot 0x067 + son compteur roulant. Sert d'allocateur sans
 * conflit (un slot pris n'est jamais réattribué) et de rappel (remote connu -> même identité). */
typedef struct __attribute__((packed)) {
    uint32_t remote;    /* serial 28b de la vraie télécommande (clé) */
    uint16_t counter;   /* compteur roulant courant de l'identité */
    uint8_t  slot;      /* slot attribué [1,63] */
} pfx_entry_t;
#define PFX_MAP_MAX PFX_KEY_COUNT
static pfx_entry_t s_map[PFX_MAP_MAX];
static int         s_map_n;

/* Slots dont le compteur de la table est SUR (bit = slot), NVS map_ok : identite neuve, compteur
 * reporte a la suppression du volet, ou enrolement CONFIRME (volet virtuel cree, l'utilisateur a
 * vu le moteur repondre). Une entree non confirmee (ancien firmware, ou re-enrolement pas encore
 * confirme) peut etre en retard d'un nombre inconnu de commandes : son re-enrolement saute
 * (voir pfx_enrol_capture). La marque n'est JAMAIS posee avant de savoir que le moteur a accepte :
 * un echec ne doit pas consommer le saut (issue #45, retour terrain). La cle s'appelait map_sync
 * dans un build de test qui marquait trop tot : elle est ignoree. */
static uint64_t s_map_ok;
static uint8_t  s_map_try[PFX_KEY_COUNT + 1];   /* sauts deja tentes par slot (NVS map_try) */
/* Saut au re-enrolement d'une entree non confirmee, croissant a chaque essai rate. Un decodeur
 * KeeLoq accepte un code au-dela de sa fenetre directe (+16) s'il recoit DEUX codes consecutifs
 * (l'apprentissage n, puis la 1re commande n+1), jusqu'a sa fenetre de resync (32768 en standard).
 * Prouve sur FranciaFlex M4G a +1024 ; on part petit au cas ou un recepteur aurait une fenetre plus
 * etroite. A 16384, 3 essais au plus rattrapent n'importe quel compteur (fenetre de 32768). */
static const uint16_t PFX_RESYNC_STEPS[] = { 1024, 4096, 16384 };
#define N_RESYNC_STEPS ((int)(sizeof(PFX_RESYNC_STEPS) / sizeof(PFX_RESYNC_STEPS[0])))

static void map_load(void) {
    s_map_n = 0; memset(s_map, 0, sizeof(s_map)); s_map_ok = 0; memset(s_map_try, 0, sizeof(s_map_try));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_map);
    if (nvs_get_blob(h, "map", s_map, &len) == ESP_OK) s_map_n = (int)(len / sizeof(pfx_entry_t));
    nvs_get_u64(h, "map_ok", &s_map_ok);
    len = sizeof(s_map_try); nvs_get_blob(h, "map_try", s_map_try, &len);
    nvs_close(h);
    if (s_map_n > PFX_MAP_MAX) s_map_n = PFX_MAP_MAX;
}
static void map_save(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "map", s_map, (size_t)s_map_n * sizeof(pfx_entry_t));
    nvs_set_u64(h, "map_ok", s_map_ok);
    nvs_set_blob(h, "map_try", s_map_try, sizeof(s_map_try));
    nvs_commit(h); nvs_close(h);
}
static inline bool map_slot_ok(uint8_t slot) { return slot >= 1 && slot <= PFX_KEY_COUNT; }
/* Compteur sur : marque posee, tentatives remises a zero. */
static inline void map_mark_ok(uint8_t slot) { if (map_slot_ok(slot)) { s_map_ok |= 1ULL << slot; s_map_try[slot] = 0; } }
static inline bool map_is_ok(uint8_t slot)   { return map_slot_ok(slot) && (s_map_ok >> slot & 1); }
/* Prochain slot libre [1,63] : ni dans la table, ni celui de l'identité active courante
 * (protège une identité pas encore dans la table). 0 si tous pris. */
static uint8_t map_free_slot(void) {
    for (uint8_t slot = 1; slot <= PFX_KEY_COUNT; slot++) {
        bool taken = (s_id.active && (uint8_t)(s_id.idx + 1) == slot);
        for (int i = 0; i < s_map_n && !taken; i++) if (s_map[i].slot == slot) taken = true;
        /* exclut aussi les slots déjà utilisés par un volet virtuel (pas de conflit) */
        if (!taken && shutters_virt_serial_used(((uint32_t)slot << 12) | PFX_FAMILY_MARKER)) taken = true;
        if (!taken) return slot;
    }
    return 0;
}
static pfx_entry_t *map_find(uint32_t remote) {
    if (!remote) return NULL;
    for (int i = 0; i < s_map_n; i++) if (s_map[i].remote == remote) return &s_map[i];
    return NULL;
}

/* Compteur 16 bits qui reboucle : a est DEVANT b s'il le precede de moins d'un demi-tour. */
static inline bool cnt_ahead(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

int                pfx_enrol_model_count(void) { return N_MODELS; }
const pfx_model_t *pfx_enrol_model(int id)     { return (id >= 0 && id < N_MODELS) ? &MODELS[id] : NULL; }

static void save(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "active", s_id.active ? 1 : 0);
    nvs_set_u8(h, "model", (uint8_t)s_id.model);
    nvs_set_u8(h, "idx", s_id.idx);
    nvs_set_u32(h, "serial", s_id.serial);
    nvs_set_u16(h, "counter", s_id.counter);
    nvs_set_u64(h, "key", s_id.key);
    nvs_set_u32(h, "remote", s_id.remote);
    nvs_commit(h); nvs_close(h);
    /* garde le compteur de la table à jour pour cette télécommande (compteur roulant par identité) */
    if (s_id.remote) {
        pfx_entry_t *e = map_find(s_id.remote);
        /* suit le compteur emis, sans jamais le faire reculer ni marquer l'entree sure */
        if (e && cnt_ahead(s_id.counter, e->counter)) { e->counter = s_id.counter; map_save(); }
    }
}

void pfx_enrol_init(void) {
    memset(&s_id, 0, sizeof(s_id));
    map_load();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t a = 0, m = 0, ix = 0; uint32_t sr = 0, rem = 0; uint16_t c = 0; uint64_t key = 0;
        nvs_get_u8(h, "active", &a); nvs_get_u8(h, "model", &m); nvs_get_u8(h, "idx", &ix);
        nvs_get_u32(h, "serial", &sr); nvs_get_u16(h, "counter", &c);
        nvs_get_u64(h, "key", &key); nvs_get_u32(h, "remote", &rem);
        nvs_close(h);
        s_id.active = a; s_id.model = (m < N_MODELS) ? m : 0; s_id.idx = ix;
        s_id.serial = sr; s_id.counter = c; s_id.key = key; s_id.remote = rem;
        /* compat : ancienne identité sans clé stockée -> re-dérive de la table. */
        if (s_id.key == 0 && s_id.active)
            pfx_key_for_serial(s_id.serial, &s_id.key, NULL);
    }
}

bool pfx_enrol_get(pfx_ident_t *out) { if (out) *out = s_id; return s_id.active; }

/* Charge une identité (slot + compteur) comme identité courante et la persiste. */
static void load_slot(uint8_t slot, uint16_t counter, int model, uint32_t remote) {
    uint32_t serial = ((uint32_t)slot << 12) | PFX_FAMILY_MARKER;
    uint64_t key = 0; uint8_t idx = (uint8_t)(slot - 1);
    pfx_key_for_serial(serial, &key, &idx);
    s_id.active = true;
    s_id.model  = (model >= 0 && model < N_MODELS) ? model : s_id.model;
    s_id.idx = idx; s_id.serial = serial; s_id.counter = counter; s_id.key = key; s_id.remote = remote;
    save();
}

int pfx_enrol_capture(uint32_t remote, int model, bool *is_new) {
    if (is_new) *is_new = false;
    if (!remote) return -2;
    pfx_entry_t *e = map_find(remote);
    if (e) {                                   /* moteur déjà connu -> même identité, compteur conservé */
        uint32_t serial = ((uint32_t)e->slot << 12) | PFX_FAMILY_MARKER;
        uint16_t cnt = e->counter;
        uint16_t vc  = shutters_virt_counter_for_serial(serial);  /* compteur réellement émis par le volet */
        if (vc && cnt_ahead(vc, cnt)) cnt = vc;   /* on repart TOUJOURS devant le moteur (jamais un rejeu) */
        if (!map_is_ok(e->slot)) {             /* non confirmée : retard inconnu, saut croissant.
                                                * Vaut aussi pour un volet restauré d'une sauvegarde,
                                                * dont le compteur est une photo. */
            uint8_t t = s_map_try[e->slot];
            uint16_t step = PFX_RESYNC_STEPS[t < N_RESYNC_STEPS ? t : N_RESYNC_STEPS - 1];
            ESP_LOGW(TAG, "slot %u : compteur %u non confirme, essai %u : saut de %u pour la resync KeeLoq",
                     e->slot, cnt, (unsigned)t + 1, step);
            cnt = (uint16_t)(cnt + step);
            if (t < 255) s_map_try[e->slot] = t + 1;
        }
        e->counter = cnt; map_save();          /* marque posée seulement à la confirmation */
        load_slot(e->slot, cnt, model, remote);
        ESP_LOGW(TAG, "telecommande 0x%07X connue -> identite existante slot=%u serial=0x%07X cnt=%u",
                 (unsigned)remote, e->slot, (unsigned)s_id.serial, cnt);
        return 0;
    }
    if (s_map_n >= PFX_MAP_MAX) { ESP_LOGW(TAG, "table pleine : 63 identites deja prises"); return -1; }
    uint8_t slot = map_free_slot();            /* moteur inconnu -> prochain slot libre */
    if (!slot) { ESP_LOGW(TAG, "aucun slot libre"); return -1; }
    s_map[s_map_n].remote = remote; s_map[s_map_n].counter = 2; s_map[s_map_n].slot = slot; s_map_n++;
    map_mark_ok(slot);                         /* identité neuve : compteur sûr */
    map_save();                                /* mémorisé DÉFINITIVEMENT */
    load_slot(slot, 2, model, remote);
    if (is_new) *is_new = true;
    ESP_LOGW(TAG, "telecommande 0x%07X inconnue -> nouvelle identite slot=%u serial=0x%07X (memorisee)",
             (unsigned)remote, slot, (unsigned)s_id.serial);
    return 0;
}

void pfx_enrol_note_counter(uint32_t serial, uint16_t next) {
    if ((serial & 0x3FF) != PFX_FAMILY_MARKER) return;
    uint8_t slot = (uint8_t)(serial >> 12);
    for (int i = 0; i < s_map_n; i++) if (s_map[i].slot == slot) {
        /* Compteur reporte ; la marque ne change pas : un volet cree depuis ce correctif a ete
         * confirme a sa creation, un volet plus ancien ou restaure reste a confirmer. */
        if (cnt_ahead(next, s_map[i].counter)) s_map[i].counter = next;
        ESP_LOGI(TAG, "slot %u : compteur %u reporte (volet supprime)%s", slot, s_map[i].counter,
                 map_is_ok(slot) ? "" : ", non confirme");
        map_save();
        return;
    }
}

/* Sauvegarde : la table d'enrolement (telecommande -> slot + compteur). Sans elle, un
 * effacement de la NVS faisait capter un moteur deja enrole comme un inconnu. */
cJSON *pfx_enrol_export(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_map_n; i++) {
        cJSON *o = cJSON_CreateObject();
        char rx[12]; snprintf(rx, sizeof(rx), "0x%07X", (unsigned)s_map[i].remote);
        cJSON_AddStringToObject(o, "remote", rx);
        cJSON_AddNumberToObject(o, "slot", s_map[i].slot);
        cJSON_AddNumberToObject(o, "counter", s_map[i].counter);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}
/* Restauration : fusion avec la table en place. Une sauvegarde est une photo : le moteur a pu
 * avancer depuis. On garde le compteur le plus avance, et une entree qui vient de la sauvegarde
 * est NON confirmee (son re-enrolement sautera). Une entree dont le slot est deja pris par une
 * autre telecommande est ignoree (jamais deux telecommandes sur un slot). */
void pfx_enrol_import(const cJSON *arr) {
    if (!cJSON_IsArray(arr)) return;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        const char *rs = cJSON_GetStringValue(cJSON_GetObjectItem(o, "remote"));
        const cJSON *js = cJSON_GetObjectItem(o, "slot"), *jc = cJSON_GetObjectItem(o, "counter");
        if (!rs || !cJSON_IsNumber(js) || !cJSON_IsNumber(jc)) continue;
        uint32_t remote = (uint32_t)strtoul(rs, NULL, 16);
        uint8_t slot = (uint8_t)js->valuedouble; uint16_t cnt = (uint16_t)jc->valuedouble;
        if (!remote || !map_slot_ok(slot)) continue;
        pfx_entry_t *e = map_find(remote);
        if (e) {
            if (e->slot != slot) continue;
            if (cnt_ahead(cnt, e->counter)) { e->counter = cnt; s_map_ok &= ~(1ULL << slot); }
            continue;
        }
        bool taken = false;
        for (int i = 0; i < s_map_n; i++) if (s_map[i].slot == slot) taken = true;
        if (taken || s_map_n >= PFX_MAP_MAX) { ESP_LOGW(TAG, "restauration : slot %u deja pris, entree 0x%07X ignoree", slot, (unsigned)remote); continue; }
        s_map[s_map_n].remote = remote; s_map[s_map_n].slot = slot; s_map[s_map_n].counter = cnt; s_map_n++;
        s_map_ok &= ~(1ULL << slot); s_map_try[slot] = 0;
    }
    map_save();
}
/* Un volet virtuel restaure porte un compteur-photo : son identite redevient non confirmee. */
void pfx_enrol_unconfirm(uint32_t serial) {
    if ((serial & 0x3FF) != PFX_FAMILY_MARKER) return;
    uint8_t slot = (uint8_t)(serial >> 12);
    if (!map_is_ok(slot)) return;
    s_map_ok &= ~(1ULL << slot); map_save();
}

void pfx_enrol_confirm(void) {
    if (!s_id.active || !s_id.remote) return;
    pfx_entry_t *e = map_find(s_id.remote);
    if (!e) return;
    if (cnt_ahead(s_id.counter, e->counter)) e->counter = s_id.counter;
    map_mark_ok(e->slot); map_save();
    ESP_LOGI(TAG, "slot %u : enrolement confirme, compteur %u", e->slot, e->counter);
}

void pfx_enrol_forget(void) {
    uint32_t sv = s_id.serial, rem = s_id.remote;
    /* NE supprime PAS l'entrée de la table : slot ET compteur restent mémorisés définitivement.
     * « Oublier » ne fait que désélectionner l'identité active ; ré-appuyer sur la télécommande
     * la récupère avec son compteur (jamais remis à zéro) -> un moteur strict l'accepte toujours. */
    if (rem) {                                 /* fige le dernier compteur avant de désélectionner */
        pfx_entry_t *e = map_find(rem);
        if (e && cnt_ahead(s_id.counter, e->counter)) { e->counter = s_id.counter; map_save(); }
    }
    memset(&s_id, 0, sizeof(s_id)); save();
    ESP_LOGW(TAG, "identite deselectionnee (etait 0x%07X, telecommande 0x%07X : compteur conserve)",
             (unsigned)sv, (unsigned)rem);
}

/* rev4 : inverse les 4 bits du bouton (LSB-first sur l'air, fix anti-tamper). */
static inline uint8_t rev4(uint8_t b) {
    return (uint8_t)(((b & 1) << 3) | ((b & 2) << 1) | ((b & 4) >> 1) | ((b & 8) >> 3));
}

/* Cœur : construit la trame 66 bits on-air ('0'/'1', out[66]=NUL) pour un serial + une clé DONNÉE. */
static void frame_with_key(uint32_t serial, uint64_t key, uint8_t button, uint16_t counter, char out[68]) {
    uint16_t discrim = (uint16_t)(serial & 0xFFF);
    uint32_t plain = ((uint32_t)(button & 0xF) << 28) | ((uint32_t)(discrim & 0xFFF) << 16) | counter;
    uint32_t enc = keeloq_encrypt(plain, key);
    uint32_t enc_tx = 0;                                   /* hop LSB-first */
    for (int k = 0; k < 32; k++) enc_tx |= ((enc >> k) & 1u) << (31 - k);
    uint8_t frame[9]; memset(frame, 0, 9);
    frame[0] = (enc_tx >> 24) & 0xFF; frame[1] = (enc_tx >> 16) & 0xFF;
    frame[2] = (enc_tx >>  8) & 0xFF; frame[3] =  enc_tx        & 0xFF;
    uint32_t sr = 0;                                       /* serial 28b LSB-first */
    for (int k = 0; k < 28; k++) sr |= ((serial >> k) & 1u) << (27 - k);
    frame[4] = (sr >> 20) & 0xFF; frame[5] = (sr >> 12) & 0xFF; frame[6] = (sr >> 4) & 0xFF;
    frame[7] = ((sr & 0xF) << 4) | rev4(button);
    frame[8] = 0x40;                                       /* statut '01' = RPT=1 */
    for (int i = 0; i < 66; i++) out[i] = ((frame[i / 8] >> (7 - (i % 8))) & 1) ? '1' : '0';
    out[66] = 0;
}

/* Public : trame pour une identité 0x067 (clé lue dans la table). Utilisé par shutters. */
int pfx_enrol_frame(uint32_t serial, uint8_t button, uint16_t counter, char out[68]) {
    uint64_t key = 0;
    if (!pfx_key_for_serial(serial, &key, NULL)) { out[0] = 0; return -1; }
    frame_with_key(serial, key, button, counter, out);
    return 0;
}

uint16_t pfx_enrol_model_te(int model) {
    return (model >= 0 && model < N_MODELS) ? MODELS[model].te_us : 455;
}

/* Lit le TE d'émission configuré (cfg/tx_te) pour le restaurer après notre émission. */
static uint32_t cfg_tx_te(void) {
    uint32_t te = 455; nvs_handle_t h;
    if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
        uint32_t t; if (nvs_get_u32(h, "tx_te", &t) == ESP_OK && t) te = t;
        nvs_close(h);
    }
    return te;
}

static int emit(uint8_t button, uint16_t counter) {
    if (!s_id.active) return -1;
    const pfx_model_t *m = &MODELS[s_id.model];
    char bits[68]; frame_with_key(s_id.serial, s_id.key, button, counter, bits);
    uint32_t prev_te = cfg_tx_te();
    cc1101_set_tx_te(m->te_us);          /* TE du modèle le temps de la rafale */
    radio_tx(bits, PFX_BURST);           /* rafale, compteur figé sur toute la rafale */
    cc1101_set_tx_te(prev_te);           /* restaure le TE de rejeu configuré */
    ESP_LOGW(TAG, "TX 0x067 serial=0x%07X btn=0x%X cnt=%u te=%u",
             (unsigned)s_id.serial, button, counter, m->te_us);
    return 0;
}

int pfx_enrol_emit_learn(void) {
    if (!s_id.active) return -1;
    /* Trame d'apprentissage (btn5) tenue ~5 s : on répète la même trame en salves, avec un
     * yield entre chaque (nourrit le watchdog, évite un bit-bang bloquant de 5 s d'un coup).
     * Le moteur ne fait son va-et-vient de confirmation qu'après ~5 s.
     *
     * Compteur = le compteur COURANT de l'identité : 3 pour une identité neuve (capture à 2),
     * le compteur conservé pour un moteur déjà connu. Il était figé à 3, puis les commandes
     * repartaient de 4 : ré-enrôler un moteur connu émettait DERRIÈRE son compteur, rejeté
     * comme un rejeu (issue #45). Réserve-puis-émets, comme pfx_enrol_cmd. */
    const pfx_model_t *m = &MODELS[s_id.model];
    uint16_t cnt = s_id.counter < 3 ? 3 : s_id.counter;
    s_id.counter = cnt + 1;   /* commandes à partir du suivant */
    save();
    char bits[68];
    frame_with_key(s_id.serial, s_id.key, BTN_LEARN, cnt, bits);
    uint32_t prev_te = cfg_tx_te();
    cc1101_set_tx_te(m->te_us);
    for (int i = 0; i < PFX_LEARN_ROUNDS; i++) {
        radio_tx(bits, PFX_BURST);
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    cc1101_set_tx_te(prev_te);
    ESP_LOGW(TAG, "TX 0x067 APPRENTISSAGE serial=0x%07X btn=0x5 cnt=%u (~5s, %d salves) te=%u",
             (unsigned)s_id.serial, cnt, PFX_LEARN_ROUNDS, m->te_us);
    return 0;
}

int pfx_enrol_cmd(const char *cmd) {
    if (!s_id.active || !cmd) return -1;
    uint8_t b;
    if      (!strcmp(cmd, "up"))   b = PFX_BTN_UP;
    else if (!strcmp(cmd, "stop")) b = PFX_BTN_STOP;
    else if (!strcmp(cmd, "down")) b = PFX_BTN_DOWN;
    else return -1;
    /* Réserve-puis-émets : on persiste le PROCHAIN compteur AVANT d'émettre (jamais de rejeu
     * au reboot ; au pire un compteur sauté, toléré par la fenêtre KeeLoq du moteur). */
    uint16_t cnt = s_id.counter;
    s_id.counter = cnt + 1;
    save();
    return emit(b, cnt);
}
