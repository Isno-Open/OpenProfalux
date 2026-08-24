#include "profalux.h"
#include "keeloq.h"
#include "cc1101.h"
#include "pfx_keys.h"

/* --- Identite de test Phase 2 (reference DEVMEL) ---
 * Si PFX_USE_TEST_IDENTITY=1, pfx_state_reset() emet le serial DEVMEL du pool valide
 * (clef derivee via pfx_key_for_serial) au lieu d'un emetteur random.
 * serial 0x36067 = slot 54 (index 53) -> clef 0x41AA649E6ED6E5A7 (oracle valide).
 * Mettre a 0 pour revenir au self-learn random. */
#define PFX_USE_TEST_IDENTITY 1
#define PFX_TEST_SERIAL       0x00036067u
#include <string.h>
#include <esp_random.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "profalux";
static const char *NVS_NAMESPACE = "pfx_state";

int pfx_state_init(pfx_tx_state_t *st) {
    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(*st);
        if (nvs_get_blob(h, "state", st, &sz) == ESP_OK && sz == sizeof(*st)) loaded = true;
        nvs_close(h);
    }
#if PFX_USE_TEST_IDENTITY
    /* Force l'identite DEVMEL slot 54 (serial + cle deterministes), mais garde le
     * counter depuis NVS pour qu'il AVANCE (anti-replay). Premier init : counter=2
     * (= PAIRMODE DEVMEL, oracle 0xF029775B). A retirer apres appairage reussi. */
    uint64_t k; uint8_t idx;
    if (pfx_key_for_serial(PFX_TEST_SERIAL, &k, &idx)) {
        st->serial = PFX_TEST_SERIAL;
        st->crypt_key = k;
        st->discrimination = PFX_TEST_SERIAL & 0xFFF;   /* 0x067 */
        if (!loaded) st->counter = 2;
        ESP_LOGI(TAG, "Identite DEVMEL slot 54: serial=0x%08X idx=%u counter=%u (avance/anti-replay)",
                 (unsigned)st->serial, idx, (unsigned)st->counter);
        return pfx_state_save(st);
    }
    ESP_LOGW(TAG, "PFX_TEST_SERIAL 0x%08X invalide, fallback", (unsigned)PFX_TEST_SERIAL);
#endif
    if (loaded) {
        ESP_LOGI(TAG, "Loaded state: serial=0x%08X counter=%u",
                 (unsigned)st->serial, (unsigned)st->counter);
        return 0;
    }
    ESP_LOGI(TAG, "No state in NVS, generating new random");
    return pfx_state_reset(st);
}

int pfx_state_save(const pfx_tx_state_t *st) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, "state", st, sizeof(*st)));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    return 0;
}

int pfx_state_reset(pfx_tx_state_t *st) {
#if PFX_USE_TEST_IDENTITY
    /* Identite DEVMEL du pool valide (test Phase 2). Clef derivee du serial. */
    uint64_t k; uint8_t idx;
    if (pfx_key_for_serial(PFX_TEST_SERIAL, &k, &idx)) {
        st->serial = PFX_TEST_SERIAL;
        st->crypt_key = k;
        st->counter = 2;                            /* = counter DEVMEL PAIRMODE valide (oracle 0xF029775B) */
        st->discrimination = st->serial & 0xFFF;   /* = 0x067 (famille PFX) */
        ESP_LOGI(TAG, "Identite DEVMEL de test: serial=0x%08X idx=%u key=0x%016llX",
                 (unsigned)st->serial, idx, (unsigned long long)st->crypt_key);
        return pfx_state_save(st);
    }
    ESP_LOGW(TAG, "PFX_TEST_SERIAL 0x%08X invalide (pas famille 0x067 ?), fallback random",
             (unsigned)PFX_TEST_SERIAL);
#endif
    st->serial = esp_random() & 0x0FFFFFFF;
    st->crypt_key = ((uint64_t)esp_random() << 32) | esp_random();
    st->counter = 0;
    st->discrimination = st->serial & 0xFFF;
    return pfx_state_save(st);
}

void pfx_frame_build(const pfx_tx_state_t *st, uint8_t button, uint8_t frame[9]) {
    /* State to encrypt: button(4) | discrim(12) | counter(16) = 32 bits */
    uint32_t plaintext = ((uint32_t)(button & 0xF) << 28)
                       | ((uint32_t)(st->discrimination & 0xFFF) << 16)
                       | st->counter;
    uint32_t enc = keeloq_encrypt(plaintext, st->crypt_key);

    /* Hop chiffre : en HCS30x tout le mot est transmis LSB-first, comme le serial
     * (dont l'ordre LSB-first est verifie contre la capture reelle). Notre serial
     * etait deja LSB-first mais le hop etait MSB-first = incoherent. On bit-reverse
     * le hop pour qu'il sorte LSB-first lui aussi. Flag pour rebasculer si besoin. */
#ifndef PFX_HOP_LSB_FIRST
#define PFX_HOP_LSB_FIRST 1
#endif
#if PFX_HOP_LSB_FIRST
    uint32_t enc_tx = 0;
    for (int k = 0; k < 32; k++) enc_tx |= ((enc >> k) & 1u) << (31 - k);
#else
    uint32_t enc_tx = enc;
#endif

    /* Frame 66-bit layout (bit stream) :
     * [encrypted:32][serial:28][button:4][status:2]
     * empaquete MSB-first par octet (le cc1101 emet MSB-first) */
    memset(frame, 0, 9);
    frame[0] = (enc_tx >> 24) & 0xFF;
    frame[1] = (enc_tx >> 16) & 0xFF;
    frame[2] = (enc_tx >>  8) & 0xFF;
    frame[3] = (enc_tx >>  0) & 0xFF;
    /* Serial 28-bit serialise LSB-first (=comme les vraies trames HCS300/PFX 0x813,
     * verifie 25/25 contre captures reelles). On inverse les 28 bits du serial, puis
     * on empaquette MSB-first par octet (l'emission cc1101 est MSB-first) => le serial
     * ressort LSB-first sur l'air. crypt_key / counter / KeeLoq inchanges. */
    uint32_t sr = 0;
    for (int k = 0; k < 28; k++) sr |= ((st->serial >> k) & 1u) << (27 - k);
    frame[4] = (sr >> 20) & 0xFF;
    frame[5] = (sr >> 12) & 0xFF;
    frame[6] = (sr >>  4) & 0xFF;
    /* Les quatre bits bouton HCS300 sont eux aussi transmis LSB-first
     * (S0, S1, S2, S3). Le CC1101 parcourt le nibble bas MSB-first : il faut
     * donc inverser le nibble, comme pour le hopping et le serial. Sans cela,
     * 0x1 et 0x8 ainsi que 0x2 et 0x4 sont permutes sur l'air ; le bouton clair
     * ne correspond alors plus au bouton inclus dans le plaintext KeeLoq. */
    uint8_t br = 0;
    for (int k = 0; k < 4; k++) br |= ((button >> k) & 1u) << (3 - k);
    frame[7] = ((sr & 0xF) << 4) | br;
    /* Bits 4-3: button (already in frame[7]) */
    /* Bits 2-1: status flags = 0 (repeat=0, batt_low=0) */
    frame[8] = 0;
}

int pfx_frame_parse(const uint8_t frame[9], pfx_rx_frame_t *out) {
    if (!frame || !out) return -1;
    uint32_t enc = ((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16)
                 | ((uint32_t)frame[2] <<  8) | ((uint32_t)frame[3]);
    /* Serial LSB-first sur l'air (=meme convention que pfx_frame_build) : on lit le
     * champ empaquete (sr) puis on inverse les 28 bits pour retrouver le serial reel. */
    uint32_t sr = ((uint32_t)frame[4] << 20) | ((uint32_t)frame[5] << 12)
                | ((uint32_t)frame[6] <<  4) | ((uint32_t)(frame[7] >> 4) & 0x0F);
    uint32_t serial = 0;
    for (int k = 0; k < 28; k++) serial |= ((sr >> k) & 1u) << (27 - k);
    uint8_t br = frame[7] & 0x0F;
    uint8_t button = 0;
    for (int k = 0; k < 4; k++) button |= ((br >> k) & 1u) << (3 - k);
    uint8_t status = frame[8] & 0x03;

    out->serial = serial;
    out->encrypted_hop = enc;
    out->button = button;
    out->status_flags = status;
    out->counter = 0;
    out->decoded = false;
    return 0;
}

int pfx_frame_decrypt(pfx_rx_frame_t *frm, uint64_t crypt_key) {
    if (!frm) return -1;
    uint32_t plain = keeloq_decrypt(frm->encrypted_hop, crypt_key);
    /* Sanity check : discriminant should match 12 LSB of serial */
    uint16_t discrim = (plain >> 16) & 0xFFF;
    uint16_t expected = frm->serial & 0xFFF;
    if (discrim != expected) return -2; /* Wrong key */
    frm->counter = plain & 0xFFFF;
    frm->decoded = true;
    return 0;
}

void pfx_emit_burst(pfx_tx_state_t *st, uint8_t button, uint32_t n_frames, uint32_t duration_ms) {
    uint32_t delay_per_frame = duration_ms / (n_frames > 0 ? n_frames : 1);
    uint8_t frame[9];
    ESP_LOGI(TAG, "Burst start: %u frames over %u ms",
             (unsigned)n_frames, (unsigned)duration_ms);
    for (uint32_t i = 0; i < n_frames; i++) {
        pfx_frame_build(st, button, frame);
        cc1101_tx_ook_frame(frame, 66);
        st->counter++;
        if ((i & 0x0F) == 0) pfx_state_save(st);
        vTaskDelay(pdMS_TO_TICKS(delay_per_frame));
    }
    pfx_state_save(st);
    ESP_LOGI(TAG, "Burst done. Counter now = %u", (unsigned)st->counter);
}

void pfx_emit_hold(pfx_tx_state_t *st, uint8_t button, uint32_t duration_ms) {
    uint8_t frame[9];
    /* Un appui maintenu repete le meme codeword : counter/hopping fixes, RPT=0
     * sur la premiere trame puis RPT=1. Le compteur avance au relachement. */
    pfx_frame_build(st, button, frame);
    ESP_LOGI(TAG, "EMIT bouton 0x%X counter=%u FIXE pendant %u ms (RPT 0->1)",
             button, (unsigned)st->counter, (unsigned)duration_ms);
    uint32_t elapsed = 0, n = 0;
    while (elapsed < duration_ms) {
        frame[8] = (n == 0) ? 0x00 : 0x40;
        cc1101_tx_ook_frame(frame, 66);
        vTaskDelay(pdMS_TO_TICKS(60));
        elapsed += 165; n++;
    }
    st->counter++;
    pfx_state_save(st);
    ESP_LOGI(TAG, "EMIT termine: %u repetitions (code fixe), counter->%u",
             (unsigned)n, (unsigned)st->counter);
}

void pfx_emit_enroll(pfx_tx_state_t *st) {
    ESP_LOGI(TAG, "SETTINGS interne: 5000 ms, aucune TX RF (counter=%u)",
             (unsigned)st->counter);
    vTaskDelay(pdMS_TO_TICKS(5000));
    st->counter++;
    pfx_state_save(st);
    ESP_LOGI(TAG, "ENROLL RF: btn=0x5 counter=%u plain=0x%08X",
             (unsigned)st->counter,
             (unsigned)((0x5u << 28) | ((uint32_t)(st->discrimination & 0xFFFu) << 16) | st->counter));
    pfx_emit_command(st, PFX_BTN_ENROLL);
}

void pfx_emit_command(pfx_tx_state_t *st, uint8_t button) {
    uint8_t frame[9];
    pfx_frame_build(st, button, frame);
    for (int i = 0; i < 8; i++) {
        frame[8] = (i == 0) ? 0x00 : 0x40;
        cc1101_tx_ook_frame(frame, 66);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    st->counter++;
    pfx_state_save(st);
}
