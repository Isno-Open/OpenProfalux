/* io_pair : voir io_pair.h. Orchestration de l'appairage io, d'apres iown-home (classes
 * mode2w) et la capture 1W de samr037/iohc-flipper. UN SEUL mode : 1W.
 * La carte POUSSE sa cle vers un moteur mis en mode PROG. CC1101 seul, ni reception ni saut de
 * frequence. Le 2W (decouverte + defi-reponse) a ete RETIRE le 2026-10-28 : il n'a jamais ete
 * valide sur du materiel, et le porter en parallele du 1W brouillait le diagnostic (#87). */
#include "io_pair.h"
#include <cstring>
#include "iohome_crypto.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include "io_radio.h"
#include "io_framer.h"
#include "io_store.h"
#include "io_trace.h"       /* journal RX+TX recuperable (diagnostic enrolement) */
#include "io_send.h"        /* io_hc_send_confirm_1w : 0x2e de fermeture */
#include "esp_random.h"
#include "esp_mac.h"        /* esp_read_mac : chip ID pour identite deterministe */
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

using namespace iohome;
using namespace iohome::frame;

static const char *TAG = "io_pair";

/* Emet LES MEMES octets IO_TX_REPEATS fois, espacees de IO_TX_REPEAT_GAP_MS, puis attend que
 * la periode IO_PAIR_FRAME_PERIOD_MS soit ecoulee depuis le DEBUT de cette trame. Rythme releve
 * sur l'enrolement la box de copie reel (voir iohome_constants.h).
 *
 * ⚠️ On reemet le MEME tableau d'octets : repasser par le constructeur incrementerait le code
 * roulant et fabriquerait des trames differentes, ce que la box de copie ne fait pas. */
static bool tx_repeated(const uint8_t *io_bytes, uint8_t n) {
    /* TRACE : log de CHAQUE trame qu'on EMET (pour voir qui fait quoi dans l'enrolement). */
    { char hx[2 * FRAME_MAX_SIZE + 1]; int m = n < FRAME_MAX_SIZE ? n : FRAME_MAX_SIZE;
      for (int i = 0; i < m; i++) snprintf(hx + 2 * i, 3, "%02X", io_bytes[i]);
      ESP_LOGW(TAG, "ENROL TX (%d o) cmd=0x%02X : %s", n, (n > 8 ? io_bytes[8] : 0), hx); }
    io_trace_add('T', 0, io_bytes, n);   /* meme journal que les RX : sequence complete recuperable */
    const int64_t debut = esp_timer_get_time();
    bool ok = false;
    for (uint8_t i = 0; i < IO_TX_REPEATS; i++) {
        if (io_radio_tx_frame(io_bytes, n) == 0) ok = true;
        if (i + 1 < IO_TX_REPEATS) vTaskDelay(pdMS_TO_TICKS(IO_TX_REPEAT_GAP_MS));
    }
    const int64_t ecoule_ms = (esp_timer_get_time() - debut) / 1000;
    if (ecoule_ms < IO_PAIR_FRAME_PERIOD_MS) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(IO_PAIR_FRAME_PERIOD_MS - ecoule_ms)));
    }
    return ok;
}

/* Emet une trame io deja finalisee : serialise -> emission repetee -> chemin unique (io_radio). */
static bool tx_frame(const IoFrame *f) {
    uint8_t io_bytes[FRAME_MAX_SIZE];
    size_t n = serialize_frame(f, io_bytes, sizeof io_bytes);
    if (n == 0 || n > sizeof io_bytes) return false;
    return tx_repeated(io_bytes, (uint8_t)n);
}

/* Emet des octets io deja assembles a la main (trame 0x30 sans HMAC). */
static bool tx_raw(const uint8_t *io_bytes, uint8_t n) {
    return tx_repeated(io_bytes, n);
}


/* Apprend le profil d'emission depuis une trame io captee de la VRAIE telecommande du volet. On ne
 * retient que les commandes 0x00 : leur destination (broadcast encodant le type) et leur charge utile
 * entiere sont ce qu'on doit rejouer. Disposition 1W :
 *   ctrl0 ctrl1 | dest[3] | src[3] | cmd | charge utile | code roulant[2] | HMAC[6] | CRC[2]
 * La charge utile reelle = tout entre le cmd (offset 9) et le code roulant, soit n - 9 - 2 - 6 - 2.
 * On la capture a sa VRAIE longueur (6 octets pour une p0x00_14, 8 pour une p0x00_16) au lieu de lire
 * des offsets fixes : sur une trame de 6 octets, frame[15]/frame[16] sont deja le code roulant.
 * Rend 0 si exploitable, -1 sinon. */
extern "C" int io_profile_from_capture(const uint8_t *frame, int n, io_profile_t *out) {
    if (!frame || !out) return -1;
    const int overhead = 9 + ROLLING_CODE_SIZE + HMAC_SIZE + CRC_SIZE;   /* 9 + 2 + 6 + 2 = 19 */
    int data_len = n - overhead;
    if (data_len < 3 || data_len > IO_PROFILE_PAYLOAD_MAX) return -1;     /* besoin d'au moins origin/fab/bouton */
    if (frame[8] != CMD_EXECUTE) return -1;                              /* on n'apprend que sur une commande 0x00 */
    memcpy(out->target, &frame[2], NODE_ID_SIZE);                        /* destination = broadcast type-dependant */
    memcpy(out->payload, &frame[9], data_len);                           /* charge utile reelle, telle quelle */
    out->payload_len = (uint8_t)data_len;
    return 0;
}

/* Tire une adresse de source io de TYPE TELECOMMANDE (node type 0xD), la seule qu'un moteur
 * accepte comme emetteur a l'appairage. D'apres Message_GetBroadcastType (doc io, linklayer.md) :
 * des que le 1er octet est non nul, le noeud est de type 0xD. Une adresse a 1er octet 0x00 serait
 * d'un autre type (noeud final, groupe, broadcast) et le moteur la refuserait en silence. On
 * rejette donc le 1er octet nul, et les sources reservees (FFFFFE = P2P/broadcast, FFFFFF).
 *
 * â  Mesure du 2026-10-28 : les 11 identites du SDK la box de copie ET la vraie telecommande Somfy de
 * le testeur (XXXXXX) sont TOUTES de type 0xD. esp_fill_random seul tombait hors type 1 fois sur 256
 * (1er octet nul), un echec intermittent silencieux. */
static void io_random_controller_addr(uint8_t out[NODE_ID_SIZE]) {
    do {
        esp_fill_random(out, NODE_ID_SIZE);
    } while (out[0] == 0x00 ||
             (out[0] == 0xFF && out[1] == 0xFF && (out[2] == 0xFE || out[2] == 0xFF)));
}

extern "C" int io_hc_pair_1w(const io_profile_t *profile, const uint8_t *fixed_src,
                             const uint8_t *fixed_key, int man_id,
                             uint8_t out_src[3], uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!out_src || !profile) return -2;

    uint8_t src[NODE_ID_SIZE], key[AES_KEY_SIZE];
    if (fixed_src) memcpy(src, fixed_src, NODE_ID_SIZE);   /* identite choisie (connue d'un moteur) */
    else           io_random_controller_addr(src);         /* sinon, identite de type telecommande au hasard */
    /* Cle : soit une cle CONNUE fournie a l'execution (identite deja appairee a ce moteur, p.ex.
     * une identite la box de copie que le moteur fait deja confiance), soit une cle neuve a nous. Aucune cle
     * n'est jamais ecrite dans les sources : le secret entre par l'appelant (voir web_ui h_io_pair_1w). */
    if (fixed_key) memcpy(key, fixed_key, AES_KEY_SIZE);
    else           esp_fill_random(key, sizeof key);
    io_store_set(src, key);                /* pose l'identite + initialise le compteur roulant */
    io_store_set_profile(src, profile);    /* memorise le profil appris pour l'emission */
    const uint8_t *tgt = profile->target;  /* on vise le meme broadcast que la vraie telecommande */

    io_radio_set_channel(IO_CH2);   /* canal 1W primaire (868,95 MHz) */

    /* Enrolement « nouvelle telecommande » : UN SEUL transfert de cle 0x30, rien d'autre.
     *
     * ⚠️ Modele confirme le 2026-10-28 (doc io « Copy Remote Controller (1W) », #87, journal 4) :
     * l'ajout d'une telecommande io se fait PAR PARRAINAGE. L'utilisateur met sa telecommande
     * Smoove DEJA APPAIREE en mode appairage (appui ~4 s sur son PROG) ; elle autorise le moteur a
     * accepter une nouvelle telecommande. Nous sommes cette « nouvelle telecommande » : on pousse
     * notre 0x30 (notre identite de type 0xD, notre cle), le moteur l'accepte GRACE au parrainage.
     *
     * On ne reproduit PAS la ceremonie longue de la Smoove maitre (0x39 + rafale 0x20 + 0x2e) :
     *   - le 0x39 et le 0x2e appartiennent a la telecommande MAITRE qui parraine, pas a nous ;
     *   - la rafale 0x20 (main 0x05XX) est du REGLAGE DE FINS DE COURSE (doc : enter_settings_mode,
     *     save_lower/upper_end_limit, save_settings) : l'emettre sur un moteur deja regle le
     *     DEREGLERAIT. Dangereux et hors sujet pour un simple ajout.
     * la box de copie, en « nouvelle telecommande », emet exactement cela : un 0x30 puis ses commandes
     * (journal 4, source XXXXXX). C'est le retour a la sequence courte de test6, mais avec la bonne
     * MANIP (parrainage par la Smoove, pas PROG du moteur) et le bon rythme. */
    const uint8_t man = (man_id >= 0) ? (uint8_t)man_id    /* man_id capte sur l'enrolement la box de copie reel */
                                      : MANUFACTURER_SOMFY_PAIR; /* defaut 0x02 (Somfy) a l'identite creee */
    uint8_t enc[AES_KEY_SIZE];
    if (!crypto::encrypt_1w_key(key, src, enc)) return -2;
    {
        uint8_t f[FRAME_MAX_SIZE];
        const uint8_t msg_len = 28;   /* de ctrlb1 au dernier octet avant le CRC ; total 31 o */
        f[0] = (uint8_t)((msg_len & CTRL0_LENGTH_MASK) | CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK);  /* debut+fin+1W */
        f[1] = CTRL1_LOW_POWER;                           /* 0x20 : bit basse conso, mis par la box de copie sur le 0x30 d'une NOUVELLE identite (capture #87) ; le maitre, lui, met 0x00 */
        memcpy(&f[2], tgt, NODE_ID_SIZE);                 /* broadcast 00003F */
        memcpy(&f[5], src, NODE_ID_SIZE);
        f[8] = CMD_KEY_TRANSFER_1W;                       /* 0x30 */
        memcpy(&f[9], enc, AES_KEY_SIZE);                 /* cle enveloppee (16 o) */
        f[25] = man;                                      /* man_id : capte (la box de copie) ou defaut Somfy */
        f[26] = PAIR_STATUS_BYTE;                         /* 0x01 */
        uint16_t seq = io_store_next_rolling(src);
        f[27] = (uint8_t)(seq >> 8);
        f[28] = (uint8_t)(seq & 0xFF);
        uint16_t crc = crypto::compute_crc16(f, 1 + msg_len);   /* CRC-16/KERMIT sur ctrlb1..seq */
        f[29] = (uint8_t)(crc & 0xFF);                    /* LSB d'abord */
        f[30] = (uint8_t)(crc >> 8);
        tx_raw(f, 31);
    }

    memcpy(out_src, src, NODE_ID_SIZE);
    ESP_LOGW(TAG, "enrolement 1W : 0x30 emis, identite %02X%02X%02X (%s), cle %s, man_id 0x%02X",
             src[0], src[1], src[2], fixed_src ? "fournie" : "creee",
             fixed_key ? "fournie" : "creee", man);
    return 0;
}

/* Decode une trame d'enrolement 0x30 captee : rend l'identite (f[5:8]), la cle systeme en clair
 * (champ f[9:25] dechiffre par encrypt_1w_key, son propre inverse) et le compteur (f[27:28]).
 * Sert au profil 2 (fermeture en tant qu'origine) : la carte decode la cle de l'origine d'un 0x30
 * capte, rien en dur. Rend true si la trame est un 0x30 exploitable. */
extern "C" bool io_decode_0x30(const uint8_t *frame, int n, uint8_t out_src[3],
                               uint8_t out_key[16], int *out_seq) {
    if (!frame || n < 31 || frame[8] != CMD_KEY_TRANSFER_1W || !out_src || !out_key) return false;
    memcpy(out_src, &frame[5], NODE_ID_SIZE);
    if (!crypto::encrypt_1w_key(&frame[9], &frame[5], out_key)) return false;  /* -> cle en clair */
    if (out_seq) *out_seq = ((int)frame[27] << 8) | frame[28];
    return true;
}

/* splitmix64 : generateur deterministe de qualite pour deriver identite+cle d'une graine. */
static uint64_t splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Identite DETERMINISTE (survit au formatage/reflash de la MEME carte) : derivee du chip ID (MAC
 * STA, dans le materiel) + index. src de type telecommande (0xD : 1er octet non nul). Comme la cle
 * est regeneree a chaque enrolement cote moteur, cette identite a nous n'a pas besoin d'etre une
 * identite la box de copie ; elle nous appartient et reste recalculable pour pouvoir la retirer plus tard. */
extern "C" void io_derive_identity(uint32_t index, uint8_t out_src[3], uint8_t out_key[16]) {
    if (!out_src || !out_key) return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint64_t seed = ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) | ((uint64_t)mac[2] << 24) |
                    ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | (uint64_t)mac[5];
    seed ^= ((uint64_t)index + 1) * 0xD1B54A32D192ED03ULL;
    uint64_t st = seed;
    uint64_t a = splitmix64(&st);
    out_src[0] = (uint8_t)(a & 0xFF); if (out_src[0] == 0x00) out_src[0] = 0x0D;  /* type telecommande */
    out_src[1] = (uint8_t)((a >> 8) & 0xFF);
    out_src[2] = (uint8_t)((a >> 16) & 0xFF);
    for (int i = 0; i < 16; i += 8) {
        uint64_t k = splitmix64(&st);
        for (int j = 0; j < 8; j++) out_key[i + j] = (uint8_t)(k >> (8 * j));
    }
}

/* Emet une trame 1W SIGNEE (cmd + charge utile + code roulant + HMAC) au nom de src, signee key,
 * vers dst. Le code roulant vient du compteur par-source de io_store (croissant, anti-rejeu) ; pour
 * le parrainage on l'amorce d'abord avec io_store_set_rolling(origin). Passe par tx_frame donc herite
 * du rythme capte (IO_TX_REPEATS x a IO_TX_REPEAT_GAP_MS, periode IO_PAIR_FRAME_PERIOD_MS). */
static bool send_signed(const uint8_t src[3], const uint8_t key[16], const uint8_t dst[3],
                        uint8_t cmd, const uint8_t *payload, size_t n) {
    IoFrame f;
    init_frame(&f);
    set_source(&f, src);
    set_destination(&f, dst);
    if (!set_command(&f, cmd, payload, n)) return false;
    set_rolling_code(&f, io_store_next_rolling(src));
    if (!finalize_frame(&f, key)) return false;   /* ajoute HMAC + CRC */
    return tx_frame(&f);
}

/* Enrolement cale sur les VRAIES trames captees chez le testeur (journal 9, 2026-10-29).
 * mode (voir io_pair.h) :
 *   1 = SIMPLE : notre 0x30 seul (identite neuve ; sert de temoin, sans PROG).
 *   2 = PARRAINAGE : on emet la ceremonie AU NOM DE L'ORIGINE (0x39 -> rafale 0x20 -> 0x2e, signes
 *       et compteur avec la cle de l'origine captee d'un 0x30), et AU MILIEU notre PROPRE 0x30
 *       (notre identite neuve, transfert de cle non signe), comme une box tierce glisse la sienne.
 *       On ne signe JAMAIS en notre nom (un 0x39 signe par nous = "retire-MOI", bug test7-8).
 *       Compteur de l'origine amorce (origin_seq = 0x30 capte + marge) et avance a chaque trame (le
 *       moteur rejette un compteur <= dernier vu). Rien en dur : cle/identite/compteur de l'origine
 *       decodes d'un 0x30 capte, notre identite a nous. */
extern "C" int io_hc_enroll(const io_profile_t *profile,
                            const uint8_t our_src[3], const uint8_t our_key[16], int man_id,
                            int mode, const uint8_t *origin_src, const uint8_t *origin_key,
                            int origin_seq, int gap_ms, int confirm_repeats, uint8_t out_src[3]) {
    if (!profile || !our_src || !our_key || !out_src) return -2;
    (void)gap_ms; (void)confirm_repeats; (void)man_id;   /* rythme = constantes io ; la copie impose man 0x02 */
    { char lbl[32]; snprintf(lbl, sizeof lbl, "Creer - nos TX (mode %d)", mode); io_trace_mark(lbl); }

    if (mode == 1) {   /* SIMPLE : notre 0x30 seul (temoin) */
        int r1 = io_hc_pair_1w(profile, our_src, our_key, -1, out_src, 0);
        io_trace_persist();   /* flush du journal (notre TX) en NVS */
        return r1;
    }
    if (!origin_src || !origin_key) return -3;   /* mode 2 exige la cle de l'origine (captee, 0x30) */

    const uint8_t *bcast = SOMFY_BROADCAST_ADDR;
    /* Le compteur de l'origine DOIT etre le vrai (capte du 0x30, + marge). Sans lui, on n'emet PAS au
     * nom de l'origine : repartir d'un compteur bidon (1/0) serait rejete comme rejeu et casserait tout. */
    if (origin_seq < 0) return -4;
    io_store_set_rolling(origin_src, (uint16_t)origin_seq);   /* amorce = compteur capte + marge (anti-rejeu) */

    /* 0) 0x39 remove, SIGNE AVEC LA CLE DE L'ORIGINE : le moteur reconnait un parrain de confiance. */
    { uint8_t d = 0x00; send_signed(origin_src, origin_key, bcast, CMD_REMOVE_1W, &d, 1); }

    /* 1) 0x30 : NOTRE identite (notre cle enveloppee), comme la box de copie ajoute SA propre identite.
     *    f[1]=0x20 (nouvelle identite), man 0x02. Non signe (garde par le PROG physique). */
    int r = io_hc_pair_1w(profile, our_src, our_key, -1, out_src, 0);
    if (r != 0) return r;

    /* 2a) Rafale 0x20 SIGNEE ORIGINE : (0C,00) puis (05,02..07). */
    { uint8_t pw[5] = {0x02, 0x03, 0x0C, 0x00, 0x00};
      send_signed(origin_src, origin_key, bcast, CMD_PRIVATE_WRITE, pw, sizeof pw);
      for (uint8_t s = 0x02; s <= 0x07; s++) { pw[2] = 0x05; pw[3] = s;
          send_signed(origin_src, origin_key, bcast, CMD_PRIVATE_WRITE, pw, sizeof pw); } }

    /* 3) 0x2e confirmation SIGNEE ORIGINE (au milieu, apres 07). */
    { uint8_t d = 0x00; send_signed(origin_src, origin_key, bcast, CMD_PAIR_CONFIRM, &d, 1); }

    /* 2b) Fin de rafale 0x20 SIGNEE ORIGINE : (05,08),(05,09),(05,FF). */
    { const uint8_t steps[3] = {0x08, 0x09, 0xFF}; uint8_t pw[5] = {0x02, 0x03, 0x05, 0x00, 0x00};
      for (int i = 0; i < 3; i++) { pw[3] = steps[i];
          send_signed(origin_src, origin_key, bcast, CMD_PRIVATE_WRITE, pw, sizeof pw); } }

    ESP_LOGW(TAG, "enrolement (mode 2) : rituel signe ORIGINE %02X%02X%02X + NOTRE 0x30, seq base %d",
             origin_src[0], origin_src[1], origin_src[2], origin_seq);
    io_trace_persist();   /* flush du journal (toutes nos TX) en NVS */
    return 0;
}
