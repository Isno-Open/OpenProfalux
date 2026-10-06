/*
 * console.c — Banc de test enrolement DEVMEL R6 (pilotage serie).
 *
 * Reproduit la procedure AirSend R6 : OpenProfalux REJOUE toute la sequence de
 * mouvements (trames captees de la vraie telecommande) et S'INSERE AU MILIEU en emettant
 * une identite virtuelle 0x067 (settings + shutter). L'utilisateur positionne juste le
 * volet au milieu au depart. Marche car le moteur accepte le rejeu (anti-rejeu casse).
 *
 * Commandes (miniterm sur COM4) :
 *   help
 *   newid                 -> tire une identite 0x067 fraiche au hasard (clef du pool, counter=2)
 *   learn up|stop|down    -> capture RX une trame de la vraie telecommande (a rejouer)
 *   settime up=<ms> down=<ms> down20=<ms>
 *   set settings=<hex> shutter=<hex> burst=<n> gap=<ms>
 *   emit                  -> injection seule : settings (5 s) + shutter
 *   up|stop|down          -> commande en 0x067 (apres enrolement), compteur roulant
 *   enroll                -> R6 COMPLET auto : rejeu Phase A + injection + rejeu Phase B
 *   status
 *
 * Tout est logue trame par trame (tag "BANC").
 */
#include <string.h>
#include <stdlib.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "profalux.h"
#include "pfx_keys.h"
#include "keeloq.h"
#include "cc1101.h"

static const char *TAG = "BANC";

/* ---- Etat du banc ---- */
static pfx_tx_state_t s_id;              /* identite 0x067 courante */
static bool     s_id_ok = false;
static int      s_step = 0;              /* etape de la procedure guidee (0=debut, 1=apres PhaseA, 2=apres PhaseB) */

typedef enum { CAP_UP = 0, CAP_STOP = 1, CAP_DOWN = 2 } cap_idx_t;
static struct { char bits[80]; int n; bool have; } s_cap[3];   /* trames reelles captees */
static const char *CAP_NAME[3] = { "UP", "STOP", "DOWN" };

/* ---- Reglages ---- */
static int      s_burst        = 9;      /* trames 0x067 par commande (mesure trames reelles Lecanard ~9) */
static uint32_t s_timeout_ms   = 5000;   /* MIDDLE demarre a t=timeout depuis le DEBUT de SETTINGS (APK &timeout=) */
#define DEVMEL_FRAME_MS 114              /* start-to-start entre 2 trames DEVMEL (mesure ~113.5 ms) */

/* Carte des boutons DEVMEL 0x067 (prouvee sur trames reelles). NE PAS confondre avec les
 * PFX_BTN_* (convention Profalux : DOWN=0x1) : ici DEVMEL DOWN = 0x8. */
#define DEV_BTN_SHUT50 0x5               /* shutter LEVEL=50 (volet 50%) = SEULE trame radio de l'injection */
#define DEV_BTN_STOP  0x2                /* event 17 */
#define DEV_BTN_UP    0x4                /* event 35 / OPEN 22 */
#define DEV_BTN_DOWN  0x8                /* event 34 / CLOSE 21 */
static uint32_t s_gap_ms       = 700;    /* DIGESTION entre phases (apres STOP, autour injection) ; set gap= */
/* Modele IMPULSION (comme un humain) : appui bref -> le moteur bouge seul -> on ATTEND.
 * On n'emet PAS pendant que le volet voyage. */
static uint32_t s_mid_top_ms   = 7000;   /* Phase A : montee position depart -> butee (settime midtop=) */
static uint32_t s_return_ms    = 3500;   /* Phase B : montee pres-du-haut -> butee, plus court (settime return=) */
static uint32_t s_down20_ms    = 2000;   /* DESCENTE ~20 cm Phase A : DOWN MAINTENU cette duree */
static uint32_t s_down3_ms     = 3000;   /* DESCENTE Phase B : DOWN MAINTENU 3 s (R6) */
static uint32_t s_release_ms   = 300;    /* relache court : settings->shutter, DOWN->STOP (settime release=) */

/* Trames reelles "Chambre Parent" (serial 0x0000813), extraites de la NVS de l'ESP32.
 * Rejeu direct (le moteur accepte le rejeu). 66 bits chacune. */
static const char *CP_UP   = "111001011001111111111010100001001100100000010000000000000000010000";
static const char *CP_DOWN = "011000001111010001011101111010011100100000010000000000000000000100";
static const char *CP_STOP = "011101100000000110111011010010101100100000010000000000000000001000";

/* ================= Emission 0x067 ================= */

/* Enveloppe OOK utilisee pour emettre l'identite 0x067 :
 *   true  = enveloppe PROFALUX PROUVEE (TE 455, header long-L 4450) = celle que le moteur
 *           d'Olivier decode a coup sur (le rejeu de sa telecommande marche).
 *   false = enveloppe DEVMEL mesuree (TE 410, header 1H+10L) = profil du vrai boitier.
 * Les BITS emis sont identiques dans les deux cas ; seule l'enveloppe change. Toggle 'env'. */
static bool s_env_proven = true;

/* Emet UNE trame 0x067 (bouton donne, compteur courant) + log. NE touche pas au compteur. */
static void tx067_one(uint8_t button) {
    uint8_t frame[9];
    pfx_frame_build(&s_id, button, frame);
    /* RPT=1 comme DEVMEL (toute la rafale marquee repetition). CORRECTION (re-analyse 24/08) :
     * le bit RPT est le bit 65 (dernier), pas le 64. Les vraies rafales (EMPX 0x813 ET DEVMEL
     * slot-6) finissent en statut '01' = VLOW=0,RPT=1. On mettait 0x80 (bit64) = VLOW=1,RPT=0,
     * l'inverse. -> frame[8]=0x40 (bit65) => statut '01' comme le reel. */
    frame[8] = 0x40;
    uint32_t plain = ((uint32_t)(button & 0xF) << 28)
                   | ((uint32_t)(s_id.discrimination & 0xFFF) << 16) | s_id.counter;
    uint32_t hop = keeloq_encrypt(plain, s_id.crypt_key);
    ESP_LOGW(TAG, "TX 0x067 serial=0x%07X btn=0x%X cnt=%u RPT=1 plain=0x%08X hop=0x%08X (env=%s)",
             (unsigned)s_id.serial, button, (unsigned)s_id.counter, (unsigned)plain, (unsigned)hop,
             s_env_proven ? "PROFALUX-455" : "DEVMEL-410");
    if (s_env_proven) {
        /* memes bits, mais enveloppe TE455/header long-L que le moteur decode (rejeu prouve) */
        char bits[67];
        for (int i = 0; i < 66; i++) bits[i] = ((frame[i / 8] >> (7 - (i % 8))) & 1) ? '1' : '0';
        bits[66] = 0;
        cc1101_tx_raw_bits(bits, 66);
    } else {
        cc1101_tx_devmel_frame(frame, 66);   /* profil DEVMEL PFX (TE410) */
    }
}

/* Un "appui" 0x067 = rafale de s_burst trames au meme compteur, puis compteur++. */
/* Emet n trames 0x067 (meme bouton, meme compteur) a 108 ms start-to-start. NE touche pas au compteur. */
static void tx067_block(uint8_t button, int n) {
    for (int i = 0; i < n; i++) {
        int64_t t = esp_timer_get_time();
        tx067_one(button);
        int el = (int)((esp_timer_get_time() - t) / 1000);
        if (el < DEVMEL_FRAME_MS) vTaskDelay(pdMS_TO_TICKS(DEVMEL_FRAME_MS - el));
    }
}

/* Un "appui" = bloc de s_burst trames au compteur courant, puis compteur++. */
static void tx067_press(uint8_t button, const char *label) {
    ESP_LOGI(TAG, "-- 0x067 %s : %d trames (cnt %u, RPT=1, 108ms) --", label, s_burst, (unsigned)s_id.counter);
    tx067_block(button, s_burst);
    s_id.counter++;
    pfx_state_save(&s_id);
}

/* ================= Rejeu des trames reelles ================= */

/* MAINTIEN : repete la trame pendant duration_ms (comme tenir le bouton). Pour la DESCENTE
 * controlee (20 cm / 3 s) : le moteur descend tant qu'on repete, on l'arrete au STOP. */
static bool replay_hold(cap_idx_t c, uint32_t duration_ms) {
    if (!s_cap[c].have) { ESP_LOGE(TAG, "trame %s absente", CAP_NAME[c]); return false; }
    ESP_LOGI(TAG, "-- MAINTIEN %s %u ms --", CAP_NAME[c], (unsigned)duration_ms);
    int64_t t0 = esp_timer_get_time(); int nf = 0;
    while ((esp_timer_get_time() - t0) < (int64_t)duration_ms * 1000) {
        cc1101_tx_raw_bits(s_cap[c].bits, s_cap[c].n); nf++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "   (%d trames %s sur %u ms)", nf, CAP_NAME[c], (unsigned)duration_ms);
    return true;
}

/* APPUI BREF : rafale x s_burst (pour MONTEE = le moteur part seul vers la butee, et STOP). */
static bool replay_press(cap_idx_t c) {
    if (!s_cap[c].have) { ESP_LOGE(TAG, "trame %s non capturee", CAP_NAME[c]); return false; }
    ESP_LOGI(TAG, "-- REJEU %s (rafale x%d) --", CAP_NAME[c], s_burst);
    for (int i = 0; i < s_burst; i++) { cc1101_tx_raw_bits(s_cap[c].bits, s_cap[c].n);
                                        vTaskDelay(pdMS_TO_TICKS(20)); }
    return true;
}

/* ================= Commandes ================= */

static int cmd_newid(int argc, char **argv) {
    (void)argc; (void)argv;
    /* Identite d'ENROLEMENT reelle de Lecanard38 : slot 10 = serial 0x000B067 (idx 10).
     * C'est la trame captee btn5/cnt3 -> hop 0x4F837454 qui a REELLEMENT enrole son FranciaFlex.
     * (le slot 6 = 0x0007067 etait l'identite des *commandes*, pas de l'enrolement.) */
    uint32_t serial = 0x000B067;
    uint64_t key; uint8_t got;
    if (pfx_key_for_serial(serial, &key, &got)) {
        s_id.serial = serial; s_id.crypt_key = key; s_id.counter = 2;
        s_id.discrimination = serial & 0xFFF; s_id_ok = true;
        pfx_state_save(&s_id);
        ESP_LOGW(TAG, "NEWID : identite Lecanard 0x067 = serial 0x%07X (pool idx %u), counter=2",
                 (unsigned)serial, got);
        return 0;
    }
    ESP_LOGE(TAG, "newid: clef pool introuvable pour 0x%07X", (unsigned)serial); return 1;
}

static int cmd_learn(int argc, char **argv) {
    if (argc < 2) { ESP_LOGE(TAG, "usage: learn up|stop|down"); return 1; }
    cap_idx_t c;
    if      (!strcasecmp(argv[1], "up"))   c = CAP_UP;
    else if (!strcasecmp(argv[1], "stop")) c = CAP_STOP;
    else if (!strcasecmp(argv[1], "down")) c = CAP_DOWN;
    else { ESP_LOGE(TAG, "learn: up|stop|down"); return 1; }
    ESP_LOGW(TAG, "LEARN %s : APPUIE sur %s de ta vraie telecommande (10 s)...",
             CAP_NAME[c], CAP_NAME[c]);
    char bits[80];
    int n = cc1101_rx_listen_bits(10000, bits, sizeof(bits));
    if (n <= 0) { ESP_LOGE(TAG, "learn %s: aucune trame captee", CAP_NAME[c]); return 1; }
    memcpy(s_cap[c].bits, bits, n); s_cap[c].bits[n] = '\0'; s_cap[c].n = n; s_cap[c].have = true;
    ESP_LOGW(TAG, "LEARN %s OK : %d bits captes -> %s", CAP_NAME[c], n, s_cap[c].bits);
    return 0;
}

static uint32_t kv_u32(const char *arg, const char *key, uint32_t def) {
    size_t kl = strlen(key);
    if (!strncmp(arg, key, kl) && arg[kl] == '=') return (uint32_t)strtoul(arg + kl + 1, NULL, 0);
    return def;
}

static int cmd_settime(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        s_mid_top_ms = kv_u32(argv[i], "midtop",  s_mid_top_ms);
        s_return_ms  = kv_u32(argv[i], "return",  s_return_ms);
        s_release_ms = kv_u32(argv[i], "release", s_release_ms);
        s_down20_ms  = kv_u32(argv[i], "down20",  s_down20_ms);
        s_down3_ms   = kv_u32(argv[i], "down3",   s_down3_ms);
    }
    ESP_LOGI(TAG, "settime: midtop=%u return=%u release=%u down20=%u down3=%u ms",
             (unsigned)s_mid_top_ms, (unsigned)s_return_ms, (unsigned)s_release_ms,
             (unsigned)s_down20_ms, (unsigned)s_down3_ms);
    return 0;
}

static int cmd_set(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        s_burst      = (int)kv_u32(argv[i], "burst",   s_burst);
        s_timeout_ms =      kv_u32(argv[i], "timeout", s_timeout_ms);
        s_gap_ms     =      kv_u32(argv[i], "gap",     s_gap_ms);
    }
    ESP_LOGI(TAG, "set: burst=%d timeout=%u gap=%u", s_burst, (unsigned)s_timeout_ms, (unsigned)s_gap_ms);
    return 0;
}

/* Injection. CORRECTION (Olivier, sur la SEULE trame d'enrolement captee en vrai = slot-10
 * 0x000B067 : btn 0x5, cnt 3). Le btn0/cnt2 "PAIRMODE" venait du GDB emulateur en HTTP500,
 * JAMAIS confirme sur l'air -> le cnt2 est consomme dans la prep interne DEVMEL, sans codeword
 * PFX utile. Le 1er codeword radio d'enrolement est donc btn5/cnt3, MAINTENU ~5 s. */
static void inject(void) {
    /* La fenetre d'apprentissage de SON moteur 2009 est ouverte par le STOP physique d'Olivier
     * (va-et-vient observe). Donc on injecte IMMEDIATEMENT (pas la temporisation "settings" 5s de
     * DEVMEL, qui n'a de sens que dans le flux appli). Seule trame radio = shutter btn5/cnt3. */
    ESP_LOGW(TAG, "== INJECTION 0x067 IMMEDIATE : shutter btn5/cnt3 rafale x%d (fenetre = TON STOP) ==", s_burst);
    s_id.counter = 3;
    tx067_block(DEV_BTN_SHUT50, s_burst);
    s_id.counter = 4; pfx_state_save(&s_id);   /* commandes suivantes (test) : cnt4 */
    ESP_LOGW(TAG, "   -> teste tout de suite : stop / up / down. Le volet repond ?");
}

static int cmd_emit(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!s_id_ok) { ESP_LOGE(TAG, "emit: fais 'newid' d'abord"); return 1; }
    inject(); return 0;
}

/* env [devmel|profalux] : choisit l'enveloppe OOK pour l'emission 0x067 (memes bits). */
static int cmd_env(int argc, char **argv) {
    if (argc >= 2) {
        if      (!strcasecmp(argv[1], "devmel"))   s_env_proven = false;
        else if (!strcasecmp(argv[1], "profalux")) s_env_proven = true;
        else { ESP_LOGE(TAG, "usage: env [devmel|profalux]"); return 1; }
    }
    ESP_LOGW(TAG, "ENVELOPPE 0x067 = %s", s_env_proven
             ? "PROFALUX prouvee (TE455, header long-L, 23 preamb)"
             : "DEVMEL mesuree (TE410, header 1H+10L, 22 preamb)");
    return 0;
}

static int cmd_cmd067(int argc, char **argv) {
    (void)argc;
    if (!s_id_ok) { ESP_LOGE(TAG, "fais 'newid' d'abord"); return 1; }
    uint8_t b; const char *l = argv[0];
    if      (!strcasecmp(l, "up"))   b = DEV_BTN_UP;
    else if (!strcasecmp(l, "stop")) b = DEV_BTN_STOP;
    else if (!strcasecmp(l, "down")) b = DEV_BTN_DOWN;
    else return 1;
    tx067_press(b, l); return 0;
}

/* Procedure GUIDEE : les mouvements R6 sont faits par la VRAIE telecommande (trames
 * fraiches authentifiees) ; le banc ne fait QUE l'injection 0x067. On avance d'une etape
 * a chaque appel (bouton G39 ou commande 'next'). */
static void banc_step(void) {
    switch (s_step) {
    case 0:
        cmd_newid(0, NULL);
        ESP_LOGW(TAG, "#### ETAPE 1/3 : avec ta VRAIE telecommande, fais PHASE A ####");
        ESP_LOGW(TAG, "   MONTEE -> butee haute -> DESCENTE ~20 cm -> STOP  (volet arrete)");
        ESP_LOGW(TAG, "   >>> puis APPUIE G39 (ou 'next') pour lancer l'INJECTION 0x067");
        s_step = 1;
        break;
    case 1:
        if (!s_id_ok) { ESP_LOGE(TAG, "fais l'etape 1 d'abord (G39)"); s_step = 0; return; }
        ESP_LOGW(TAG, "#### INJECTION 0x067 (volet arrete) : btn5/cnt3 maintenu ~5s ####");
        inject();
        ESP_LOGW(TAG, "#### ETAPE 2/3 : avec ta VRAIE telecommande, fais PHASE B ####");
        ESP_LOGW(TAG, "   MONTEE -> haut -> DESCENTE 3 s -> STOP -> MONTEE -> haut");
        ESP_LOGW(TAG, "   GUETTE LE VA-ET-VIENT de confirmation du moteur.");
        ESP_LOGW(TAG, "   >>> puis APPUIE G39 (ou 'next') pour TESTER (DOWN 0x067)");
        s_step = 2;
        break;
    case 2:
        ESP_LOGW(TAG, "#### ETAPE 3/3 TEST : commandes DEVMEL 0x067 (0x4 puis 0x8) ####");
        ESP_LOGW(TAG, "   >>> si le volet BOUGE = identite APPRISE, GAGNE ! (DEVMEL: 0x2=stop, 0x4/0x8=monte/descend)");
        tx067_press(0x4, "cmd DEVMEL 0x4");
        vTaskDelay(pdMS_TO_TICKS(3000));
        tx067_press(0x8, "cmd DEVMEL 0x8");
        ESP_LOGW(TAG, "#### FIN. Le volet a-t-il bouge (0x4 ou 0x8) ? Nouvel essai : G39 ####");
        s_step = 0;
        break;
    }
}

static int cmd_enroll(int argc, char **argv) {
    (void)argc; (void)argv;
    s_step = 0;          /* (re)demarre la procedure guidee a l'etape 1 */
    banc_step();
    return 0;
}

static int cmd_next(int argc, char **argv) { (void)argc; (void)argv; banc_step(); return 0; }

/* Diagnostic : rejoue UNE commande reelle (0x813) a la demande, pour isoler (ex STOP). */
static int cmd_rejeu(int argc, char **argv) {
    if (argc < 2) { ESP_LOGE(TAG, "usage: rejeu up|stop|down"); return 1; }
    cap_idx_t c;
    if      (!strcasecmp(argv[1], "up"))   c = CAP_UP;
    else if (!strcasecmp(argv[1], "stop")) c = CAP_STOP;
    else if (!strcasecmp(argv[1], "down")) c = CAP_DOWN;
    else { ESP_LOGE(TAG, "rejeu up|stop|down"); return 1; }
    replay_press(c);
    return 0;
}

static int cmd_status(int argc, char **argv) {
    (void)argc; (void)argv;
    if (s_id_ok) ESP_LOGI(TAG, "identite 0x067 : serial=0x%07X counter=%u",
                          (unsigned)s_id.serial, (unsigned)s_id.counter);
    else ESP_LOGI(TAG, "identite : AUCUNE (newid)");
    for (int c = 0; c < 3; c++)
        ESP_LOGI(TAG, "trame %-4s : %s", CAP_NAME[c], s_cap[c].have ? "capturee" : "-");
    ESP_LOGI(TAG, "reglages : burst=%d gap=%u | midtop=%u return=%u release=%u down20=%u down3=%u ms",
             s_burst, (unsigned)s_gap_ms, (unsigned)s_mid_top_ms, (unsigned)s_return_ms,
             (unsigned)s_release_ms, (unsigned)s_down20_ms, (unsigned)s_down3_ms);
    return 0;
}

/* ---- Self-test KeeLoq au boot (clef index 15 -> trame DEVMEL reelle) ---- */
void console_selftest(void) {
    uint64_t key; uint8_t idx;
    bool ok = pfx_key_for_serial(0x0010067, &key, &idx);
    uint32_t dec = ok ? keeloq_decrypt(0x01A98657, key) : 0;
    ESP_LOGW(TAG, "SELFTEST KeeLoq : cle idx %u ; decrypt(0x01A98657)=0x%08X attendu 0x20670CEB -> %s",
             idx, (unsigned)dec, (ok && dec == 0x20670CEB) ? "OK" : "ECHEC");
}

/* ================= Pilotage par BOUTON (G39) ================= */

/* 1 appui : capture guidee UP -> STOP -> DOWN de la vraie telecommande. */
void banc_capture_guided(void) {
    const cap_idx_t order[3] = { CAP_UP, CAP_STOP, CAP_DOWN };
    ESP_LOGW(TAG, "########## CAPTURE : suis les invites (10 s par bouton) ##########");
    for (int i = 0; i < 3; i++) {
        cap_idx_t c = order[i];
        ESP_LOGW(TAG, ">>> APPUIE **%s** sur ta vraie telecommande maintenant...", CAP_NAME[c]);
        char bits[80];
        int n = cc1101_rx_listen_bits(10000, bits, sizeof(bits));
        if (n <= 0) { ESP_LOGE(TAG, "  %s : rien capte (recommence : 1 appui)", CAP_NAME[c]); return; }
        memcpy(s_cap[c].bits, bits, n); s_cap[c].bits[n] = '\0'; s_cap[c].n = n; s_cap[c].have = true;
        ESP_LOGW(TAG, "  %s OK (%d bits)", CAP_NAME[c], n);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "########## CAPTURE OK. Positionne le volet AU MILIEU, puis 2 appuis = ENROLL ##########");
}

/* Bouton G39 : avance la procedure guidee d'une etape (newid+PhaseA / injection+PhaseB / test). */
void banc_enroll(void) { banc_step(); }

/* 3 appuis : test commande native 0x067 (UP -> STOP -> DOWN). */
void banc_test_cmd(void) {
    if (!s_id_ok) { ESP_LOGE(TAG, "pas d'identite (fais 2 appuis = enroll d'abord)"); return; }
    ESP_LOGW(TAG, "########## TEST COMMANDE 0x067 : UP, STOP, DOWN ##########");
    tx067_press(DEV_BTN_UP,   "UP");   vTaskDelay(pdMS_TO_TICKS(3000));
    tx067_press(DEV_BTN_STOP, "STOP"); vTaskDelay(pdMS_TO_TICKS(2000));
    tx067_press(DEV_BTN_DOWN, "DOWN");
}

/* Pre-charge les trames "Chambre Parent" dans s_cap (pas besoin de capturer). */
static void load_cp_frames(void) {
    struct { cap_idx_t c; const char *b; } m[3] = {
        { CAP_UP, CP_UP }, { CAP_STOP, CP_STOP }, { CAP_DOWN, CP_DOWN } };
    for (int i = 0; i < 3; i++) {
        int n = (int)strlen(m[i].b);
        memcpy(s_cap[m[i].c].bits, m[i].b, n); s_cap[m[i].c].bits[n] = '\0';
        s_cap[m[i].c].n = n; s_cap[m[i].c].have = true;
    }
    ESP_LOGW(TAG, "Trames Chambre Parent chargees (up/stop/down, 66 bits, serial 0x813).");
}

void console_start(void) {
    load_cp_frames();
    /* CORRECTION (log Lecanard) : les vraies trames DEVMEL 0x067 sont recues FREQEST 0 sur
     * 868.425 -> DEVMEL emet a ~868.425, PAS 868.350. On garde donc 868.425 (0x21/0x66/0xA5),
     * seul le profil temporel change (TE 400us, 108ms) dans tx_devmel_frame. */
    cc1101_set_freq(0x21, 0x66, 0xA5);
    ESP_LOGW(TAG, "TX 0x067 : 868.425 MHz, %d trames/cmd @ %dms, RPT=1. Enveloppe=%s (toggle 'env').",
             s_burst, DEVMEL_FRAME_MS, s_env_proven ? "PROFALUX-455 (prouvee)" : "DEVMEL-410");
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "pfx>";
    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uc, &rc, &repl));
    const esp_console_cmd_t cmds[] = {
        { .command = "newid",   .help = "tire une identite 0x067 fraiche",      .func = cmd_newid },
        { .command = "learn",   .help = "learn up|stop|down (capture RX)",       .func = cmd_learn },
        { .command = "settime", .help = "settime up= down= down20= (ms)",        .func = cmd_settime },
        { .command = "set",     .help = "set settings= shutter= burst= gap=",    .func = cmd_set },
        { .command = "emit",    .help = "injection seule settings+shutter",      .func = cmd_emit },
        { .command = "enroll",  .help = "(re)demarre la procedure guidee (etape 1)", .func = cmd_enroll },
        { .command = "next",    .help = "avance la procedure guidee d'une etape",  .func = cmd_next },
        { .command = "up",      .help = "commande 0x067 UP",                     .func = cmd_cmd067 },
        { .command = "stop",    .help = "commande 0x067 STOP",                   .func = cmd_cmd067 },
        { .command = "down",    .help = "commande 0x067 DOWN",                   .func = cmd_cmd067 },
        { .command = "env",     .help = "env [devmel|profalux] : enveloppe OOK 0x067", .func = cmd_env },
        { .command = "rejeu",   .help = "rejeu up|stop|down (isoler une commande reelle)", .func = cmd_rejeu },
        { .command = "status",  .help = "etat du banc",                          .func = cmd_status },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    esp_console_register_help_command();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGW(TAG, "Banc console pret. Tape 'help'. Flux: newid -> learn up/stop/down -> settime -> enroll.");
}
