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
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

using namespace iohome;
using namespace iohome::frame;

static const char *TAG = "io_pair";

/* Emet LES MEMES octets IO_TX_REPEATS fois, espacees de IO_TX_REPEAT_GAP_MS, puis attend que
 * la periode IO_PAIR_FRAME_PERIOD_MS soit ecoulee depuis le DEBUT de cette trame. Rythme releve
 * sur l'enrolement Devmel reel (voir iohome_constants.h).
 *
 * ⚠️ On reemet le MEME tableau d'octets : repasser par le constructeur incrementerait le code
 * roulant et fabriquerait des trames differentes, ce que Devmel ne fait pas. */
static bool tx_repeated(const uint8_t *io_bytes, uint8_t n) {
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
 * â  Mesure du 2026-10-28 : les 11 identites du SDK Devmel ET la vraie telecommande Somfy de
 * Pascal (6D1846) sont TOUTES de type 0xD. esp_fill_random seul tombait hors type 1 fois sur 256
 * (1er octet nul), un echec intermittent silencieux. */
static void io_random_controller_addr(uint8_t out[NODE_ID_SIZE]) {
    do {
        esp_fill_random(out, NODE_ID_SIZE);
    } while (out[0] == 0x00 ||
             (out[0] == 0xFF && out[1] == 0xFF && (out[2] == 0xFE || out[2] == 0xFF)));
}

extern "C" int io_hc_pair_1w(const io_profile_t *profile, const uint8_t *fixed_src,
                             uint8_t out_src[3], uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!out_src || !profile) return -2;

    uint8_t src[NODE_ID_SIZE], key[AES_KEY_SIZE];
    if (fixed_src) memcpy(src, fixed_src, NODE_ID_SIZE);   /* identite choisie dans l'UI */
    else           io_random_controller_addr(src);         /* sinon, identite de type telecommande au hasard */
    esp_fill_random(key, sizeof key);                      /* la cle AES est toujours la notre */
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
     * Devmel, en « nouvelle telecommande », emet exactement cela : un 0x30 puis ses commandes
     * (journal 4, source 055A72). C'est le retour a la sequence courte de test6, mais avec la bonne
     * MANIP (parrainage par la Smoove, pas PROG du moteur) et le bon rythme. */
    uint8_t enc[AES_KEY_SIZE];
    if (!crypto::encrypt_1w_key(key, src, enc)) return -2;
    {
        uint8_t f[FRAME_MAX_SIZE];
        const uint8_t msg_len = 28;   /* de ctrlb1 au dernier octet avant le CRC ; total 31 o */
        f[0] = (uint8_t)((msg_len & CTRL0_LENGTH_MASK) | CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK);  /* debut+fin+1W */
        f[1] = 0x00;                                      /* ctrl byte 1 : PAS de bit reveil (comme une vraie telecommande Somfy) */
        memcpy(&f[2], tgt, NODE_ID_SIZE);                 /* broadcast 00003F */
        memcpy(&f[5], src, NODE_ID_SIZE);
        f[8] = CMD_KEY_TRANSFER_1W;                       /* 0x30 */
        memcpy(&f[9], enc, AES_KEY_SIZE);                 /* cle enveloppee (16 o) */
        f[25] = MANUFACTURER_SOMFY_PAIR;                  /* 0x02 (Somfy), comme la vraie Smoove de Pascal */
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
    ESP_LOGW(TAG, "enrolement 1W (nouvelle telecommande) : 0x30 emis, identite %02X%02X%02X (%s)",
             src[0], src[1], src[2], fixed_src ? "fournie" : "creee");
    return 0;
}
