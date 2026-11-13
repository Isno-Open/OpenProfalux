#include <algorithm>
/* Banc hote : vrai code frame/send/pair/store, radio/NVS/AES simules. */
#include "iohome_frame.h"
#include "iohome_crypto.h"
#include "iohome_constants.h"
#include "io_pair.h"
#include "io_send.h"
#include "io_radio.h"
#include "nvs.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#if __has_include("io_compat_pool_test.inc") && __has_include("io_compat_pool.inc")
#include "io_compat_pool_test.inc"
#define IO_TEST_HAVE_COMPAT_POOL 1
#else
#define IO_TEST_HAVE_COMPAT_POOL 0
#endif

using namespace iohome;
static std::map<std::string, std::vector<uint8_t>> saved, pending;
static bool commit_ok = true, radio_ok = true;
static std::vector<std::vector<uint8_t>> sent;
static std::vector<int64_t> sent_at;

extern "C" {
int nvs_open(const char *, int, nvs_handle_t *h) { *h = 1; pending = saved; return ESP_OK; }
void nvs_close(nvs_handle_t) {}
int nvs_commit(nvs_handle_t) { if (!commit_ok) return ESP_ERR_INVALID_STATE; saved = pending; return ESP_OK; }
int nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *n) {
    auto it = pending.find(key);
    if (it == pending.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (*n < it->second.size()) return ESP_ERR_INVALID_STATE;
    *n = it->second.size(); memcpy(out, it->second.data(), *n); return ESP_OK;
}
int nvs_set_blob(nvs_handle_t, const char *key, const void *data, size_t n) {
    const auto *p = static_cast<const uint8_t *>(data);
    pending[key] = std::vector<uint8_t>(p, p + n); return ESP_OK;
}
int nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *out) { size_t n = 4; return nvs_get_blob(h, key, out, &n); }
int nvs_set_u32(nvs_handle_t h, const char *key, uint32_t n) { return nvs_set_blob(h, key, &n, 4); }
int nvs_get_used_entry_count(nvs_handle_t, size_t *n) { *n = saved.size(); return ESP_OK; }
int nvs_erase_all(nvs_handle_t) { pending.clear(); return ESP_OK; }
int io_radio_tx_frame(const uint8_t *p, uint8_t n) {
    return io_radio_tx_frame_at(p, n, 0, NULL);
}
int io_radio_tx_frame_at(const uint8_t *p, uint8_t n, int64_t at, int64_t *actual) {
    const int64_t start = at ? at : (sent_at.empty() ? 1000000 : sent_at.back() + 10000);
    sent.emplace_back(p, p + n); sent_at.push_back(start);
    if (actual) *actual = start;
    return radio_ok ? 0 : -1;
}
}
int esp_read_mac(uint8_t mac[6], int) {
    const uint8_t fixed[] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    memcpy(mac, fixed, 6); return ESP_OK;
}

/* CRC de reference ; le double AES expose le compteur donne au MAC, sans tester AES. */
namespace iohome { namespace crypto {
bool aes128_encrypt(const uint8_t input[16], const uint8_t key[16], uint8_t output[16]) {
    const uint8_t expected_tk[] = {0x34,0xc3,0x46,0x6e,0xd8,0x8f,0x4e,0x8e,0x16,0xaa,0x47,0x39,0x49,0x88,0x43,0x73};
    if (memcmp(key, expected_tk, 16)) return false;
#if IO_TEST_HAVE_COMPAT_POOL
    if (!memcmp(input, COMPAT_TEST_IV, 16)) { memcpy(output, COMPAT_TEST_AES, 16); return true; }
#endif
    for (int i = 0; i < 16; i++) output[i] = input[i] ^ key[i];
    return true;
}
uint16_t compute_crc16(const uint8_t *data, size_t n, uint16_t crc) {
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    return crc;
}
bool verify_crc16(const uint8_t *p, size_t n) {
    return n >= 2 && compute_crc16(p, n - 2) == (p[n - 2] | p[n - 1] << 8);
}
bool create_1w_hmac(const uint8_t *, size_t, const uint8_t seq[2], const uint8_t[16], uint8_t out[6]) {
    memset(out, 0, 6); memcpy(out, seq, 2); return true;
}
bool encrypt_1w_key(const uint8_t key[16], const uint8_t[3], uint8_t out[16]) {
    memcpy(out, key, 16); return true;
}
}}

// Rafale de COMMANDE = 4 trames meme compteur. first_ctrl1 = octet de controle 1 CAPTE (basse conso) ;
// clear_after : la commande retire le bit LPM apres la 1re trame. (L'enrolement 0x30 utilise la paire
// de reference FC20->FC00, verifiee separement plus bas, pas cette fonction.)
static void check_burst(uint16_t rolling, size_t counter_at, uint8_t first_ctrl1, bool clear_after) {
    assert(sent.size() == 4);
    for (size_t i = 0; i < sent.size(); i++) {
        const auto &f = sent[i];
        assert(f.size() == sent.front().size());
        assert(std::equal(f.begin() + 2, f.end() - 2, sent.front().begin() + 2));
        assert(f[0] == sent.front()[0]);
        uint8_t expect1 = (i == 0 || !clear_after) ? first_ctrl1 : (uint8_t)(first_ctrl1 & ~0x20);
        assert(f[1] == expect1);
        assert(f[counter_at] == (rolling >> 8) && f[counter_at + 1] == (rolling & 255));
        assert(crypto::verify_crc16(f.data(), f.size()));
    }
    sent.clear();
    sent_at.clear();
}

// Fabrique une trame io valide pour le banc (CRC = celui du stub). hmac=6 pour une commande, 0 pour un 0x30.
static std::vector<uint8_t> mkframe(uint8_t ctrl1, const uint8_t dest[3], uint8_t cmd,
                                    std::vector<uint8_t> body, int hmac) {
    size_t total = 9 + body.size() + 2 /*rolling*/ + hmac + 2 /*crc*/;
    std::vector<uint8_t> f;
    f.push_back((uint8_t)(0xC0 | 0x20 | (total - 3)));   // ordre (debut+fin) + protocole 1W + longueur
    f.push_back(ctrl1);
    f.insert(f.end(), dest, dest + 3);
    const uint8_t remote[3] = {0x6D, 0x18, 0x46};        // la vraie telecommande (indifferent a l'extraction)
    f.insert(f.end(), remote, remote + 3);
    f.push_back(cmd);
    f.insert(f.end(), body.begin(), body.end());
    f.resize(total - 2, 0x00);                           // code roulant + HMAC a zero (le banc ne teste pas l'AES)
    uint16_t c = crypto::compute_crc16(f.data(), f.size());
    f.push_back((uint8_t)c); f.push_back((uint8_t)(c >> 8));
    return f;
}

int main() {
    const uint8_t cmd_dst[3]    = {0x00, 0x01, 0xBF};    // dest des commandes (Somfy)
    const uint8_t enroll_dst[3] = {0x00, 0x00, 0x3F};    // dest d'enrolement (capte du 0x30)
    // Trois commandes 0x00 reelles d'un Somfy secteur (ctrl1 = 0x00) : seules [2] (bouton) et [5] different.
    auto up   = mkframe(0x00, cmd_dst, CMD_EXECUTE, {0x01,0x43,0x00,0x00,0x80,0xD3,0x00,0x00}, 6);
    auto stop = mkframe(0x00, cmd_dst, CMD_EXECUTE, {0x01,0x43,0xD2,0x00,0x80,0xD2,0x00,0x00}, 6);
    auto down = mkframe(0x00, cmd_dst, CMD_EXECUTE, {0x01,0x43,0xC8,0x00,0x80,0xD3,0x00,0x00}, 6);
    // Le 0x30 d'enrolement : cle(16) + man_id(0x02 Somfy) + statut(0x01), pas de HMAC.
    std::vector<uint8_t> key30(16); for (int i = 0; i < 16; i++) key30[i] = (uint8_t)(0x40 + i);
    std::vector<uint8_t> body30 = key30; body30.push_back(0x02); body30.push_back(0x01);
    /* La capture retient la PREMIERE copie du couple reel, donc FC20. L'emetteur doit tout de meme
     * reconstruire FC20 -> FC00 ; ce cas attrape la regression FC20 -> FC20. */
    auto enr = mkframe(0x20, enroll_dst, CMD_KEY_TRANSFER_1W, body30, 0);

    // --- Extraction : rien n'est suppose, tout vient des trames. L'appelant met le profil a zero. ---
    io_profile_t profile; memset(&profile, 0, sizeof profile);
    assert(io_profile_from_capture(up.data(),   (int)up.size(),   &profile, IO_ACTION_UP)   == 0);
    assert(io_profile_from_capture(stop.data(), (int)stop.size(), &profile, IO_ACTION_STOP) == 0);
    assert(io_profile_from_capture(down.data(), (int)down.size(), &profile, IO_ACTION_DOWN) == 0);
    assert(io_enroll_from_capture(enr.data(),   (int)enr.size(),  &profile) == 0);
    assert(memcmp(profile.cmd_target, cmd_dst, 3) == 0 && memcmp(profile.enroll_target, enroll_dst, 3) == 0);
    assert(profile.man_id == 0x02 && profile.pair_status == 0x01 && profile.low_power == 0x00);
    assert(profile.payload_len[IO_ACTION_UP] == 8 && profile.payload_len[IO_ACTION_STOP] == 8);
    assert(profile.payload[IO_ACTION_UP][2]   == 0x00 && profile.payload[IO_ACTION_UP][5]   == 0xD3);
    assert(profile.payload[IO_ACTION_DOWN][2] == 0xC8 && profile.payload[IO_ACTION_DOWN][5] == 0xD3);
    assert(profile.payload[IO_ACTION_STOP][2] == 0xD2 && profile.payload[IO_ACTION_STOP][5] == 0xD2);
    // Captures invalides : tronquee, CRC casse, mauvaise commande.
    assert(io_profile_from_capture(up.data(), (int)up.size() - 1, &profile, IO_ACTION_UP) == -1);
    up[9] ^= 1; assert(io_profile_from_capture(up.data(), (int)up.size(), &profile, IO_ACTION_UP) == -1); up[9] ^= 1;
    assert(io_enroll_from_capture(up.data(), (int)up.size(), &profile) == -1);   // un 0x00 n'est pas un 0x30

    uint8_t src[3], key[16], src2[3], key2[16];
    assert(io_derive_identity(0, src, key) && io_derive_identity(0, src2, key2));
    const uint8_t expected_src[] = {0x06, 0xC1, 0x84};   /* 1er octet force en type controleur 0x02..0x09 (0xAC & 7 = 4 -> 0x02+4 = 0x06) */
    const uint8_t expected_key[] = {0x13,0x96,0x29,0xF8,0x1D,0xE6,0x5A,0x06,0xD8,0x8F,0x13,0xA3,0x10,0x0F,0xC9,0x3E};
    assert(memcmp(src, expected_src, 3) == 0 && memcmp(key, expected_key, 16) == 0);
    assert(memcmp(src, src2, 3) == 0 && memcmp(key, key2, 16) == 0);
    assert(io_derive_identity(1, src2, key2) && memcmp(src, src2, 3) != 0);
    // Pool de compatibilite (PRIVE, hors depot public) : reproduction deterministe, index 1, seed fixe
    // -> X4 et cle de l'oracle Python. Sans le fichier prive, le pool doit etre desactive.
    uint8_t dx4[4];
#if IO_TEST_HAVE_COMPAT_POOL
    assert(io_derive_compat_pool_identity(1, 1762768800000000ULL, src2, key2, dx4));
    assert(memcmp(src2, COMPAT_TEST_SRC, 3) == 0 && memcmp(dx4, COMPAT_TEST_X4, 4) == 0);
    assert(memcmp(key2, COMPAT_TEST_KEY, 16) == 0);
#else
    assert(!io_derive_compat_pool_identity(1, 1762768800000000ULL, src2, key2, dx4));
#endif
    assert(!io_derive_compat_pool_identity(11, 1, src2, key2, dx4));
    // Pool OpenProfalux : 11 identites fixes distinctes, meme X4 et meme fabrique de cle.
    uint8_t local_seen[11][3];
    for (uint8_t i = 0; i < 11; i++) {
        uint8_t lk[16], lx4[4], lk_again[16], lx4_again[4];
        assert(io_derive_local_pool_identity(i, 1762768800000000ULL, local_seen[i], lk, lx4));
        assert(local_seen[i][0] >= 0x02 && local_seen[i][0] <= 0x09 && lx4[3] == i);
        assert(io_derive_local_pool_identity(i, 1762768800000000ULL, src2, lk_again, lx4_again));
        assert(memcmp(local_seen[i], src2, 3) == 0 && memcmp(lk, lk_again, 16) == 0 &&
               memcmp(lx4, lx4_again, 4) == 0);
        for (uint8_t j = 0; j < i; j++) assert(memcmp(local_seen[i], local_seen[j], 3) != 0);
#if IO_TEST_HAVE_COMPAT_POOL
        for (uint8_t j = 0; j < 11; j++) assert(memcmp(local_seen[i], COMPAT_TEST_IDS[j], 3) != 0);
#endif
    }
    assert(!io_derive_local_pool_identity(11, 1, src2, key2, dx4));
    // Identite tiree : adresse en plage controleur quel que soit l'alea, cle = R ^ AES(IV) ^ X4.
    {
        const uint8_t first_bytes[] = {0x00, 0xFF, 0x0D, 0x02, 0x09, 0x7A};
        for (uint8_t fb : first_bytes) {
            uint8_t rnd[19], rs[3], rk[16], rx4[4], iv[16], aes[16];
            for (int i = 0; i < 19; i++) rnd[i] = (uint8_t)(0xA5 ^ (i * 37) ^ fb);
            rnd[0] = fb;
            assert(io_make_random_identity(rnd, 1762768800000000ULL, rs, rk, rx4));
            assert(rs[0] >= 0x02 && rs[0] <= 0x09 && rs[1] == rnd[1] && rs[2] == rnd[2]);
            for (int i = 0; i < 16; i++) iv[i] = rs[i % 3];
            assert(iohome::crypto::aes128_encrypt(iv, iohome::TRANSFER_KEY, aes));
            for (int i = 0; i < 16; i++) assert((uint8_t)(rk[i] ^ aes[i] ^ rx4[i & 3]) == rnd[3 + i]);
            assert(rx4[3] == 0);   // X4 garde son format, index 0
            // R porte l'alea : meme adresse et meme heure, R different => cle differente
            uint8_t rnd2[19], rk2[16];
            memcpy(rnd2, rnd, 19); rnd2[10] ^= 0x01;
            assert(io_make_random_identity(rnd2, 1762768800000000ULL, rs, rk2, rx4));
            assert(memcmp(rk, rk2, 16) != 0);
        }
        uint8_t rs[3], rk[16];
        assert(!io_make_random_identity(nullptr, 1, rs, rk, nullptr));
    }
    /* 1er octet = type CONTROLEUR (0x02..0x09), comme les identites passerelle de reference : un moteur Somfy
     * n'enrole pas une src de type telecommande (0x0D). Garde-fou sur plusieurs index. */
    for (uint32_t i = 0; i < 8; i++) { uint8_t s[3], k[16]; assert(io_derive_identity(i, s, k) && s[0] >= 0x02 && s[0] <= 0x09); }

    assert(io_store_set(src, key, &profile, 0x1234));
    uint8_t stored_key[16]; io_profile_t stored_profile;
    assert(io_store_get_key(src, stored_key) && memcmp(key, stored_key, 16) == 0);
    assert(io_store_get_profile(src, &stored_profile) && memcmp(&profile, &stored_profile, sizeof profile) == 0);
    assert(sent.empty());

    // Commande MONTER : rejeu VERBATIM de payload[UP] (dest cmd_target, basse conso captee = 0x00).
    assert(io_hc_send_shutter_1w(src, key, IO_ACTION_UP) == 0);
    assert(sent[0].size() == 27 && memcmp(&sent[0][2], cmd_dst, 3) == 0);
    assert(sent[0][9+2] == 0x00 && sent[0][9+5] == 0xD3);   // bouton et [5] du vrai UP, non reecrits
    assert(sent[0][17] == 0x12 && sent[0][18] == 0x34);     // code roulant (payload 8 -> offset 17)
    check_burst(0x1234, 17, 0x00, true);
    assert(!io_store_set(src, key, &profile, 0x1234));       // deja consomme : jamais de recul
    assert(io_store_set(src, key, &profile, -1));

    // Commande DESCENDRE : payload[DOWN] verbatim ([2]=C8, [5]=D3), distinct de UP.
    assert(io_hc_send_shutter_1w(src, key, IO_ACTION_DOWN) == 0);
    assert(sent[0][9+2] == 0xC8 && sent[0][9+5] == 0xD3);
    check_burst(0x1235, 17, 0x00, true);

    // Enrolement ecole passerelle de reference (capture Somfy de Pascal 2026-11-08) : EXACTEMENT 2 copies a seq
    // CONSTANTE, FC20 (bit 0x20 de ctrl1) puis FC00. man_id = IO_ENROLL_MAN_ID (0x01, marque de
    // l'ENROLEUR), PAS le man_id capte du remote (0x02 ici). Le CRC couvre ctrl1 : propre a chaque
    // variante. (Ni 4x FC00 d'io12k, ni 4x FC20 de vodhash : la paire du Somfy.)
    assert(io_hc_pair_1w(&profile, src, key, NULL, NULL) == 0);
    assert(sent.size() == 2);                                              // paire de reference, pas 4 copies
    for (const auto &f : sent) {
        assert(f.size() == 31 && f[8] == 0x30 && memcmp(&f[2], enroll_dst, 3) == 0);
        assert(f[25] == IO_ENROLL_MAN_ID && f[26] == 0x01);               // enroleur 0x01, statut 0x01
        assert(f[27] == 0x12 && f[28] == 0x36);                           // MEME seq 0x1236 sur les 2
        assert(crypto::verify_crc16(f.data(), f.size()));                 // CRC propre a chaque variante
    }
    assert(sent[0][1] == 0x20 && sent[1][1] == 0x00);                     // copie 1 = FC20, copie 2 = FC00
    assert(sent_at[1] - sent_at[0] == (int64_t)IO_ENROLL_CTRL1_GAP_MS * 1000); // debut-a-debut = 143 ms
    assert(std::equal(sent[0].begin() + 2, sent[0].end() - 2, sent[1].begin() + 2));  // seul ctrl1+CRC changent
    assert(sent[0][0] == sent[1][0]);
    sent.clear();
    sent_at.clear();

    commit_ok = false;
    assert(io_hc_send_shutter_1w(src, key, IO_ACTION_DOWN) == -1 && sent.empty());
    assert(!io_store_set(src, key, &profile, 0x2000));
    commit_ok = true;
    radio_ok = false;
    assert(io_hc_send_shutter_1w(src, key, IO_ACTION_DOWN) == -1);
    check_burst(0x1237, 17, 0x00, true);   // une seule rafale meme si chaque TX echoue
    radio_ok = true;
    assert(io_store_set(src, key, &profile, 65535));
    assert(io_hc_send_shutter_1w(src, key, IO_ACTION_STOP) == 0);
    assert(sent[0][9+2] == 0xD2 && sent[0][9+5] == 0xD2);   // STOP : [5]=D2, pas D3
    check_burst(65535, 17, 0x00, true);
    assert(io_hc_send_shutter_1w(src, key, IO_ACTION_STOP) == -1 && sent.empty());
    assert(!io_store_set(src, key, &profile, 0));
    assert(io_store_erase_all() == 3);
    assert(!io_store_get_key(src, key2));
    uint16_t seq;
    assert(!io_store_next_rolling(src, &seq));
    assert(io_store_set(src, key, &profile, 0));
    assert(io_store_next_rolling(src, &seq) && seq == 0);
    // Identite FRAICHE (rolling=-1, jamais vue en NVS) : compteur initial = IO_ENROLL_INITIAL_ROLLING
    // (0x0003), verite terrain de la passerelle de reference (ses identites demarrent a 0003). src2 n'a jamais ete stockee.
    assert(io_store_set(src2, key, &profile, -1));
    assert(io_store_next_rolling(src2, &seq) && seq == 0x0003);   // 0x0003 voulu (reference), pas le vieux 0x0001
    // Ceremonie d'ajout iohc-flipper : 0x39 signe a NOTRE adresse vers 00003F, 0x30 (man_id CAPTE) vers
    // les 4 destinations au MEME code roulant, puis STOP et DESCENTE captes. Chaque trame : 4 copies, la
    // 1re avec le bit basse conso (sauf commandes : basse conso captee), un code roulant par trame.
    sent.clear(); sent_at.clear();
    assert(io_store_set(src, key, &profile, 0x0100));
    uint8_t cer30[31]; int cer30_len = 0;
    assert(io_hc_pair_1w_ceremony(&profile, src, key, cer30, &cer30_len) == 0);
    assert(sent.size() == 4 + 16 + 4 + 4);
    auto check_group = [&](size_t first, uint8_t cmd, const uint8_t dst[3], size_t seq_at, uint16_t seq,
                           bool lpm_first) {
        for (size_t c = 0; c < 4; c++) {
            const auto &f = sent[first + c];
            assert(f[8] == cmd && memcmp(&f[2], dst, 3) == 0 && memcmp(&f[5], src, 3) == 0);
            assert(f[1] == ((lpm_first && c == 0) ? 0x20 : 0x00));
            assert(f[seq_at] == (seq >> 8) && f[seq_at + 1] == (seq & 0xFF));
            assert(crypto::verify_crc16(f.data(), f.size()));
            assert(std::equal(f.begin() + 2, f.end() - 2, sent[first].begin() + 2));   // seuls ctrl1 et CRC changent
        }
    };
    check_group(0, 0x39, IO_PAIR_BROADCAST, 10, 0x0100, true);
    for (size_t d = 0; d < 4; d++) {
        check_group(4 + 4 * d, 0x30, IO_PAIR_DESTS[d], 27, 0x0101, true);   // meme code roulant partout
        assert(sent[4 + 4 * d][25] == profile.man_id && sent[4 + 4 * d][26] == profile.pair_status);
    }
    assert(profile.man_id == 0x02);                                      // capte, pas IO_ENROLL_MAN_ID
    check_group(20, 0x00, cmd_dst, 17, 0x0102, false);
    assert(sent[20][9 + 2] == 0xD2);                                     // STOP capte
    check_group(24, 0x00, cmd_dst, 17, 0x0103, false);
    assert(sent[24][9 + 2] == 0xC8);                                     // DESCENTE captee
    assert(cer30_len == 31 && memcmp(&cer30[2], IO_PAIR_DESTS[0], 3) == 0 && cer30[8] == 0x30);
    sent.clear(); sent_at.clear();
    io_profile_t no_man = profile;
    no_man.man_id = 0;                                                   // pas de 0x30 capte : on ne devine pas
    assert(io_hc_pair_1w_ceremony(&no_man, src, key, NULL, NULL) == -1 && sent.empty());

    puts("banc io 1W : capture par action + enrolement, rejeu verbatim, identite, compteur, rafales et persistance OK");
}
