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

int somfy_rts_demanchester(const char *sym, uint8_t out[SOMFY_RTS_FRAME_BYTES]) {
    if (!sym || !out)
        return -1;
    if ((int)strlen(sym) != SOMFY_RTS_FRAME_BITS * 2)
        return -1;
    for (int i = 0; i < SOMFY_RTS_FRAME_BYTES; i++)
        out[i] = 0;
    for (int bit = 0; bit < SOMFY_RTS_FRAME_BITS; bit++) {
        char a = sym[2 * bit], b = sym[2 * bit + 1];
        int value;
        if (a == '0' && b == '1')       value = 1;   /* bas puis haut = bit 1 */
        else if (a == '1' && b == '0')  value = 0;   /* haut puis bas = bit 0 */
        else                            return -1;   /* pas une transition : bruit */
        if (value) {
            int byte  = bit >> 3;
            int shift = 7 - (bit & 7);
            out[byte] |= (uint8_t)(1u << shift);
        }
    }
    return 0;
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

/* ── 80 bits (RTS "80-S"), repris d'ESPSomfy-RTS (#73) ──────────────────────── */

/* Octet 7 de l'extension (ESPSomfy encode80Byte7) : start + 4*repeat, avec repli. */
static uint8_t enc80_byte7(int start, int repeat) {
    while ((repeat * 4) + start > 255) repeat -= 15;
    return (uint8_t)(start + repeat * 4);
}

/* Checksum 4 bits des 3 octets d'extension (ESPSomfy calc80Checksum). */
static uint8_t calc80_checksum(uint8_t b0, uint8_t b1, uint8_t b2) {
    uint8_t cs = (uint8_t)(((b0 & 0xF0) >> 4) ^ ((b1 & 0xF0) >> 4));
    cs ^= (uint8_t)((b2 & 0xF0) >> 4);
    cs ^= (uint8_t)(b0 & 0x0F);
    cs ^= (uint8_t)(b1 & 0x0F);
    return (uint8_t)(cs & 0x0F);
}

void somfy_rts_frame80(const uint8_t base56[SOMFY_RTS_FRAME_BYTES], somfy_rts_cmd_t cmd,
                       uint8_t repeat, uint8_t out[SOMFY_RTS_FRAME_BYTES_80]) {
    if (!base56 || !out)
        return;
    for (int i = 0; i < SOMFY_RTS_FRAME_BYTES; i++)
        out[i] = base56[i];
    uint8_t b7, b8, b9;
    switch (cmd) {
    case SOMFY_RTS_CMD_UP:   b7 = enc80_byte7(196, repeat); b8 = 0x20; b9 = 0x00; break;
    case SOMFY_RTS_CMD_DOWN: b7 = enc80_byte7(196, repeat); b8 = 0x2C; b9 = 0x80; break;
    case SOMFY_RTS_CMD_MY:   /* stop chez nous */
    default:                 b7 = (uint8_t)(repeat > 0 ? 132 : 196); b8 = 0x2F; b9 = 0xF0; break;
    }
    b9 = (uint8_t)(b9 | calc80_checksum(b7, b8, b9));
    out[7] = b7;
    out[8] = b8;
    out[9] = b9;
}

int somfy_rts_tx_pulses80(const uint8_t bytes[SOMFY_RTS_FRAME_BYTES_80], bool first,
                          somfy_rts_pulse_t *out, int max) {
    if (!bytes || !out || max < 1)
        return -1;
    int n = 0;
    const int pairs = first ? SOMFY_RTS_HW_SYNC_FIRST : SOMFY_RTS_HW_SYNC_REPEAT;
    for (int i = 0; i < pairs; i++) {
        if (!push_pulse(out, &n, max, 1, SOMFY_RTS_HW_SYNC_US)) return -1;
        if (!push_pulse(out, &n, max, 0, SOMFY_RTS_HW_SYNC_US)) return -1;
    }
    if (!push_pulse(out, &n, max, 1, SOMFY_RTS_SW_SYNC_US))     return -1;
    if (!push_pulse(out, &n, max, 0, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
    /* Donnees : 80 bits Manchester (poids fort en tete). 1 = bas puis haut, 0 = haut puis bas. */
    for (int bit = 0; bit < SOMFY_RTS_FRAME_BITS_80; bit++) {
        int byte  = bit >> 3;
        int shift = 7 - (bit & 7);
        int b     = (bytes[byte] >> shift) & 1;
        if (b) {
            if (!push_pulse(out, &n, max, 0, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
            if (!push_pulse(out, &n, max, 1, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
        } else {
            if (!push_pulse(out, &n, max, 1, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
            if (!push_pulse(out, &n, max, 0, SOMFY_RTS_HALF_SYMBOL_US)) return -1;
        }
    }
    return n;
}

void somfy_rts_remote_next(somfy_rts_remote_t *r, somfy_rts_cmd_t cmd,
                           uint8_t out[SOMFY_RTS_FRAME_BYTES]) {
    if (!r || !out)
        return;
    const somfy_rts_frame_t f = {
        .key          = (uint8_t)(SOMFY_RTS_KEY_PREFIX | (r->rolling_code & 0x0F)),
        .cmd          = cmd,
        .rolling_code = r->rolling_code,
        .address      = r->address & 0xFFFFFFu,
    };
    somfy_rts_pack(&f, out);
    r->rolling_code++;   /* repli naturel a 0 apres 0xFFFF (uint16_t) */
}
