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

/* Construit une trame 1W signee (cmd + charge utile + code roulant + HMAC) et l'emet. Le code
 * roulant vient du compteur par-source de io_store (croissant, anti-rejeu). */
static bool send_signed_1w(const uint8_t src[3], const uint8_t key[16],
                           uint8_t cmd, const uint8_t *payload, size_t n) {
    IoFrame f;
    init_frame(&f, true);
    set_source(&f, src);
    set_destination(&f, SOMFY_BROADCAST_ADDR);
    if (!set_command(&f, cmd, payload, n)) return false;
    set_rolling_code(&f, io_store_next_rolling(src));
    if (!finalize_frame(&f, key, nullptr)) return false;
    return tx_frame(&f);
}

/* Appairage 1W : la carte se presente comme une nouvelle telecommande et POUSSE sa cle vers un
 * moteur mis en mode PROG. Ceremonie calquee sur une capture Situo reelle
 * (danielpetrovic/somfy-io-bridge), la seule prouvee sur un vrai moteur Somfy :
 *  1) 0x30 : cle enveloppee, SANS HMAC (l'appairage est garde par le bouton du moteur),
 *  2) rafale 0x20 "private-write" signee : (main 0x0C00), puis 0x05xx (xx = 02..08) et un 0xFF,
 *  3) 0x2e : confirmation de pairage, signee.
 * Un 0x30 seul fait tressaillir le moteur mais ne le bonde pas ; la rafale + le 0x2e closent
 * l'enrolement, comme la rafale rituelle de l'enrolement Profalux. Aucune reception ni saut de
 * frequence : un seul CC1101 suffit. Cree une identite d'emetteur neuve (propre a CE volet), la
 * memorise et la rend dans out_src. Rend 0 si emis, -2 sur erreur.
 * EXPERIMENTAL : une partie de la rafale 0x20 est reproduite d'une seule capture, a valider au banc. */
extern "C" int io_hc_pair_1w(uint8_t out_src[3], uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!out_src) return -2;

    uint8_t src[NODE_ID_SIZE], key[AES_KEY_SIZE];
    esp_fill_random(src, sizeof src);
    esp_fill_random(key, sizeof key);
    io_store_set(src, key);   /* pose l'identite + initialise le compteur roulant par-source */

    io_radio_set_channel(IO_CH2);   /* canal 1W primaire (868,95 MHz) */

    /* 1) Transfert de cle 0x30, assemble a la main (notre IoFrame ajoute toujours un HMAC,
     *    absent de cette trame). Disposition : ctrlb1 ctrlb2 dst src 0x30 cle[16] man_id data
     *    seq[2] crc[2], soit msg_len = 28 et total = 31 octets. */
    uint8_t enc[AES_KEY_SIZE];
    if (!crypto::encrypt_1w_key(key, src, enc)) return -2;
    {
        uint8_t f[FRAME_MAX_SIZE];
        const uint8_t msg_len = 28;   /* du ctrl byte 1 jusqu'au dernier octet avant le CRC */
        f[0] = (uint8_t)((msg_len & CTRL0_LENGTH_MASK) | 0x80 | 0x40 | CTRL0_PROTOCOL_MASK);  /* end+start+1W */
        f[1] = 0x00;                                      /* ctrl byte 1 */
        memcpy(&f[2], SOMFY_BROADCAST_ADDR, NODE_ID_SIZE);
        memcpy(&f[5], src, NODE_ID_SIZE);
        f[8] = CMD_KEY_TRANSFER_1W;                       /* 0x30 */
        memcpy(&f[9], enc, AES_KEY_SIZE);                 /* cle enveloppee (16 o) */
        f[25] = MANUFACTURER_SOMFY_PAIR;                  /* 0x02 */
        f[26] = PAIR_STATUS_BYTE;                         /* 0x01 */
        uint16_t seq = io_store_next_rolling(src);
        f[27] = (uint8_t)(seq >> 8);
        f[28] = (uint8_t)(seq & 0xFF);
        uint16_t crc = crypto::compute_crc16(f, 1 + msg_len);   /* CRC-16/KERMIT sur ctrlb1..data */
        f[29] = (uint8_t)(crc & 0xFF);                    /* LSB d'abord */
        f[30] = (uint8_t)(crc >> 8);
        tx_raw(f, 31);
        esp_rom_delay_us(20000);
    }

    /* 2) Rafale "private-write" 0x20 (signee), charge utile = origin, acei, main[2], fp1.
     *    Valeurs reproduites de la capture Situo : (main0,main1) = (0x0C,0x00), puis (0x05, xx)
     *    pour xx dans 02..08, puis (0x05, 0xFF). */
    {
        const uint8_t steps[] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF};
        uint8_t pw[5] = {0x02 /*origin*/, 0x03 /*acei*/, 0x0C /*main0*/, 0x00 /*main1*/, 0x00 /*fp1*/};
        send_signed_1w(src, key, CMD_PRIVATE_WRITE, pw, sizeof pw);
        esp_rom_delay_us(14000);
        for (size_t i = 0; i < sizeof steps; i++) {
            pw[2] = 0x05; pw[3] = steps[i];
            send_signed_1w(src, key, CMD_PRIVATE_WRITE, pw, sizeof pw);
            esp_rom_delay_us(14000);
        }
    }

    /* 3) Confirmation de pairage 0x2e (signee), charge utile = un octet de donnee a 0x00. */
    {
        uint8_t data = 0x00;
        send_signed_1w(src, key, CMD_PAIR_CONFIRM, &data, 1);
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
