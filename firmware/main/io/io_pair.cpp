/* io_pair : voir io_pair.h. Orchestration d'appairage 2W, d'apres iown-home (classes mode2w). */
#include "io_pair.h"
#include <cstring>
#include "iohome_2w.h"
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

extern "C" int io_hc_pair(uint8_t out_dst[3], uint32_t timeout_ms) {
    if (!out_dst) return -2;

    /* Identite de controleur : on REUTILISE la cle du reseau si la carte en a deja une (on ajoute
     * l'appareil au meme reseau), sinon on genere une source et une cle neuves. */
    uint8_t src[NODE_ID_SIZE], key[AES_KEY_SIZE];
    if (!io_store_get(src, key)) {
        esp_fill_random(src, sizeof src);
        esp_fill_random(key, sizeof key);
        ESP_LOGW(TAG, "nouvelle identite io (aucun reseau existant)");
    }

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
