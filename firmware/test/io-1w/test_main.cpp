#include <algorithm>
/* Banc hote : vrai code frame/send/pair/store, radio/NVS/AES simules. */
#include "iohome_frame.h"
#include "iohome_crypto.h"
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

using namespace iohome;
static std::map<std::string, std::vector<uint8_t>> saved, pending;
static bool commit_ok = true, radio_ok = true;
static std::vector<std::vector<uint8_t>> sent;

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
    sent.emplace_back(p, p + n); return radio_ok ? 0 : -1;
}
}
int esp_read_mac(uint8_t mac[6], int) {
    const uint8_t fixed[] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    memcpy(mac, fixed, 6); return ESP_OK;
}

/* CRC de reference ; le double AES expose le compteur donne au MAC, sans tester AES. */
namespace iohome { namespace crypto {
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

// Commandes : bit LPM sur la 1re trame seulement (comme la vraie telecommande). 0x30 : 4 copies identiques.
static void check_burst(uint16_t rolling, size_t counter_at, bool lpm_first_only = true) {
    assert(sent.size() == 4);
    for (size_t i = 0; i < sent.size(); i++) {
        const auto &f = sent[i];
        assert(f.size() == sent.front().size());
        assert(std::equal(f.begin() + 2, f.end() - 2, sent.front().begin() + 2));
        assert(f[0] == sent.front()[0]);
        assert(f[1] == ((i == 0 || !lpm_first_only) ? 0x20 : 0x00));
        assert(f[counter_at] == (rolling >> 8) && f[counter_at + 1] == (rolling & 255));
        assert(crypto::verify_crc16(f.data(), f.size()));
    }
    sent.clear();
}

int main() {
    uint8_t capture[] = {0xF6,0,0,0,0x3F,0xE2,0xC1,0x76,0,0x01,0x61,0,0,0,0,0x4E,0xBC,0xA3,0xF9,0xE2,0xD2,0xA5,0xC6,0xD5,0xDE};
    io_profile_t profile;
    assert(io_profile_from_capture(capture, sizeof capture, &profile) == 0);
    assert(profile.payload_len == 6 && profile.payload[1] == 0x61 && profile.target[2] == 0x3F);
    assert(io_profile_from_capture(capture, sizeof capture - 1, &profile) == -1);
    capture[9] ^= 1;
    assert(io_profile_from_capture(capture, sizeof capture, &profile) == -1);
    capture[9] ^= 1;
    assert(io_profile_from_capture(capture, sizeof capture, &profile) == 0);

    /* PREUVE : la SYNTHESE (sans capture) d'un volet Velux == le profil tire de sa VRAIE trame captee.
     * Byte a byte : on peut donc appairer sans faire capturer l'utilisateur, l'io etant un protocole
     * partage (seul le fabricant change). Le bouton capte ici est 0x00 (BTN_UP), notre placeholder aussi. */
    io_profile_t synth;
    assert(io_profile_synth(0x01 /* Velux */, &synth) == 0);
    assert(memcmp(&synth, &profile, sizeof synth) == 0);
    io_profile_t synth_somfy;
    assert(io_profile_synth(0x02 /* Somfy */, &synth_somfy) == 0);
    assert(synth_somfy.payload_len == 6 && synth_somfy.payload[0] == 0x01 &&
           synth_somfy.payload[1] == 0x43 && synth_somfy.target[2] == 0x3F);
    assert(io_profile_synth(0x05 /* marque io inconnue */, &synth) == -1);

    uint8_t src[3], key[16], src2[3], key2[16];
    assert(io_derive_identity(0, src, key) && io_derive_identity(0, src2, key2));
    const uint8_t expected_src[] = {0xAC, 0xC1, 0x84};
    const uint8_t expected_key[] = {0x13,0x96,0x29,0xF8,0x1D,0xE6,0x5A,0x06,0xD8,0x8F,0x13,0xA3,0x10,0x0F,0xC9,0x3E};
    assert(memcmp(src, expected_src, 3) == 0 && memcmp(key, expected_key, 16) == 0);
    assert(memcmp(src, src2, 3) == 0 && memcmp(key, key2, 16) == 0);
    assert(io_derive_identity(1, src2, key2) && memcmp(src, src2, 3) != 0);

    assert(io_store_set(src, key, &profile, 0x1234));
    uint8_t stored_key[16]; io_profile_t stored_profile;
    assert(io_store_get_key(src, stored_key) && memcmp(key, stored_key, 16) == 0);
    assert(io_store_get_profile(src, &stored_profile) && memcmp(&profile, &stored_profile, sizeof profile) == 0);
    assert(sent.empty());
    assert(io_hc_send_shutter_1w(src, key, BTN_UP) == 0);
    assert(sent[0].size() == 25 && sent[0][11] == BTN_UP);
    assert(sent[0][17] == 0x12 && sent[0][18] == 0x34); // entree du HMAC aussi poids fort en tete
    check_burst(0x1234, 15);
    assert(!io_store_set(src, key, &profile, 0x1234)); // deja consomme : jamais de recul
    assert(io_store_set(src, key, &profile, -1));
    assert(io_hc_send_shutter_1w(src, key, BTN_STOP) == 0);
    check_burst(0x1235, 15);

    assert(io_hc_pair_1w(&profile, src, key, 1) == 0);
    assert(sent[0].size() == 31 && sent[0][8] == 0x30 && sent[0][25] == 1);
    check_burst(0x1236, 27, false);
    commit_ok = false;
    assert(io_hc_send_shutter_1w(src, key, BTN_DOWN) == -1 && sent.empty());
    assert(!io_store_set(src, key, &profile, 0x2000));
    commit_ok = true;
    radio_ok = false;
    assert(io_hc_send_shutter_1w(src, key, BTN_DOWN) == -1);
    check_burst(0x1237, 15); // une seule rafale meme si chaque TX echoue
    radio_ok = true;
    assert(io_store_set(src, key, &profile, 65535));
    assert(io_hc_send_shutter_1w(src, key, BTN_STOP) == 0);
    check_burst(65535, 15);
    assert(io_hc_send_shutter_1w(src, key, BTN_STOP) == -1 && sent.empty());
    assert(!io_store_set(src, key, &profile, 0));
    assert(io_store_erase_all() == 3);
    assert(!io_store_get_key(src, key2));
    uint16_t seq;
    assert(!io_store_next_rolling(src, &seq));
    assert(io_store_set(src, key, &profile, 0));
    assert(io_store_next_rolling(src, &seq) && seq == 0);
    puts("banc io 1W : profil, identite, compteur poids fort, rafales et persistance OK");
}
