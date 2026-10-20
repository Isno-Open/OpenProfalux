/*
 * somfy_rts.c : codec logique Somfy RTS (voir somfy_rts.h). Pur, teste sur PC.
 */
#include "somfy_rts.h"
#include <string.h>

uint8_t somfy_rts_checksum(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES]) {
    uint8_t cksum = 0;
    for (int i = 0; i < SOMFY_RTS_FRAME_BYTES; i++) {
        cksum ^= (uint8_t)(bytes[i] & 0x0F);
        cksum ^= (uint8_t)(bytes[i] >> 4);
    }
    return (uint8_t)(cksum & 0x0F);
}

void somfy_rts_pack(const somfy_rts_frame_t *f, uint8_t out[SOMFY_RTS_FRAME_BYTES]) {
    out[0] = f->key;
    out[1] = (uint8_t)(((uint8_t)f->cmd << 4) & 0xF0);   /* quartet de controle a 0 */
    out[2] = (uint8_t)(f->rolling_code >> 8);
    out[3] = (uint8_t)(f->rolling_code & 0xFF);
    out[4] = (uint8_t)(f->address & 0xFF);
    out[5] = (uint8_t)((f->address >> 8) & 0xFF);
    out[6] = (uint8_t)((f->address >> 16) & 0xFF);
    out[1] = (uint8_t)((out[1] & 0xF0) | somfy_rts_checksum(out));
    /* obscurcissement en cascade */
    for (int i = 1; i < SOMFY_RTS_FRAME_BYTES; i++)
        out[i] ^= out[i - 1];
}

bool somfy_rts_unpack(const uint8_t in[SOMFY_RTS_FRAME_BYTES], somfy_rts_frame_t *f) {
    uint8_t b[SOMFY_RTS_FRAME_BYTES];
    memcpy(b, in, SOMFY_RTS_FRAME_BYTES);
    /* annuler l'obscurcissement : de la fin vers le debut */
    for (int i = SOMFY_RTS_FRAME_BYTES - 1; i >= 1; i--)
        b[i] ^= b[i - 1];
    uint8_t stored = (uint8_t)(b[1] & 0x0F);
    uint8_t probe[SOMFY_RTS_FRAME_BYTES];
    memcpy(probe, b, SOMFY_RTS_FRAME_BYTES);
    probe[1] = (uint8_t)(probe[1] & 0xF0);               /* quartet de controle a 0 */
    if (somfy_rts_checksum(probe) != stored)
        return false;
    if (f) {
        f->key          = b[0];
        f->cmd          = (somfy_rts_cmd_t)((b[1] >> 4) & 0x0F);
        f->rolling_code = (uint16_t)(((uint16_t)b[2] << 8) | b[3]);
        f->address      = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16);
    }
    return true;
}

int somfy_rts_manchester(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES], char *out, int max) {
    const int n = SOMFY_RTS_FRAME_BITS * 2;
    if (!out || max < n + 1)
        return -1;
    int k = 0;
    for (int bit = 0; bit < SOMFY_RTS_FRAME_BITS; bit++) {
        int byte  = bit >> 3;
        int shift = 7 - (bit & 7);
        int b     = (bytes[byte] >> shift) & 1;
        if (b) {                 /* 1 = bas puis haut */
            out[k++] = '0';
            out[k++] = '1';
        } else {                 /* 0 = haut puis bas */
            out[k++] = '1';
            out[k++] = '0';
        }
    }
    out[k] = '\0';
    return k;
}

/* Ajoute une impulsion (level, us), en fusionnant avec la precedente si elle porte
 * deja ce niveau. Rend false si la place manque pour une NOUVELLE impulsion. */
static bool push_pulse(somfy_rts_pulse_t *out, int *n, int max, uint8_t level, uint32_t us) {
    if (*n > 0 && out[*n - 1].level == level) {
        out[*n - 1].us += us;
        return true;
    }
    if (*n >= max)
        return false;
    out[*n].level = level;
    out[*n].us    = us;
    (*n)++;
    return true;
}

int somfy_rts_tx_pulses(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES], bool first,
                        somfy_rts_pulse_t *out, int max) {
    if (!bytes || !out || max < 1)
        return -1;
    char sym[2 * SOMFY_RTS_FRAME_BITS + 1];
    if (somfy_rts_manchester(bytes, sym, (int)sizeof(sym)) < 0)
        return -1;
    int n = 0;
    const int pairs = first ? SOMFY_RTS_HW_SYNC_FIRST : SOMFY_RTS_HW_SYNC_REPEAT;
    /* Sync materiel : `pairs` paires haut/bas a HW_SYNC_US. */
    for (int i = 0; i < pairs; i++) {
        if (!push_pulse(out, &n, max, 1, SOMFY_RTS_HW_SYNC_US)) return -1;
        if (!push_pulse(out, &n, max, 0, SOMFY_RTS_HW_SYNC_US)) return -1;
    }
    /* Sync logiciel : un haut long, puis un demi-symbole bas (qui fusionnera avec un
     * premier demi-symbole de donnee s'il est bas lui aussi). */
    if (!push_pulse(out, &n, max, 1, SOMFY_RTS_SW_SYNC_US))     return -1;
    if (!push_pulse(out, &n, max, 0, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
    /* Donnees : un demi-symbole par caractere Manchester, niveaux adjacents fusionnes. */
    for (const char *p = sym; *p; p++) {
        uint8_t level = (*p == '1') ? 1 : 0;
        if (!push_pulse(out, &n, max, level, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
    }
    return n;
}
