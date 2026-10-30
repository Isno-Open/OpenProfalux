/*
 * Profalux SIM version : uses mock NVS + no FreeRTOS
 * Same logic as profalux.c but PC-compilable for debug
 */
#ifdef SIM_MODE

#include "profalux.h"
#include "keeloq.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int mock_nvs_load(void *out, size_t max_sz, size_t *actual_sz);
extern int mock_nvs_save(const void *data, size_t sz);
extern uint32_t mock_random(void);
extern int cc1101_tx_ook_frame(const uint8_t *frame, size_t bits);

int pfx_state_init(pfx_tx_state_t *st) {
    size_t sz;
    if (mock_nvs_load(st, sizeof(*st), &sz) == 0 && sz == sizeof(*st)) {
        printf("[PFX] Loaded state: serial=0x%08X counter=%u\n", st->serial, st->counter);
        return 0;
    }
    return pfx_state_reset(st);
}

int pfx_state_save(const pfx_tx_state_t *st) {
    return mock_nvs_save(st, sizeof(*st));
}

int pfx_state_reset(pfx_tx_state_t *st) {
    st->serial = mock_random() & 0x0FFFFFFF;
    st->crypt_key = ((uint64_t)mock_random() << 32) | mock_random();
    st->counter = 0;
    st->discrimination = st->serial & 0xFFF;
    printf("[PFX] Generated new state:\n");
    printf("      Serial     = 0x%08X\n", st->serial);
    printf("      Crypt key  = 0x%016lX\n", (unsigned long)st->crypt_key);
    printf("      Counter    = %u\n", st->counter);
    printf("      Discrim    = 0x%03X\n", st->discrimination);
    return pfx_state_save(st);
}

void pfx_frame_build(const pfx_tx_state_t *st, uint8_t button, uint8_t frame[9]) {
    uint32_t plain = ((uint32_t)(button & 0xF) << 28)
                   | ((uint32_t)(st->discrimination & 0xFFF) << 16)
                   | st->counter;
    uint32_t enc = keeloq_encrypt(plain, st->crypt_key);
    memset(frame, 0, 9);
    frame[0] = (enc >> 24) & 0xFF; frame[1] = (enc >> 16) & 0xFF;
    frame[2] = (enc >>  8) & 0xFF; frame[3] = enc & 0xFF;
    frame[4] = (st->serial >> 20) & 0xFF; frame[5] = (st->serial >> 12) & 0xFF;
    frame[6] = (st->serial >>  4) & 0xFF;
    frame[7] = ((st->serial & 0xF) << 4) | (button & 0xF);
    frame[8] = 0;
}

int pfx_frame_parse(const uint8_t frame[9], pfx_rx_frame_t *out) {
    if (!frame || !out) return -1;
    uint32_t enc = ((uint32_t)frame[0] << 24) | ((uint32_t)frame[1] << 16)
                 | ((uint32_t)frame[2] <<  8) | ((uint32_t)frame[3]);
    uint32_t serial = ((uint32_t)frame[4] << 20) | ((uint32_t)frame[5] << 12)
                    | ((uint32_t)frame[6] <<  4) | (((uint32_t)frame[7] >> 4) & 0x0F);
    out->serial = serial;
    out->encrypted_hop = enc;
    out->button = frame[7] & 0x0F;
    out->status_flags = frame[8] & 0x03;
    out->counter = 0;
    out->decoded = false;
    return 0;
}

int pfx_frame_decrypt(pfx_rx_frame_t *frm, uint64_t crypt_key) {
    if (!frm) return -1;
    uint32_t plain = keeloq_decrypt(frm->encrypted_hop, crypt_key);
    uint16_t discrim = (plain >> 16) & 0xFFF;
    uint16_t expected = frm->serial & 0xFFF;
    if (discrim != expected) return -2;
    frm->counter = plain & 0xFFFF;
    frm->decoded = true;
    return 0;
}

void pfx_emit_burst(pfx_tx_state_t *st, uint8_t button, uint32_t n, uint32_t dur_ms) {
    uint32_t per = (n > 0) ? (dur_ms / n) : 1000;
    uint8_t frame[9];
    printf("[BURST START] %u frames over %ums (~%ums each)\n", n, dur_ms, per);
    for (uint32_t i = 0; i < n; i++) {
        pfx_frame_build(st, button, frame);
        printf("[Frame %02u/%02u] counter=%u | ", i+1, n, st->counter);
        cc1101_tx_ook_frame(frame, 66);
        st->counter++;
        if ((i & 0x0F) == 0) pfx_state_save(st);
        /* No actual delay in sim to run fast */
    }
    pfx_state_save(st);
    printf("[BURST DONE] Total emitted: %u. Counter now = %u\n", n, st->counter);
}

void pfx_emit_command(pfx_tx_state_t *st, uint8_t button) {
    uint8_t frame[9];
    const char *btn_name = button == 0x01 ? "DOWN" :
                          button == 0x02 ? "STOP" :
                          button == 0x04 ? "UP"   : "?";
    printf("[CMD %s] counter=%u\n", btn_name, st->counter);
    for (int i = 0; i < 3; i++) {
        pfx_frame_build(st, button, frame);
        cc1101_tx_ook_frame(frame, 66);
        st->counter++;
    }
    pfx_state_save(st);
}

#endif /* SIM_MODE */
