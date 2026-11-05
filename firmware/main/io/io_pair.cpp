/* io_pair : voir io_pair.h. Orchestration de l'appairage io, d'apres iown-home (classes
 * mode2w) et la capture 1W de samr037/iohc-flipper. UN SEUL mode : 1W.
 * La carte POUSSE sa cle vers un moteur mis en appairage par parrainage. CC1101 seul, ni
 * reception ni saut de frequence. Le 2W (decouverte + defi-reponse) a ete RETIRE le 2026-10-28 :
 * il n'a jamais ete valide sur du materiel, et le porter en parallele du 1W brouillait le
 * diagnostic (#87). */
#include "io_pair.h"
#include <cstring>
#include "iohome_crypto.h"
#include "iohome_frame.h"
#include "iohome_constants.h"
#include "io_radio.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace iohome;
using namespace iohome::frame;

/* Apprend le profil d'emission depuis une trame io captee de la VRAIE telecommande du volet. On ne
 * retient que les commandes 0x00 : leur destination (broadcast encodant le type) et leur charge utile
 * entiere sont ce qu'on doit rejouer. Disposition 1W :
 *   ctrl0 ctrl1 | dest[3] | src[3] | cmd | charge utile | code roulant[2] | HMAC[6] | CRC[2]
 * La charge utile reelle = tout entre le cmd (offset 9) et le code roulant, soit n - 9 - 2 - 6 - 2.
 * On la capture a sa VRAIE longueur (6 octets pour une p0x00_14, 8 pour une p0x00_16) au lieu de lire
 * des offsets fixes : sur une trame de 6 octets, frame[15]/frame[16] sont deja le code roulant.
 * Longueur et CRC verifies : une capture tronquee ou bruitee est refusee. Rend 0 si exploitable. */
extern "C" int io_profile_from_capture(const uint8_t *frame, int n, io_profile_t *out) {
    if (!frame || !out) return -1;
    const int overhead = 9 + ROLLING_CODE_SIZE + HMAC_SIZE + CRC_SIZE;   /* 9 + 2 + 6 + 2 = 19 */
    int data_len = n - overhead;
    if (data_len < 3 || data_len > IO_PROFILE_PAYLOAD_MAX) return -1;     /* besoin d'au moins origin/fab/bouton */
    if (n > FRAME_MAX_SIZE || get_frame_length(frame[0]) != n ||
        (frame[0] & (CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK)) != (CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK) ||
        !crypto::verify_crc16(frame, n) || frame[8] != CMD_EXECUTE) return -1;                              /* on n'apprend que sur une commande 0x00 */
    memset(out, 0, sizeof *out);
    memcpy(out->target, &frame[2], NODE_ID_SIZE);                        /* destination = broadcast type-dependant */
    memcpy(out->payload, &frame[9], data_len);                           /* charge utile reelle, telle quelle */
    out->payload_len = (uint8_t)data_len;
    return 0;
}

/* Profil de commande d'un volet io SYNTHETISE depuis la marque, SANS capture. Meme contenu que
 * io_profile_from_capture sur une vraie trame de volet, mais fabrique depuis les constantes : seul le
 * fabricant change avec la marque (0x43 Somfy / 0x61 Velux) ; le reste (origin, broadcast volet
 * 00 00 3F, code bouton) est le protocole io PARTAGE (samr037, valide sur Velux ET Somfy). Prouve au
 * banc : io_profile_synth(Velux) == io_profile_from_capture(vraie trame Velux). Le bouton est un
 * placeholder, io_hc_send_shutter_1w reecrit cet octet a chaque commande. -1 si marque io inconnue. */
extern "C" int io_profile_synth(uint8_t man_id, io_profile_t *out) {
    if (!out) return -1;
    uint8_t fab;
    if (man_id == MANUFACTURER_SOMFY_PAIR)      fab = MANUFACTURER_SOMFY;   /* 0x02 -> 0x43 */
    else if (man_id == MANUFACTURER_VELUX_PAIR) fab = MANUFACTURER_VELUX;   /* 0x01 -> 0x61 */
    else return -1;                                                        /* marque io inconnue */
    memset(out, 0, sizeof *out);
    memcpy(out->target, SOMFY_BROADCAST_ADDR, NODE_ID_SIZE);  /* 00 00 3F : broadcast volet io (Velux idem, mesure banc) */
    out->payload[0] = BUTTON_ORIGIN;   /* 0x01 */
    out->payload[1] = fab;             /* 0x43 Somfy / 0x61 Velux */
    out->payload[2] = BTN_UP;          /* placeholder : io_hc_send_shutter_1w reecrit le bouton par commande */
    out->payload_len = 6;              /* [origin, fabricant, bouton, 0, 0, 0] */
    return 0;
}

extern "C" const char *io_marque_name(uint8_t fabricant) {
    if (fabricant == MANUFACTURER_SOMFY) return "Somfy";   /* 0x43 */
    if (fabricant == MANUFACTURER_VELUX) return "Velux";   /* 0x61 */
    return nullptr;
}

static uint64_t splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Identite DETERMINISTE (survit au formatage/reflash de la MEME carte) : derivee de la MAC et de
 * l'index, meme calcul que feat/io-pair-known-identity. Le 1er octet de la source est force non nul :
 * un moteur n'accepte comme emetteur qu'un noeud de type telecommande (0xD), et une adresse a 1er
 * octet 0x00 serait d'un autre type (doc io, linklayer.md). */
extern "C" bool io_derive_identity(uint32_t index, uint8_t out_src[3], uint8_t out_key[16]) {
    if (!out_src || !out_key) return false;
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return false;
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
    return true;
}

extern "C" int io_hc_pair_1w(const io_profile_t *profile, const uint8_t src[3],
                             const uint8_t key[16], uint8_t man_id) {
    if (!profile || !src || !key) return -1;
    uint8_t f[31] = {0};
    f[0] = 28 | CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK;
    f[1] = CTRL1_LOW_POWER;   /* 0x20 : bit basse conso, mis par la box de copie sur le 0x30 d'une NOUVELLE identite (capture #87) */
    memcpy(&f[2], profile->target, NODE_ID_SIZE);
    memcpy(&f[5], src, NODE_ID_SIZE);
    f[8] = CMD_KEY_TRANSFER_1W;
    if (!crypto::encrypt_1w_key(key, src, &f[9])) return -1;
    f[25] = man_id;           /* fabricant : 0x01 Velux (seul accepte par un Velux), 0x02 Somfy */
    f[26] = PAIR_STATUS_BYTE;
    uint16_t seq;
    if (!io_store_next_rolling(src, &seq)) return -1;
    f[27] = (uint8_t)(seq >> 8);
    f[28] = (uint8_t)seq;
    uint16_t crc = crypto::compute_crc16(f, 29);
    f[29] = (uint8_t)crc;
    f[30] = (uint8_t)(crc >> 8);
    /* Enrolement « nouvelle telecommande » : UN SEUL transfert de cle 0x30, rien d'autre. L'ajout
     * d'une telecommande io se fait PAR PARRAINAGE (doc io « Copy Remote Controller (1W) », #87) :
     * la telecommande deja appairee ouvre l'apprentissage du moteur, nous sommes la nouvelle.
     * On ne reproduit PAS la ceremonie de la telecommande maitre (0x39 + rafale 0x20 + 0x2e) : la
     * rafale 0x20 est du REGLAGE DE FINS DE COURSE et dereglerait un moteur deja regle.
     * ⚠️ On reemet LES MEMES octets : repasser par le constructeur incrementerait le code roulant. */
    int rc = -1;
    for (uint8_t i = 0; i < IO_TX_REPEATS; i++) {
        if (io_radio_tx_frame(f, sizeof f) == 0) rc = 0;
        if (i + 1 < IO_TX_REPEATS) vTaskDelay(pdMS_TO_TICKS(IO_TX_REPEAT_GAP_MS));
    }
    return rc;
}
