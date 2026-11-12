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
#include "io_send.h"
extern "C" {
#include "keeloq.h"
}
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>

using namespace iohome;
using namespace iohome::frame;

/* Pool de compatibilite : PRIVE, hors du depot public. Le fichier est fourni a la construction
 * depuis le depot prive (firmware-private/io_compat_pool.inc). Absent, ce pool est desactive et
 * le reste du firmware fonctionne a l'identique. */
#if __has_include("io_compat_pool.inc")
#include "io_compat_pool.inc"
#define IO_HAVE_COMPAT_POOL 1
#else
#define IO_HAVE_COMPAT_POOL 0
#endif

/* Pool produit : meme fabrique que le pool de compatibilite, avec ses propres identites et T.
 * Le fichier ne contient que des T masquees ; les valeurs claires ne sont jamais embarquees. */
#include "io_local_pool.inc"
#define LOCAL_IO_T_OBFK1 0x8C31E75A4DB209F6ULL
#define LOCAL_IO_T_OBFK2 0x27D4A90C61BEF853ULL
#define LOCAL_IO_T_PRIME 0x01000193u
#define LOCAL_IO_T_BASE  0x4F504649u
#define LOCAL_IO_T_STEP  0x9E3779B9u

static uint32_t pool_lcg(uint32_t x) { return 0x41C64E6Du * x + 0x3039u; }

static bool io_make_x4(uint8_t idx, uint64_t seed_us, uint8_t x4[4]) {
    if (!seed_us) {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) != 0) return false;
        seed_us = (uint64_t)tv.tv_sec * 1000000ULL + (uint32_t)tv.tv_usec;
    }
    uint32_t r1 = pool_lcg((uint32_t)seed_us), r2 = pool_lcg(r1), r3 = pool_lcg(r2);
    uint32_t upper = ((r1 >> 6) & 0x1FFC00u) | ((r2 >> 16) & 0x3FFu);
    uint32_t random30 = (upper << 10) | ((r3 >> 16) & 0x3FFu);
    uint64_t quotient = (((uint64_t)(random30 >> 1) * 0x80000101ULL) >> 54);
    uint32_t mixed = random30 + 2u * (uint32_t)quotient;
    uint32_t source = (mixed << 8) + idx + 0x100u;
    x4[0] = (uint8_t)(source >> 24); x4[1] = (uint8_t)(source >> 16);
    x4[2] = (uint8_t)(source >> 8);  x4[3] = (uint8_t)source;
    return true;
}

static bool io_derive_pool_identity(uint8_t idx, uint64_t seed_us,
                                    const uint8_t ids[11][3], const uint8_t obf[11][16],
                                    uint64_t k1, uint64_t k2, uint32_t prime,
                                    uint32_t base, uint32_t step,
                                    uint8_t src[3], uint8_t key[16], uint8_t out_x4[4]) {
    if (idx >= 11 || !src || !key) return false;
    uint8_t x4[4];
    if (!io_make_x4(idx, seed_us, x4)) return false;
    uint8_t iv[16], aes[16];
    memcpy(src, ids[idx], 3);
    for (int i = 0; i < 16; i++) iv[i] = src[i % 3];
    if (!crypto::aes128_encrypt(iv, TRANSFER_KEY, aes)) return false;
    for (int block = 0; block < 4; block++) {
        uint32_t mask_seed = base ^ ((uint32_t)idx * prime) ^ ((uint32_t)block * step);
        uint32_t mask = keeloq_encrypt(mask_seed, (block & 1) ? k2 : k1);
        for (int b = 0; b < 4; b++) {
            int i = block * 4 + b;
            uint8_t t = obf[idx][i] ^ (uint8_t)(mask >> (24 - 8 * b));
            key[i] = t ^ aes[i] ^ x4[i & 3];
        }
    }
    if (out_x4) memcpy(out_x4, x4, 4);
    return true;
}

extern "C" bool io_derive_compat_pool_identity(uint8_t idx, uint64_t seed_us,
                                                 uint8_t src[3], uint8_t key[16], uint8_t out_x4[4]) {
#if IO_HAVE_COMPAT_POOL
    return io_derive_pool_identity(idx, seed_us, COMPAT_IO_IDS, COMPAT_IO_T_OBF,
                                   COMPAT_IO_T_OBFK1, COMPAT_IO_T_OBFK2, COMPAT_IO_T_PRIME,
                                   COMPAT_IO_T_BASE, COMPAT_IO_T_STEP, src, key, out_x4);
#else
    (void)idx; (void)seed_us; (void)src; (void)key; (void)out_x4;
    return false;   /* pool prive absent de cette construction */
#endif
}

extern "C" bool io_derive_local_pool_identity(uint8_t idx, uint64_t seed_us,
                                                uint8_t src[3], uint8_t key[16], uint8_t out_x4[4]) {
    return io_derive_pool_identity(idx, seed_us, LOCAL_IO_IDS, LOCAL_IO_T_OBF,
                                   LOCAL_IO_T_OBFK1, LOCAL_IO_T_OBFK2, LOCAL_IO_T_PRIME,
                                   LOCAL_IO_T_BASE, LOCAL_IO_T_STEP, src, key, out_x4);
}

/* Apprend UNE action depuis une trame 0x00 captee de la VRAIE telecommande. Disposition 1W :
 *   ctrl0 ctrl1 | dest[3] | src[3] | cmd | charge utile | code roulant[2] | HMAC[6] | CRC[2]
 * La charge utile reelle = n - 9 - 2 - 6 - 2, capturee a sa VRAIE longueur et rejouee VERBATIM : on ne
 * reecrit RIEN, pas meme l'octet du bouton, puisque chaque action a sa propre capture. On remplit UNE
 * action de *out (cmd_target, payload[action], low_power) ; l'appelant a mis *out a zero et appelle
 * cette fonction une fois par action, plus io_enroll_from_capture. Longueur et CRC verifies : une
 * capture tronquee ou bruitee est refusee. Rend 0 si exploitable. */
extern "C" int io_profile_from_capture(const uint8_t *frame, int n, io_profile_t *out, io_action_t action) {
    if (!frame || !out || action < 0 || action >= IO_ACTION_COUNT) return -1;
    const int overhead = OFFSET_DATA + ROLLING_CODE_SIZE + HMAC_SIZE + CRC_SIZE;   /* 9 + 2 + 6 + 2 = 19 */
    int data_len = n - overhead;
    if (data_len < 3 || data_len > IO_PROFILE_PAYLOAD_MAX) return -1;     /* au moins origin/fabricant/bouton */
    if (n > FRAME_MAX_SIZE || get_frame_length(frame[OFFSET_CTRL_BYTE_0]) != n ||
        (frame[OFFSET_CTRL_BYTE_0] & (CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK)) != (CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK) ||
        !crypto::verify_crc16(frame, n) || frame[OFFSET_COMMAND_ID] != CMD_EXECUTE) return -1;   /* on n'apprend que sur un 0x00 */
    memcpy(out->cmd_target, &frame[OFFSET_DEST_NODE], NODE_ID_SIZE);     /* dest commande, captee */
    memcpy(out->payload[action], &frame[OFFSET_DATA], data_len);         /* charge reelle, rejouee verbatim */
    out->payload_len[action] = (uint8_t)data_len;
    out->low_power = frame[OFFSET_CTRL_BYTE_1];                          /* basse conso captee (sert aussi a l'enrolement) */
    return 0;
}

/* Apprend l'enrolement depuis le 0x30 de la VRAIE telecommande, capte pendant sa fenetre PROG : sa
 * destination d'enrolement, son fabricant et son statut. Rien
 * n'est suppose, tout est lu. Remplit les champs d'enrolement de *out (deja mis a zero par l'appelant).
 * Rend 0 si c'est un 0x30 valide, -1 sinon.
 * NOTE (option 2 « Somfy d'abord », 2026-11-05) : l'enrolement EXIGE un 0x30 capte, sans repli. Valide
 * sur Somfy (le remote de Pascal emet un 0x30 captable). Velux A RE-VALIDER avec vodhash : si un Velux
 * n'emet pas de 0x30 captable au PROG, on ajoutera a ce moment-la un repli enroll_target <- cmd_target. */
extern "C" int io_enroll_from_capture(const uint8_t *frame, int n, io_profile_t *out) {
    if (!frame || !out) return -1;
    if (n > FRAME_MAX_SIZE || get_frame_length(frame[OFFSET_CTRL_BYTE_0]) != n ||
        (frame[OFFSET_CTRL_BYTE_0] & (CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK)) != (CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK) ||
        !crypto::verify_crc16(frame, n) || frame[OFFSET_COMMAND_ID] != CMD_KEY_TRANSFER_1W) return -1;  /* dest apprise seulement sur un 0x30 */
    memcpy(out->enroll_target, &frame[OFFSET_DEST_NODE], NODE_ID_SIZE);  /* dest d'enrolement, captee */
    /* Ne pas recopier ctrl1 ici : la capture peut etre FC20 ou FC00 selon la copie retenue. Le bit
     * de commande reste celui appris sur les 0x00 ; la paire d'enrolement force elle-meme 20 -> 00. */
    out->man_id      = frame[OFFSET_PAIR_MAN_ID];                        /* man_id du REMOTE, capte pour INFO (panneau avance) ; notre emission utilise IO_ENROLL_MAN_ID, pas celui-ci */
    out->pair_status = frame[OFFSET_PAIR_STATUS];                        /* statut capte */
    return 0;
}

extern "C" const char *io_marque_name(uint8_t fabricant) {
    if (fabricant == MANUFACTURER_SOMFY) return "Somfy";   /* 0x43 */
    if (fabricant == MANUFACTURER_VELUX) return "Velux";   /* 0x61 */
    return nullptr;
}

/* Nom de marque depuis le man_id capte (octet 25 du 0x30), pour l'affichage du panneau avance.
 * Le marqueur de marque est la seule valeur de marque qui reste declaree (enum). */
extern "C" const char *io_man_name(uint8_t man_id) {
    if (man_id == MANUFACTURER_SOMFY_PAIR) return "Somfy";   /* 0x02 */
    if (man_id == MANUFACTURER_VELUX_PAIR) return "Velux";   /* 0x01 */
    return nullptr;
}

/* Octet de commande d'une trame io a l'offset fixe, ou 0xFF si trop courte (voir io_pair.h). */
extern "C" uint8_t io_frame_cmd(const uint8_t *frame, int n) {
    if (!frame || n <= OFFSET_COMMAND_ID) return 0xFF;
    return frame[OFFSET_COMMAND_ID];
}

/* Libelle d'un octet de commande io pour le journal (voir io_pair.h). Les octets sont ceux declares
 * dans iohome_constants.h : aucune valeur de protocole en dur ici. */
extern "C" const char *io_cmd_label(uint8_t cmd) {
    switch (cmd) {
        case CMD_EXECUTE:        return "0x00 commande";
        case CMD_PRIVATE_WRITE:  return "0x20 private-write";
        case CMD_PAIR_CONFIRM:   return "0x2e confirmation (fin ceremonie)";
        case CMD_KEY_TRANSFER_1W:return "0x30 cle d'enrolement";
        case CMD_REMOVE_1W:      return "0x39 depairage";
        default:                 return nullptr;
    }
}

/* Vrai si l'octet est la confirmation de fin de ceremonie (0x2e) (voir io_pair.h). */
extern "C" bool io_cmd_is_pair_confirm(uint8_t cmd) { return cmd == CMD_PAIR_CONFIRM; }

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
    /* 1er octet = TYPE de noeud (nibble bas) : une passerelle/controleur est en 0x02..0x09, une
     * telecommande en 0x0D. Les 11 identites de la passerelle de reference sont TOUTES en 0x02..0x09 ;
     * notre box joue le meme role, donc meme type. io12g..j emettait une src en 0x5D (type telecommande
     * 0xD) : le moteur Somfy a refuse l'enrolement. On force donc 0x02..0x09 (verite terrain capture
     * de reference 2026-11-08). Identite fraiche sinon (octets 1-2 derives MAC+index), jamais une src du pool de compatibilite. */
    out_src[0] = (uint8_t)(0x02 + (a & 0x07));
    out_src[1] = (uint8_t)((a >> 8) & 0xFF);
    out_src[2] = (uint8_t)((a >> 16) & 0xFF);
    for (int i = 0; i < 16; i += 8) {
        uint64_t k = splitmix64(&st);
        for (int j = 0; j < 8; j++) out_key[i + j] = (uint8_t)(k >> (8 * j));
    }
    return true;
}

/* Comme io_derive_identity, mais choisit le plus petit index dont la source n'est PAS deja enrolee
 * (io_store). Donne une identite DISTINCTE par volet io : sinon tous les volets prendraient l'index 0
 * (meme MAC+0) -> meme id, meme entree io_store, le 2e ecraserait le 1er. Pour le 1er volet, index 0
 * (dormant). */
extern "C" bool io_derive_free_identity(uint8_t out_src[3], uint8_t out_key[16]) {
    for (uint32_t index = 0; index < 4096; index++) {
        if (!io_derive_identity(index, out_src, out_key)) return false;
        uint8_t probe[16];
        if (!io_store_get_key(out_src, probe)) return true;   /* source pas encore enrolee = libre */
    }
    return false;   /* epuise (improbable) */
}

extern "C" int io_hc_pair_1w(const io_profile_t *profile, const uint8_t src[3],
                             const uint8_t key[16], uint8_t *out_frame, int *out_len) {
    if (!profile || !src || !key) return -1;
    uint8_t f[31] = {0};
    f[OFFSET_CTRL_BYTE_0] = 28 | CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK;   /* longueur 31 + debut/fin + protocole 1W */
    /* La capture peut tomber sur la premiere copie FC20. La copie de fermeture doit pourtant etre
     * FC00 : on conserve les autres bits captures, mais on force explicitement le bit LPM a zero. */
    f[OFFSET_CTRL_BYTE_1] = (uint8_t)(profile->low_power & ~CTRL1_LOW_POWER);
    memcpy(&f[OFFSET_DEST_NODE], profile->enroll_target, NODE_ID_SIZE);    /* CAPTE : dest d'enrolement (00 00 3F observe) */
    memcpy(&f[OFFSET_SRC_NODE], src, NODE_ID_SIZE);
    f[OFFSET_COMMAND_ID] = CMD_KEY_TRANSFER_1W;
    if (!crypto::encrypt_1w_key(key, src, &f[OFFSET_DATA])) return -1;
    f[OFFSET_PAIR_MAN_ID] = IO_ENROLL_MAN_ID;                             /* marque de l'ENROLEUR (0x01, passerelle de reference + vodhash), PAS le man_id capte du remote (bug io12g/h) */
    f[OFFSET_PAIR_STATUS] = profile->pair_status;                         /* CAPTE : statut */
    uint16_t seq;
    if (!io_store_next_rolling(src, &seq)) return -1;
    f[27] = (uint8_t)(seq >> 8);   /* code roulant (octets 27-28 du 0x30) */
    f[28] = (uint8_t)seq;
    uint16_t crc = crypto::compute_crc16(f, 29);
    f[29] = (uint8_t)crc;          /* CRC16 (octets 29-30), calcule sur NOTRE trame */
    f[30] = (uint8_t)(crc >> 8);
    if (out_frame) memcpy(out_frame, f, sizeof f);   /* trame exacte, pour le journal de diagnostic */
    if (out_len) *out_len = (int)sizeof f;
    /* Enrolement « nouvelle telecommande » : UN SEUL transfert de cle 0x30, rien d'autre. L'ajout
     * d'une telecommande io se fait PAR PARRAINAGE (doc io « Copy Remote Controller (1W) », #87) :
     * la telecommande deja appairee ouvre l'apprentissage du moteur, nous sommes la nouvelle.
     * On ne reproduit PAS la ceremonie de la telecommande maitre (0x39 + rafale 0x20 + 0x2e) : la
     * rafale 0x20 est du REGLAGE DE FINS DE COURSE et dereglerait un moteur deja regle.
     * ⚠️ Les deux copies gardent la MEME seq (seuls ctrl1 et son CRC changent) : repasser par le
     *    constructeur incrementerait le code roulant. */
    /* Rafale d'enrolement, ecole passerelle de reference (capture Somfy de Pascal, 2026-11-08) : DEUX copies a seq
     * CONSTANTE, la 1re avec le bit 0x20 de ctrl1 (FC20, debut de rafale), la 2e sans (FC00),
     * ~143 ms d'ecart. Le CRC couvre ctrl1, recalcule pour la variante FC20. io12k emettait 4x
     * FC00 (ni la reference ni vodhash) -> refus du Somfy. L'ecole vodhash/Velux (4x FC20 identiques)
     * reste un autre motif prouve, non retenu ici car la cible est un Somfy. */
    uint8_t f20[31];
    memcpy(f20, f, sizeof f);
    f20[OFFSET_CTRL_BYTE_1] = (uint8_t)(f[OFFSET_CTRL_BYTE_1] | CTRL1_LOW_POWER); /* bit 0x20 -> FC20 */
    uint16_t crc20 = crypto::compute_crc16(f20, 29);
    f20[29] = (uint8_t)crc20;          /* CRC propre a la variante FC20 */
    f20[30] = (uint8_t)(crc20 >> 8);

    int64_t start20 = 0;
    const int rc20 = io_radio_tx_frame_at(f20, sizeof f20, 0, &start20); /* copie 1 : FC20 */
    const int64_t start00 = start20 + (int64_t)IO_ENROLL_CTRL1_GAP_MS * 1000;
    const int rc00 = io_radio_tx_frame_at(f, sizeof f, start00, NULL);   /* FC00 exactement +143 ms */
    return (rc20 == 0 && rc00 == 0) ? 0 : -1;                     /* une paire partielle n'est pas un succes */
}

/* Ceremonie d'ajout d'une telecommande 1W, recette Situo io (voir IO_SITUO_* dans iohome_constants.h) :
 *   0x39 signe a NOTRE adresse -> 0x30 (cle) -> 0x20 02 03 0C 00 00 signe -> 0x20 02 03 05 FF 00 signe.
 * Chaque trame : son propre code roulant, 4 copies identiques a 40 ms, ctrl1 0x00.
 * man_id du 0x30 = celui CAPTE du 0x30 de la telecommande d'origine (0x02 Somfy, 0x01 Velux) : il suit
 * la marque sans valeur en dur. La telecommande d'origine doit avoir mis le moteur en mode ajout (PROG)
 * juste avant : c'est l'appelant (clic Appairer) qui garantit ce moment.
 * out_frame/out_len recoivent le 0x30 emis, pour le journal. */
extern "C" int io_hc_pair_1w_situo(const io_profile_t *profile, const uint8_t src[3],
                                   const uint8_t key[16], uint8_t *out_frame, int *out_len) {
    if (!profile || !src || !key) return -1;
    if (profile->man_id == 0) return -1;   /* pas de 0x30 capte : on ne devine pas la marque */
    const uint8_t *dst = profile->enroll_target;

    /* 1. 0x39 signe : n'efface que notre propre entree eventuelle (essai precedent). */
    if (io_hc_send_signed_1w(src, key, dst, CMD_REMOVE_1W, IO_SITUO_REMOVE_DATA,
                             sizeof IO_SITUO_REMOVE_DATA, IO_SITUO_CTRL1, IO_SITUO_REPEAT_GAP_MS) != 0)
        return -1;
    vTaskDelay(pdMS_TO_TICKS(IO_SITUO_REPEAT_GAP_MS));

    /* 2. 0x30 : cle enveloppee + man_id capte + statut capte + code roulant, sans MAC. */
    uint8_t f[31] = {0};
    f[OFFSET_CTRL_BYTE_0] = 28 | CTRL0_ORDER_MASK | CTRL0_PROTOCOL_MASK;
    f[OFFSET_CTRL_BYTE_1] = IO_SITUO_CTRL1;
    memcpy(&f[OFFSET_DEST_NODE], dst, NODE_ID_SIZE);
    memcpy(&f[OFFSET_SRC_NODE], src, NODE_ID_SIZE);
    f[OFFSET_COMMAND_ID] = CMD_KEY_TRANSFER_1W;
    if (!crypto::encrypt_1w_key(key, src, &f[OFFSET_DATA])) return -1;
    f[OFFSET_PAIR_MAN_ID] = profile->man_id;
    f[OFFSET_PAIR_STATUS] = profile->pair_status;
    uint16_t seq;
    if (!io_store_next_rolling(src, &seq)) return -1;
    f[27] = (uint8_t)(seq >> 8);
    f[28] = (uint8_t)seq;
    uint16_t crc = crypto::compute_crc16(f, 29);
    f[29] = (uint8_t)crc;
    f[30] = (uint8_t)(crc >> 8);
    if (out_frame) memcpy(out_frame, f, sizeof f);
    if (out_len) *out_len = (int)sizeof f;
    if (io_hc_send_frame_burst(f, sizeof f, IO_SITUO_REPEAT_GAP_MS) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(IO_SITUO_REPEAT_GAP_MS));

    /* 3 et 4. Les deux 0x20 signes avec la cle qu'on vient de transmettre. */
    if (io_hc_send_signed_1w(src, key, dst, CMD_PRIVATE_WRITE, IO_SITUO_PRIVATE_1,
                             sizeof IO_SITUO_PRIVATE_1, IO_SITUO_CTRL1, IO_SITUO_REPEAT_GAP_MS) != 0)
        return -1;
    vTaskDelay(pdMS_TO_TICKS(IO_SITUO_REPEAT_GAP_MS));
    return io_hc_send_signed_1w(src, key, dst, CMD_PRIVATE_WRITE, IO_SITUO_PRIVATE_2,
                                sizeof IO_SITUO_PRIVATE_2, IO_SITUO_CTRL1, IO_SITUO_REPEAT_GAP_MS);
}
