/* io_pair : voir io_pair.h. Orchestration de l'appairage io, d'apres iown-home (classes
 * mode2w) et la capture 1W de samr037/iohc-flipper. Deux modes :
 *  - 1W (io_hc_pair_1w) : on POUSSE notre cle vers un moteur en mode PROG. CC1101 seul.
 *  - 2W (io_hc_pair) : decouverte + transfert de cle 2W. Reception + saut de frequence (a finir). */
#include "io_pair.h"
#include <cstring>
#include "iohome_2w.h"
#include "iohome_crypto.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include "io_radio.h"
#include "io_framer.h"
#include "io_store.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

using namespace iohome;
using namespace iohome::frame;

static const char *TAG = "io_pair";

/* Emet une trame io deja finalisee : serialise -> encapsule UART -> FIFO CC1101. */
static bool tx_frame(const IoFrame *f) {
    uint8_t io_bytes[FRAME_MAX_SIZE];
    size_t n = serialize_frame(f, io_bytes, sizeof io_bytes);
    if (n == 0 || n > sizeof io_bytes) return false;
    uint8_t fifo[64];
    size_t m = io_frame_to_fifo(io_bytes, (uint8_t)n, fifo, sizeof fifo);
    if (m == 0 || m > 60) return false;
    return io_radio_tx_packet(fifo, m) == 0;
}

/* Emet des octets io deja assembles a la main (trame 0x30 sans HMAC) : encapsule -> FIFO. */
static bool tx_raw(const uint8_t *io_bytes, uint8_t n) {
    uint8_t fifo[64];
    size_t m = io_frame_to_fifo(io_bytes, n, fifo, sizeof fifo);
    if (m == 0 || m > 60) return false;
    return io_radio_tx_packet(fifo, m) == 0;
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

/* Appairage 1W : la carte se presente comme une nouvelle telecommande et POUSSE sa cle vers un
 * moteur mis en mode PROG (bouton physique tenu sur une telecommande existante du moteur).
 * Calque sur l'enrolement REEL de Devmel, capte sur l'air sur le moteur d'un testeur (#87) :
 * Devmel n'envoie QUE la commande 0x30 (transfert de cle), repetee 3 fois, SANS HMAC, et SANS
 * rafale 0x20 ni confirmation 0x2e. On fait pareil. Disposition du 0x30 :
 *   ctrl0 ctrl1 | dest | src | 0x30 | cle enveloppee[16] | man_id | data | seq[2] | crc[2]
 * (total 31 octets, ctrl0 = 0xFC). C'est du 1W pur : aucune reception ni saut de frequence, un
 * seul CC1101 suffit. Cree une identite d'emetteur neuve (propre a CE volet), la memorise et la
 * rend dans out_src. Rend 0 si emis, -2 sur erreur. */
extern "C" int io_hc_pair_1w(const io_profile_t *profile, uint8_t out_src[3], uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!out_src || !profile) return -2;

    uint8_t src[NODE_ID_SIZE], key[AES_KEY_SIZE];
    esp_fill_random(src, sizeof src);
    esp_fill_random(key, sizeof key);
    io_store_set(src, key);                /* pose l'identite + initialise le compteur roulant */
    io_store_set_profile(src, profile);    /* memorise le profil appris pour l'emission */
    const uint8_t *tgt = profile->target;  /* on vise le meme broadcast que la vraie telecommande */

    io_radio_set_channel(IO_CH2);   /* canal 1W primaire (868,95 MHz) */

    /* Transfert de cle 0x30, assemble a la main (notre IoFrame ajoute toujours un HMAC, absent de
     * cette trame). Disposition : ctrl0 ctrl1 dst src 0x30 cle[16] man_id data seq[2] crc[2], soit
     * msg_len = 28 et total = 31 octets. Comme Devmel, on l'emet 3 fois avec la MEME sequence, en
     * basculant ctrl1 (0x20 / 0x00) d'une repetition a l'autre (#87). Pas de rafale 0x20 ni de
     * confirmation 0x2e : Devmel ne les emet pas, ils faisaient echouer notre appairage. */
    uint8_t enc[AES_KEY_SIZE];
    if (!crypto::encrypt_1w_key(key, src, enc)) return -2;
    const uint16_t seq = io_store_next_rolling(src);   /* une seule sequence pour les 3 repetitions */
    for (int rep = 0; rep < 3; rep++) {
        uint8_t f[FRAME_MAX_SIZE];
        const uint8_t msg_len = 28;   /* du ctrl byte 1 jusqu'au dernier octet avant le CRC */
        f[0] = (uint8_t)((msg_len & CTRL0_LENGTH_MASK) | CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK);  /* debut+fin+1W = 0xFC */
        f[1] = (rep % 2) ? 0x00 : 0x20;                   /* ctrl byte 1 : Devmel bascule 0x20/0x00 */
        memcpy(&f[2], tgt, NODE_ID_SIZE);                 /* broadcast du type appris */
        memcpy(&f[5], src, NODE_ID_SIZE);
        f[8] = CMD_KEY_TRANSFER_1W;                       /* 0x30 */
        memcpy(&f[9], enc, AES_KEY_SIZE);                 /* cle enveloppee (16 o) */
        f[25] = MANUFACTURER_SOMFY_PAIR;                  /* 0x01 (mesure sur l'enrolement Devmel reel, #87) */
        f[26] = PAIR_STATUS_BYTE;                         /* 0x01 */
        f[27] = (uint8_t)(seq >> 8);
        f[28] = (uint8_t)(seq & 0xFF);
        uint16_t crc = crypto::compute_crc16(f, 1 + msg_len);   /* CRC-16/KERMIT sur ctrl0..data */
        f[29] = (uint8_t)(crc & 0xFF);                    /* LSB d'abord */
        f[30] = (uint8_t)(crc >> 8);
        tx_raw(f, 31);
        esp_rom_delay_us(20000);
    }

    memcpy(out_src, src, NODE_ID_SIZE);
    ESP_LOGW(TAG, "appairage 1W : ceremonie emise, identite emetteur %02X%02X%02X memorisee",
             src[0], src[1], src[2]);
    return 0;
}

extern "C" int io_hc_pair(uint8_t out_dst[3], uint32_t timeout_ms) {
    if (!out_dst) return -2;

    /* Identite de controleur neuve pour cet appairage 2W. */
    uint8_t src[NODE_ID_SIZE], key[AES_KEY_SIZE];
    esp_fill_random(src, sizeof src);
    esp_fill_random(key, sizeof key);

    mode2w::DiscoveryManager disc;
    disc.begin(src);
    disc.start_discovery(0xFF, timeout_ms);

    /* CH1 = 868,25 (2W), CH2 = 868,95 (primaire) : on alterne pour la decouverte. */
    const io_channel_t chans[2] = { IO_CH1, IO_CH2 };
    int ci = 0;
    mode2w::DiscoveredDevice dev;
    bool found = false;
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline && !found) {
        io_radio_set_channel(chans[ci]); ci ^= 1;

        /* Requete de decouverte, finalisee avec la cle de transfert (pre-appairage). */
        IoFrame req;
        if (disc.create_discovery_request(&req, 0xFF) && finalize_frame(&req, TRANSFER_KEY, nullptr)) {
            tx_frame(&req);
        }
        /* Fenetre d'ecoute d'une reponse (un appareil en appairage repond). */
        uint8_t rx[FRAME_MAX_SIZE];
        int rn = io_radio_rx_packet(rx, sizeof rx, 250);
        if (rn > 0) {
            IoFrame resp;
            if (parse_frame(rx, (size_t)rn, &resp)) disc.process_discovery_response(&resp, 0);
        }
        if (disc.get_discovered_count() > 0 && disc.get_discovered_device(0, &dev)) found = true;
    }

    if (!found) { ESP_LOGW(TAG, "aucun appareil io en appairage dans le delai"); return -1; }
    ESP_LOGW(TAG, "appareil io decouvert : %02X%02X%02X", dev.node_id[0], dev.node_id[1], dev.node_id[2]);

    /* Transfert de cle 2W (defi + cle du reseau chiffree) sur le canal 2W. */
    io_radio_set_channel(IO_CH1);
    mode2w::AuthenticationManager auth;
    auth.begin(key);
    uint8_t challenge[HMAC_SIZE];
    auth.generate_challenge(challenge);

    IoFrame kt;
    if (!disc.create_key_transfer_2w(&kt, dev.node_id, src, key, challenge) ||
        !finalize_frame(&kt, TRANSFER_KEY, challenge)) {
        ESP_LOGE(TAG, "construction du transfert de cle 2W echouee");
        return -2;
    }
    for (int i = 0; i < 4; i++) { tx_frame(&kt); esp_rom_delay_us(20000); }   /* repetitions */

    /* Cote emission reussi : on memorise l'identite du reseau et on rend le node du moteur. Le
     * dialogue complet (acquittement du moteur) se validera au banc. */
    io_store_set(src, key);
    memcpy(out_dst, dev.node_id, NODE_ID_SIZE);
    ESP_LOGW(TAG, "transfert de cle 2W emis vers le moteur ; identite io memorisee");
    return 0;
}
