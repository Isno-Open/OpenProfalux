/*
 * radio.c : arbitrage RX/TX de l'unique CC1101 (voir radio.h).
 */
#include "radio.h"
#include "cc1101.h"
#include "freq_sweep.h"
#include "hardware_config.h"
#include "rx_confirm.h"
#include "io/io_radio.h"
#include "io/io_send.h"
#include "io/io_store.h"
#include "io/io_pair.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "radio";

#define RX_WINDOW_MS 250   /* fenetre d'ecoute : borne la latence max d'un TX a ~250 ms */

static SemaphoreHandle_t s_mtx = NULL;   /* proprietaire exclusif du radio (GDO0 + RMT) */
static radio_frame_cb_t  s_cb  = NULL;
static volatile bool     s_run     = false;
static volatile bool     s_paused  = false;  /* pause temporaire (pendant nos TX stream) */
static volatile bool     s_listen  = false;  /* interrupteur maitre (case UI "capture toutes les trames") */
static volatile bool     s_io_listen = false; /* ecoute permanente io (2-FSK) : exclusive de l'ecoute PFX (#87) */

/* Lecture LSB-first d'un champ de la trame (ordre du fil). */
static uint32_t lsb(const char *b, int from, int len) {
    uint32_t v = 0;
    for (int i = 0; i < len; i++) if (b[from + i] == '1') v |= (1u << i);
    return v;
}

void radio_init(void) {
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    /* Coupe le spam "RX(GDO0): MARCSTATE=..." emis a chaque fenetre d'ecoute. */
    esp_log_level_set("cc1101", ESP_LOG_WARN);
    /* Coupe le spam "hw buffer too small" : le bruit OOK sature normalement le buffer
     * RMT de l'ecoute permanente ; c'est attendu et sans consequence (on se re-arme). */
    esp_log_level_set("rmt", ESP_LOG_NONE);
}

void radio_tx(const char *bits, int repeats) {
    if (!bits || !bits[0] || !s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cc1101_tx_raw_bits(bits, (int)strlen(bits), repeats);   /* rafale en UNE session TX */
    xSemaphoreGive(s_mtx);
}

void radio_tx_device(const char *bits, int repeats, const tx_params_t *p) {
    if (!bits || !bits[0] || !s_mtx) return;
    if (!p) { radio_tx(bits, repeats); return; }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    /* Sauve l'etat courant, applique les params de l'appareil, emet, restaure : une autre marque
     * sur la meme radio ne doit jamais heriter de la porteuse ou des options de la precedente. */
    uint32_t f0 = cc1101_get_freq_khz(), te0 = cc1101_get_tx_te();
    bool rpt0 = cc1101_get_tx_repeat_bit(), rmt0 = cc1101_get_tx_rmt();
    if (p->freq_khz) cc1101_set_freq_khz(p->freq_khz);
    ESP_LOGW(TAG, "TX device : %u kHz (appareil=%u, globale=%u), te=%u",   /* trace diag : sur quelle freq on emet reellement */
             (unsigned)(p->freq_khz ? p->freq_khz : f0), (unsigned)p->freq_khz, (unsigned)f0, (unsigned)(p->te_us ? p->te_us : te0));
    if (p->te_us)    cc1101_set_tx_te(p->te_us);
    if (p->rpt >= 0) cc1101_set_tx_repeat_bit(p->rpt != 0);
    if (p->rmt >= 0) cc1101_set_tx_rmt(p->rmt != 0);
    cc1101_tx_raw_bits(bits, (int)strlen(bits), repeats);
    if (p->freq_khz) cc1101_set_freq_khz(f0);
    if (p->te_us)    cc1101_set_tx_te(te0);
    if (p->rpt >= 0) cc1101_set_tx_repeat_bit(rpt0);
    if (p->rmt >= 0) cc1101_set_tx_rmt(rmt0);
    xSemaphoreGive(s_mtx);
}

/* Coeur de l'ecoute dediee : SUPPOSE le mutex pris et l'ecoute permanente en pause, et travaille
 * sur la radio ACTIVE. Les deux points d'entree ci-dessous gerent le mutex (et, pour la variante
 * par radio, la selection de la puce). Extrait pour que l'ecoute par radio (issue #95) ne duplique
 * pas la boucle. */
static int listen_confirmed_locked(uint32_t timeout_ms, char *buf, int max, bool full) {
    /* Fenetres successives : on prefere deux trames identiques (rx_confirm.h, issue #39), mais a
     * defaut on accepte une trame complete (repli ci-dessous, issue #1), car ici l'utilisateur a
     * lance la capture expres et tient le bouton. */
    static rx_confirm_t c;   /* sous s_mtx : une seule ecoute dediee a la fois */
    rx_confirm_reset(&c);
    char bits[80];
    /* Derniere trame complete et valide vue, a defaut de paire confirmee. */
    char repli[RXC_FULL_BITS + 1]; repli[0] = 0;
    const int need = full ? RXC_FULL_BITS : RXC_CMP_BITS;
    int64_t repli_us = 0;   /* instant de la 1re trame complete vue (delai de grace, issue #1) */
    int r = -1;
    int64_t t_end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (int64_t now; (now = esp_timer_get_time()) < t_end; ) {
        /* Trame complete deja captee et courte grace ecoulee : on la prend sans attendre la fin de
         * la fenetre. On laisse d'abord une chance a une paire, mais on ne fait pas patienter. */
        if (repli[0] && now - repli_us > 1500000) break;
        uint32_t w = (uint32_t)((t_end - now) / 1000) + 1;
        if (w > 300) w = 300;   /* fenetres courtes : la grace ci-dessus reagit vite */
        int n = cc1101_rx_listen_bits(w, bits, 79);
        if (n < 64) continue;
        bits[n] = 0;
        if (n >= need && n <= RXC_FULL_BITS) { strlcpy(repli, bits, sizeof repli); if (!repli_us) repli_us = now; }
        if (!rx_confirm_push(&c, bits, n, esp_timer_get_time())) continue;
        const char *ok = full ? c.full : c.last;   /* apprentissage : une trame complete de 66 bits */
        if (!ok[0]) continue;                      /* confirmee mais coupee : on attend la suivante */
        strlcpy(buf, ok, (size_t)max);
        r = (int)strlen(buf);
        ESP_LOGW(TAG, "ecoute dediee : trame confirmee (%s)",
                 c.weak ? "reception faible, repetition lente" : "repetition normale");
        break;
    }
    /* Pas de paire confirmee dans la fenetre, mais une trame complete est passee : on l'accepte.
     * La double trame reste de rigueur pour l'ecoute permanente (telecommandes fantomes, #39) ;
     * ici la capture est explicite, une trame bien formee suffit (issue #1). */
    if (r < 0 && repli[0]) { strlcpy(buf, repli, (size_t)max); r = (int)strlen(buf); }
    return r;
}

int radio_listen_confirmed(uint32_t timeout_ms, char *buf, int max, bool full) {
    if (!s_mtx || max < RXC_FULL_BITS + 1) return -1;
    bool was = s_paused; s_paused = true;   /* met la tache permanente en pause pendant l'ecoute dediee */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = listen_confirmed_locked(timeout_ms, buf, max, full);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* Comme radio_listen_confirmed, mais ECOUTE SUR LA RADIO idx (issue #95). On selectionne la puce
 * sous le mutex (le front-end de la 433 est adapte a 433, la ou la primaire 868 n'entend rien d'un
 * emetteur 433), on ecoute, puis on REND LA MAIN a la radio primaire. Sert au diagnostic de
 * reception par radio : verifier qu'une puce RECOIT, pas seulement qu'elle voit une porteuse. */
int radio_listen_confirmed_on_freq(int idx, uint32_t freq_khz, uint32_t timeout_ms, char *buf, int max, bool full) {
    if (idx < 0 || idx >= cc1101_radio_count()) return -1;
    if (!s_mtx || max < RXC_FULL_BITS + 1) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int prev = cc1101_active_radio();
    cc1101_select(idx);
    uint32_t back = cc1101_get_freq_khz();
    if (freq_khz) cc1101_set_freq_khz(freq_khz);   /* capturer a la frequence de l'APPAREIL (Eveno 868,330 vs Profalux 868,425), pas celle de la box */
    int r = listen_confirmed_locked(timeout_ms, buf, max, full);
    if (freq_khz) cc1101_set_freq_khz(back);       /* restaurer la porteuse de idx : ne pas decaler l'ecoute permanente */
    cc1101_select(prev);         /* rend la main a la radio primaire (868) */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}
int radio_listen_confirmed_on(int idx, uint32_t timeout_ms, char *buf, int max, bool full) {
    return radio_listen_confirmed_on_freq(idx, 0, timeout_ms, buf, max, full);
}

/* Capture BRUTE OOK sur la radio idx (issue #95) : selectionne la puce sous mutex (p.ex. la 433
 * pour un portail CAME), capture les impulsions brutes via cc1101_rx_capture_pulses, puis rend la
 * main a la primaire. Meme patron que radio_listen_confirmed_on. Rend le nb d'impulsions (>0),
 * <0 si rien/erreur. Appele par le moteur de capture (rf_capture.c). */
int radio_capture_raw_on_freq(int idx, uint32_t freq_khz, uint32_t timeout_ms, uint8_t *levels, uint16_t *us, int max, int8_t *rssi_out) {
    if (idx < 0 || idx >= cc1101_radio_count()) return -1;
    if (!s_mtx) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int prev = cc1101_active_radio();
    cc1101_select(idx);
    uint32_t back = cc1101_get_freq_khz();
    if (freq_khz) cc1101_set_freq_khz(freq_khz);   /* capturer a la frequence du PROFIL de marque, pas celle de la box */
    int r = cc1101_rx_capture_pulses(timeout_ms, levels, us, max, rssi_out);
    if (freq_khz) cc1101_set_freq_khz(back);       /* restaurer la porteuse de idx */
    cc1101_select(prev);         /* rend la main a la radio primaire (868) */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}
int radio_capture_raw_on(int idx, uint32_t timeout_ms, uint8_t *levels, uint16_t *us, int max, int8_t *rssi_out) {
    return radio_capture_raw_on_freq(idx, 0, timeout_ms, levels, us, max, rssi_out);
}

/* Train d'impulsions OOK sur une autre porteuse (voir radio.h). Ecoute en pause + mutex, le
 * temps d'emettre et de RETABLIR l'etat. Si la carte declare une radio dediee a la bande visee
 * (p.ex. la radio 433 de la Super pour le Somfy RTS ou un portail), on bascule sur elle (son
 * front-end est adapte a 433) ; sinon on retune la radio active, comme avant. Dans tous les cas
 * on rend la main a la radio primaire (868) a la fin. */
int radio_tx_ook_pulses(uint32_t freq_khz, const uint8_t *levels, const uint32_t *us, int n) {
    if (!s_mtx || !levels || !us || n <= 0) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int prev = cc1101_active_radio();
    int idx = cc1101_radio_for_khz(freq_khz);   /* radio dont la bande declaree contient freq_khz */
    if (idx < 0) idx = prev;                     /* aucune radio dediee : retune l'active (ancien comportement) */
    cc1101_select(idx);
    ESP_LOGI(TAG, "emission OOK sur radio %d a %u kHz (%d impulsions)", idx, (unsigned)freq_khz, n);
    uint32_t back = cc1101_get_freq_khz();
    int r = cc1101_set_freq_khz(freq_khz);
    if (r == 0) r = cc1101_tx_ook_pulses(levels, us, n);
    cc1101_set_freq_khz(back);   /* retablit la porteuse de cette radio, quoi qu'il arrive */
    cc1101_select(prev);         /* rend la main a la radio primaire (868) */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

void radio_pause_rx(bool pause) { s_paused = pause; }

/* ── io-homecontrol (2-FSK 868, #87). Toutes ces primitives passent la radio en FSK io (config +
 * canal CH2) sous mutex, ecoute permanente en pause, puis RESTAURENT la modulation OOK Profalux et
 * la porteuse, meme sur echec. Le module io/ est autonome sur cc1101 (voir io/io_radio.c). ── */
void radio_set_io_listening(bool on) {
    s_io_listen = on;
    ESP_LOGW(TAG, "ecoute io permanente %s (2-FSK Somfy io)", on ? "ON" : "OFF");
}
bool radio_is_io_listening(void) { return s_io_listen; }

int radio_io_sniff(uint8_t *buf, int max, uint32_t timeout_ms, io_sniff_kind_t kind, uint32_t freq_khz) {
    if (!s_mtx || !buf || max <= 0) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();                         /* FSK io + canal CH2 */
    /* geste 2 (ecoute du PROG) : on ecoute a la porteuse APPRISE au geste 1 (freq_khz), via le meme
     * parametre par appareil que Profalux. geste 1 (freq_khz=0) ecoute au canal par defaut et MESURE. */
    if (freq_khz) io_radio_set_carrier_khz(freq_khz);
    /* On ecoute jusqu'a une VRAIE trame du type attendu a CRC valide, pas la premiere venue : les
     * moteurs et telecommandes io crachent des balises 0x20 en continu, qui noieraient l'appui bouton
     * (geste 1) ou le 0x30 d'enrolement (geste 2). La validite est jugee par la MEME fonction que
     * l'enrolement : io_profile_from_capture (cmd==0x00) pour une commande, io_enroll_from_capture
     * (cmd==0x30) pour l'enrolement. Source unique du controle, aucune valeur en dur. */
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int n = 0;
    io_profile_t probe;
    uint8_t enroll_frame[32];
    while (esp_timer_get_time() < end) {
        int64_t left_us = end - esp_timer_get_time();
        if (left_us <= 0) break;
        int k = io_radio_rx_packet(buf, (size_t)max, (uint32_t)(left_us / 1000));
        if (k <= 0) continue;                   /* balise 0x20, CRC KO ou trame tronquee : on continue */
        int ok = (kind == IO_SNIFF_ENROLL)
                     ? io_enroll_from_capture(buf, k, &probe)                 /* 0x30 valide */
                     : io_profile_from_capture(buf, k, &probe, IO_ACTION_UP); /* 0x00 valide ; probe jete, l'action ne sert qu'a remplir un slot ici inutilise */
        if (ok == 0) {
            n = k;
            if (kind != IO_SNIFF_ENROLL) break;
            /* Le 0x30 arrive au debut du PROG. Le conserver, puis ne rendre la main a l'UI qu'apres
             * le 0x2e de fin (ou 6 s best-effort), afin que le clic Appairer distinct ne puisse pas
             * partir au milieu de la ceremonie. Les trames suivantes vont reutiliser buf. */
            memcpy(enroll_frame, buf, (size_t)n);
            bool ended = false;
            int64_t ceremony_end = esp_timer_get_time() + 6000 * 1000;
            while (esp_timer_get_time() < ceremony_end) {
                int q = io_radio_rx_packet(buf, (size_t)max, 200);
                if (q > 0 && io_cmd_is_pair_confirm(io_frame_cmd(buf, q))) { ended = true; break; }
            }
            memcpy(buf, enroll_frame, (size_t)n);
            ESP_LOGW(TAG, "io PROG capture seule : 0x30 conserve, fin 0x2e %s ; pret pour clic Appairer",
                     ended ? "vue" : "non vue (delai 6 s)");
            break;
        }
    }
    cc1101_apply_config_ook();                 /* restaure la modulation OOK Profalux */
    cc1101_set_freq_khz(back);                 /* restaure la porteuse */
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return n;
}

int radio_io_send(const uint8_t src[3], io_action_t action, uint32_t freq_khz) {
    if (!s_mtx || !src) return -1;
    uint8_t key[16];
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t back = cc1101_get_freq_khz();
    io_radio_config();
    if (freq_khz) io_radio_set_carrier_khz(freq_khz);   /* porteuse par appareil ; 0 = CH2 868950 par defaut */
    int r = io_store_get_key(src, key) ? io_hc_send_shutter_1w(src, key, action) : -1;
    cc1101_apply_config_ook();
    cc1101_set_freq_khz(back);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

int radio_io_erase(void) {
    if (!s_mtx) return -1;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = io_store_erase_all();
    xSemaphoreGive(s_mtx);
    return r;
}

/* DIAG enrolement (1W vs 2W) : juste APRES notre 0x30, on ECOUTE une reponse du moteur et on logge TOUTE
 * trame recue (hex + RSSI + canal), sans filtre : une reponse 2W serait un type qu'on ne reconnait pas.
 * D'abord sur NOTRE porteuse (latence TX->RX minimale, meilleure chance d'une reponse sur canal), puis
 * balayage des 3 canaux io au cas ou le moteur saute de frequence. NE CHANGE RIEN a l'enrolement : purement
 * passif, apres emission. Lecture : une reponse captee PROUVE le 2W ; un silence ne prouve PAS le 1W
 * (bascule RX, mauvais canal a l'instant, ou plancher RSSI ~-95 dBm). A appeler sous le mutex, radio en
 * FSK io. */
static void io_log_rx_frame(const uint8_t *buf, int k, int8_t rssi, uint32_t khz, const char *where) {
    char hex[2 * 32 + 1];
    int m = k < 32 ? k : 32;
    for (int i = 0; i < m; i++) snprintf(&hex[i * 2], 3, "%02X", buf[i]);
    uint8_t cmd = io_frame_cmd(buf, k);
    const char *lbl = io_cmd_label(cmd);
    ESP_LOGW(TAG, "io DIAG reponse : trame (%d o, %u kHz, RSSI %d dBm, %s) cmd=0x%02X (%s) : %s",
             k, (unsigned)khz, rssi, where, cmd, lbl ? lbl : "inconnu", hex);
    /* Banniere dediee quand on voit la confirmation 0x2e : c'est la FIN de la ceremonie du remote. Vu
     * APRES notre emission ici, elle situe notre 0x30 dans la ceremonie (io12g emet avant cette fin). */
    if (io_cmd_is_pair_confirm(cmd))
        ESP_LOGW(TAG, "===== io ETAPE : 0x2e vu = FIN de ceremonie du remote (RSSI %d dBm) =====", rssi);
}

static void io_probe_motor_response(uint32_t emit_khz) {
    uint8_t buf[32];
    const io_channel_t chans[3] = { IO_CH1, IO_CH2, IO_CH3 };
    uint32_t car_khz = emit_khz ? emit_khz : io_radio_channel_khz(IO_CH2);   /* porteuse d'emission (ou CH2 par defaut) */
    int seen = 0;
    /* 1) notre porteuse courante (celle de l'emission), 3 s : couvre une reponse moteur sur canal ET le
     * 0x2e de fin de ceremonie du remote, observe ~1,9 s apres notre 0x30 (meme canal que sa cle). */
    int64_t end = esp_timer_get_time() + 3000 * 1000;
    while (esp_timer_get_time() < end) {
        int k = io_radio_rx_packet(buf, sizeof buf, 200);
        if (k <= 0) continue;
        io_log_rx_frame(buf, k, cc1101_get_rssi(), car_khz, "porteuse emission");
        seen++;
    }
    /* 2) balayage des 3 canaux io (saut de frequence), 500 ms chacun : attrape une reponse moteur 2W
     * qui repondrait sur un autre canal. Probabiliste (un seul CC1101, un canal a la fois). */
    for (int c = 0; c < 3; c++) {
        io_radio_set_channel(chans[c]);
        char where[16];
        snprintf(where, sizeof where, "canal %d", c + 1);
        int64_t cend = esp_timer_get_time() + 500 * 1000;
        while (esp_timer_get_time() < cend) {
            int k = io_radio_rx_packet(buf, sizeof buf, 200);
            if (k <= 0) continue;
            io_log_rx_frame(buf, k, cc1101_get_rssi(), io_radio_channel_khz(chans[c]), where);
            seen++;
        }
    }
    ESP_LOGW(TAG, "io DIAG reponse : %d trame(s) captee(s) apres notre 0x30 (0 = pas de reponse captable ; voir caveat)", seen);
}

/* Correctif timing (B) : attend la FIN de ceremonie du remote (0x2e) AVANT notre emission, puis ~1 s,
 * comme vodhash qui emet APRES la ceremonie (ce qui a enrole le Velux). io12g emettait PENDANT, ~1,9 s
 * avant le 0x2e, en collision. Avantage UX : le boitier gere le timing, Pascal n'a aucun clic a
 * synchroniser (c'est le clic separe qui avait tue io12f). Ecoute sur la porteuse d'emission ; journalise
 * chaque trame (io_log_rx_frame pose deja la banniere 0x2e). Rend true si le 0x2e a ete vu ; sinon on
 * emet quand meme (best-effort loggue) pour ne pas rater la fenetre PROG. */
static bool io_wait_ceremony_end(uint32_t car_khz, uint32_t timeout_ms) {
    uint8_t buf[32];
    bool seen = false;
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (!seen && esp_timer_get_time() < end) {
        int k = io_radio_rx_packet(buf, sizeof buf, 200);
        if (k <= 0) continue;
        io_log_rx_frame(buf, k, cc1101_get_rssi(), car_khz, "attente fin ceremonie");
        if (io_cmd_is_pair_confirm(io_frame_cmd(buf, k))) seen = true;
    }
    if (seen) {
        ESP_LOGW(TAG, "io DIAG : fin de ceremonie detectee, emission de notre 0x30 dans ~5 s");
        vTaskDelay(pdMS_TO_TICKS(5000));
    } else {
        ESP_LOGW(TAG, "io DIAG : pas de 0x2e vu en %u ms, emission best-effort (voir caveat)", (unsigned)timeout_ms);
    }
    return seen;
}

int radio_io_enroll(const io_profile_t *profile, const uint8_t src[3], const uint8_t key[16],
                    bool tx, int rolling, uint32_t freq_khz, bool wait_ceremony_end) {
    if (!s_mtx || !profile || !src || !key || (!tx && rolling < 0)) return -1;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = io_store_set(src, key, profile, rolling) ? 0 : -1;
    if (r == 0 && tx) {
        bool was = s_paused; s_paused = true;
        uint32_t back = cc1101_get_freq_khz();
        io_radio_config();
        if (freq_khz) io_radio_set_carrier_khz(freq_khz);   /* notre 0x30 part a la porteuse apprise de l'appareil (geste 1) */
        uint32_t emit_khz = freq_khz ? freq_khz : io_radio_channel_khz(IO_CH2);
        /* Flux fusionne historique : attendre le 0x2e. Avec les trois actions separees, la ceremonie
         * a deja ete capturee et l'utilisateur vient de cliquer Appairer : emission immediate. */
        if (wait_ceremony_end) io_wait_ceremony_end(emit_khz, 6000);
        uint8_t ourframe[31];
        int ourlen = 0;
        /* Ceremonie Situo io (0x39, 0x30, deux 0x20 signes), recette de stekoverflo/esphome-somfy-io. */
        r = io_hc_pair_1w_situo(profile, src, key, ourframe, &ourlen);
        if (r == 0) {
            /* Banniere d'emission : NOTRE 0x30 est parti (horodatage ESP), sa frequence, sa dest
             * d'enrolement CAPTEE et la trame exacte. Avec les bannieres de capture et le 0x2e ci-dessous,
             * le journal donne la sequence complete (timing + frequence + trame) d'un coup d'oeil. */
            char ohex[2 * sizeof(ourframe) + 1];
            int m = ourlen < (int)sizeof(ourframe) ? ourlen : (int)sizeof(ourframe);
            for (int i = 0; i < m; i++) snprintf(&ohex[i * 2], 3, "%02X", ourframe[i]);
            /* man_id EMIS (octet 25 de notre trame) : celui capte de la telecommande d'origine. */
            ESP_LOGW(TAG, "===== io ETAPE : ceremonie Situo emise (0x39, 0x30, 0x20 x2) ; NOTRE 0x30 vers %02X%02X%02X (man_id 0x%02X, %u kHz) : %s =====",
                     profile->enroll_target[0], profile->enroll_target[1], profile->enroll_target[2],
                     (ourlen > 25 ? ourframe[25] : 0), (unsigned)emit_khz, ohex);
            io_probe_motor_response(emit_khz);   /* DIAG : le moteur repond-il ? (1W vs 2W), + capte le 0x2e de fin, passif */
        }
        cc1101_apply_config_ook();
        cc1101_set_freq_khz(back);
        s_paused = was;
    }
    xSemaphoreGive(s_mtx);
    return r;
}

/* Relance le self-test TX a la demande (bouton UI), sous mutex/pause pour ne pas
 * entrer en collision avec l'ecoute. Retourne 0 si la puce passe en TX, -1 sinon. */
int radio_tx_selftest(void) {
    if (!s_mtx) return -1;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = cc1101_tx_selftest();
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* Change la frequence porteuse a chaud (profil radio), sous mutex et ecoute en pause. */
int radio_set_freq_khz(uint32_t khz) {
    if (!s_mtx) return cc1101_set_freq_khz(khz);   /* avant radio_init : boot, pas de concurrence */
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int r = cc1101_set_freq_khz(khz);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    return r;
}

/* Applique un lot de reglages a la radio idx, puis rend la main a la radio d'avant (issue #95).
 * Pointeur NULL = champ inchange. Appele depuis la tache HTTP (POST /api/config radio=idx)
 * pendant que la tache RX tourne : mutex + ecoute en pause, comme radio_set_freq_khz. Avant
 * radio_init (boot), pas de concurrence : on applique directement. Rend 0, ou -1 si idx hors borne. */
int radio_apply(int idx, const uint32_t *freq_khz, const uint8_t *rx_gain,
                const uint32_t *tx_te, const int *tx_rpt, const int *tx_rmt) {
    if (idx < 0 || idx >= cc1101_radio_count()) return -1;
    bool locked = (s_mtx != NULL);
    bool was = s_paused;
    if (locked) { s_paused = true; xSemaphoreTake(s_mtx, portMAX_DELAY); }
    int prev = cc1101_active_radio();
    cc1101_select(idx);
    int r = 0;
    /* set_freq_khz relit le registre et rend -1 si la radio n'a pas pris la frequence. On
     * propage cet echec (au lieu de toujours rendre 0) pour que l'UI puisse le signaler ;
     * on restaure quand meme la radio d'avant et on relache le mutex. */
    if (freq_khz && cc1101_set_freq_khz(*freq_khz) != 0) r = -1;
    if (rx_gain)  cc1101_set_rx_gain(*rx_gain);
    if (tx_te)    cc1101_set_tx_te(*tx_te);
    if (tx_rpt)   cc1101_set_tx_repeat_bit(*tx_rpt != 0);
    if (tx_rmt)   cc1101_set_tx_rmt(*tx_rmt != 0);
    cc1101_select(prev);
    if (locked) { xSemaphoreGive(s_mtx); s_paused = was; }
    return r;
}

/* ── Auto-calibration du gain RX ──────────────────────────────────────────
 * Balaie une echelle de gain (du + sensible au + mordant) ; a chaque cran, ecoute
 * quelques secondes ; DES QU'UNE TRAME VALIDE (>=64 bits) est captee, verrouille ce
 * gain (= le + sensible qui donne une trame propre chez CE module). L'utilisateur
 * appuie sur sa telecommande pendant l'operation, sans lire aucun log. */
static volatile int     s_cal_state   = 0;   /* 0=idle 1=en cours 2=ok 3=echec */
static volatile uint8_t s_cal_testing = 0;   /* gain (AGCCTRL2) en cours de test */
static volatile uint8_t s_cal_result  = 0;   /* gain retenu si ok */
static volatile int     s_cal_radio   = 0;   /* radio ciblee par la calibration (issue #95) */

static void cal_task(void *arg) {
    (void)arg;
    static const uint8_t ladder[] = { 0x27, 0x2F, 0x37, 0x3F };
    char bits[80];
    bool was = s_paused; s_paused = true;              /* suspend l'ecoute permanente */
    /* Calibre la radio CIBLEE. La puce cible n'est selectionnee que DANS chaque fenetre tenue
     * sous mutex, et on restaure la radio d'avant avant de relacher. Sans ca, la secondaire
     * (433) restait active, mutex relache entre les fenetres, pendant les ~24 s du balayage :
     * un autre appelant (radio_tx d'un volet Profalux, radio_apply) aurait alors emis sur la
     * mauvaise puce. s_paused ne suspend que l'ecoute de fond, pas ces appelants. set_rx_gain
     * ecrit le slot de la radio active, il reste donc lui aussi sous mutex sur la cible. */
    int found = -1;
    for (int i = 0; i < (int)sizeof(ladder) && found < 0 && s_run; i++) {
        s_cal_testing = ladder[i];
        int64_t t_end = esp_timer_get_time() + 6 * 1000000;   /* 6 s par cran */
        while (esp_timer_get_time() < t_end && s_run) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            int cal_prev = cc1101_active_radio(); cc1101_select(s_cal_radio);
            cc1101_set_rx_gain(ladder[i]);
            int n = cc1101_rx_listen_bits(400, bits, 79);
            cc1101_select(cal_prev);
            xSemaphoreGive(s_mtx);
            if (n >= 64) { found = ladder[i]; break; }         /* trame propre = ce gain va */
        }
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int cal_prev = cc1101_active_radio(); cc1101_select(s_cal_radio);
    if (found > 0) { cc1101_set_rx_gain((uint8_t)found); s_cal_result = (uint8_t)found; s_cal_state = 2; }
    else           { cc1101_set_rx_gain(0x27);            s_cal_result = 0;             s_cal_state = 3; }
    cc1101_select(cal_prev);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    ESP_LOGW(TAG, "calibration gain RX (radio %d) : %s (0x%02X)", s_cal_radio, found > 0 ? "OK" : "ECHEC", found > 0 ? found : 0x27);
    vTaskDelete(NULL);
}

int radio_calibrate_start_on(int idx) {
    if (!s_mtx) return -1;
    if (s_cal_state == 1) return 1;                    /* deja en cours */
    s_cal_radio = (idx >= 0 && idx < cc1101_radio_count()) ? idx : cc1101_active_radio();
    s_cal_state = 1; s_cal_testing = 0; s_cal_result = 0;
    if (xTaskCreate(cal_task, "radio_cal", 4096, NULL, 5, NULL) != pdPASS) { s_cal_state = 0; return -1; }
    return 0;
}
int radio_calibrate_start(void) { return radio_calibrate_start_on(cc1101_active_radio()); }

void radio_calibrate_status(int *state, uint8_t *testing, uint8_t *result) {
    if (state)   *state   = s_cal_state;
    if (testing) *testing = s_cal_testing;
    if (result)  *result  = s_cal_result;
}

/* ── Balayage RSSI : porteuse de la telecommande (issue #51) ──────────────────────
 * Pendant une emission longue de la telecommande (commutateur P seul), balaie la frequence de
 * reception autour de la frequence actuelle et trouve sa porteuse (freq_sweep.h). Tache de
 * fond, ecoute permanente en pause. N'applique rien : l'UI propose la frequence trouvee. */
#define SWEEP_SPAN_KHZ  150
#define SWEEP_STEP_KHZ  10
#define SWEEP_DWELL_MS  150   /* > une trame (~135 ms a TE 448) : le RSSI max voit un HAUT */
#define SWEEP_MAX       (2 * SWEEP_SPAN_KHZ / SWEEP_STEP_KHZ + 1)
static volatile int s_sw_state = 0;   /* 0 jamais lance, 1 en cours, 2 fini */
static uint32_t s_sw_khz[SWEEP_MAX];
static int16_t  s_sw_hdb[SWEEP_MAX];
static int      s_sw_n;
static uint32_t s_sw_from;            /* frequence au lancement */
static int      s_sw_status;          /* fsweep_status_t, ou -10 si la puce n'est pas passee en RX */
static fsweep_result_t s_sw_res;
static volatile int s_sw_radio = 0;   /* radio ciblee par le balayage (issue #95) */

static void sweep_task(void *arg) {
    (void)arg;
    bool was = s_paused; s_paused = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    /* Balaye la radio CIBLEE, dans SA bande declaree (pas celle de la primaire), puis rend la
     * main (issue #95). */
    int sw_prev = cc1101_active_radio(); cc1101_select(s_sw_radio);
    uint32_t lo = CC1101_BAND_LOW_KHZ, hi = CC1101_BAND_HIGH_KHZ;
    cc1101_radio_info(s_sw_radio, NULL, NULL, NULL, &lo, &hi, NULL, NULL);
    s_sw_from = cc1101_get_freq_khz();
    s_sw_n = 0;
    for (int o = -SWEEP_SPAN_KHZ; o <= SWEEP_SPAN_KHZ && s_sw_n < SWEEP_MAX; o += SWEEP_STEP_KHZ) {
        uint32_t f = s_sw_from + o;
        if (f >= lo && f <= hi) s_sw_khz[s_sw_n++] = f;   /* bande declaree de cette radio */
    }
    int rc = cc1101_rssi_scan(s_sw_khz, s_sw_n, SWEEP_DWELL_MS, s_sw_hdb);
    cc1101_select(sw_prev);
    xSemaphoreGive(s_mtx);
    s_paused = was;
    s_sw_status = rc ? -10 : fsweep_analyze(s_sw_khz, s_sw_hdb, s_sw_n, &s_sw_res);
    if (s_sw_status == FSWEEP_OK)
        ESP_LOGW(TAG, "balayage RSSI : porteuse %u kHz (profil %u kHz, ecart %+d kHz), pic %d dBm, bruit %d dBm",
                 (unsigned)s_sw_res.center_khz, (unsigned)s_sw_from, (int)s_sw_res.center_khz - (int)s_sw_from,
                 s_sw_res.peak_hdb / 2, s_sw_res.floor_hdb / 2);
    else
        ESP_LOGW(TAG, "balayage RSSI : echec (%d), pic %d dBm, bruit %d dBm", s_sw_status,
                 s_sw_res.peak_hdb / 2, s_sw_res.floor_hdb / 2);
    s_sw_state = 2;
    vTaskDelete(NULL);
}

int radio_sweep_start_on(int idx) {
    if (!s_mtx) return -1;
    if (s_sw_state == 1 || s_cal_state == 1) return 1;   /* deja en cours (ou calibration de gain) */
    s_sw_radio = (idx >= 0 && idx < cc1101_radio_count()) ? idx : cc1101_active_radio();
    s_sw_state = 1; s_sw_status = 0; memset(&s_sw_res, 0, sizeof(s_sw_res));
    if (xTaskCreate(sweep_task, "radio_sweep", 4096, NULL, 5, NULL) != pdPASS) { s_sw_state = 0; return -1; }
    return 0;
}
int radio_sweep_start(void) { return radio_sweep_start_on(cc1101_active_radio()); }

int radio_sweep_status(int *status, uint32_t *from_khz, const uint32_t **khz, const int16_t **hdb, int *n,
                       uint32_t *center_khz, int16_t *peak_hdb, int16_t *floor_hdb) {
    *status = s_sw_status; *from_khz = s_sw_from; *khz = s_sw_khz; *hdb = s_sw_hdb;
    *n = s_sw_state == 2 ? s_sw_n : 0;
    *center_khz = s_sw_res.center_khz; *peak_hdb = s_sw_res.peak_hdb; *floor_hdb = s_sw_res.floor_hdb;
    return s_sw_state;
}

void radio_set_listening(bool on) {
    s_listen = on;
    ESP_LOGI(TAG, "ecoute permanente %s (case UI 'capture toutes les trames')", on ? "ON" : "OFF");
}

static void rx_task(void *arg) {
    (void)arg;
    char bits[80];
    static rx_confirm_t c;   /* serie en cours : une trame n'est transmise que confirmee (issue #39) */
    rx_confirm_reset(&c);
    while (s_run) {
        /* Une fenetre io complete sous mutex, restauration OOK avant de le rendre. */
        if (s_io_listen && !s_paused) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            uint32_t ook_freq = cc1101_get_freq_khz();
            io_radio_config();
            uint8_t iob[32];
            int ni = io_radio_rx_packet(iob, sizeof iob, 300);
            int8_t rssi = ni > 0 ? cc1101_get_rssi() : 0;   /* lu AVANT restauration OOK (sinon stale) */
            uint32_t khz = io_radio_last_carrier_khz();     /* porteuse mesuree de la trame (base + FREQEST) */
            cc1101_apply_config_ook();
            cc1101_set_freq_khz(ook_freq);
            xSemaphoreGive(s_mtx);
            if (ni > 0) {
                char hex[2 * 32 + 1];
                int m = ni > 32 ? 32 : ni;
                for (int i = 0; i < m; i++) snprintf(hex + 2 * i, 3, "%02X", iob[i]);
                /* Capture (ecoute seule) : on nomme la commande pour lire une ceremonie d'enrolement
                 * d'un coup d'oeil (0x30 cle, 0x20 private-write, 0x2e confirmation...), avec frequence
                 * et RSSI. Sert a enregistrer une sequence qui MARCHE pour comparer. Aucune emission. */
                uint8_t cmd = io_frame_cmd(iob, ni);
                const char *lbl = io_cmd_label(cmd);
                ESP_LOGW(TAG, "io capture : trame (%d o, %u kHz, RSSI %d dBm) cmd=0x%02X (%s) : %s",
                         ni, (unsigned)khz, rssi, cmd, lbl ? lbl : "inconnu", hex);
                /* Meme repere ETAPE que le chemin d'enrolement, avec le MEME libelle (io_cmd_label),
                 * pour que le journal d'un appairage capte soit DIRECTEMENT comparable ligne a ligne a
                 * celui de notre enrolement (0x00, 0x30, serie 0x20, 0x2e cote a cote). */
                if (lbl) ESP_LOGW(TAG, "===== io ETAPE : %s captee (%u kHz) =====", lbl, (unsigned)khz);
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            continue;
        }
        if (!s_listen || s_paused) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }  /* veille tant que capture OFF ou TX en cours */
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        int n = (!s_listen || s_paused) ? -1 : cc1101_rx_listen_bits(RX_WINDOW_MS, bits, 79);
        int8_t rssi = (n >= 64) ? cc1101_get_rssi() : 0;
        xSemaphoreGive(s_mtx);
        if (n >= 64) bits[n] = 0;
        /* Confirmee = identique a la precedente (64 premiers bits) : la 1re trame d'un appui
         * attend la 2e (~125 ms). Les suivantes de la rafale passent toutes (le suivi de
         * position et le journal RF dedupliquent). */
        if (n >= 64 && rx_confirm_push(&c, bits, n, esp_timer_get_time()) && s_cb) {
            if (c.weak) ESP_LOGW(TAG, "trame confirmee en reception faible (repetition lente)");
            uint32_t serial = lsb(bits, 32, 28);
            uint32_t hop    = lsb(bits, 0, 32);
            uint8_t  button = (uint8_t)lsb(bits, 60, 4);
            s_cb(bits, serial, button, hop, rssi);
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));   /* cede la main entre 2 fenetres */
        }
    }
    vTaskDelete(NULL);
}

void radio_start(radio_frame_cb_t cb) {
    if (!s_mtx) radio_init();
    s_cb = cb; s_run = true;
    xTaskCreate(rx_task, "radio_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "RX permanent demarre (arbitrage RX/TX par mutex, fenetre %d ms)", RX_WINDOW_MS);
}
