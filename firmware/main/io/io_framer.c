/* io_framer : voir io_framer.h. Repris de samr037/iohc-flipper (iohc/frame_build.c, MIT). */
#include "io_framer.h"

#define IOHC_TX_PREFIX_BYTE 0x99   /* residu apres le sync 0x57FD (queue du 0x33 encode + stop) */

typedef struct {
    uint8_t *buf;
    uint16_t bit_count;
    uint16_t bit_capacity;
} BitPacker;

static void bp_push_bit(BitPacker *p, uint8_t bit) {
    uint16_t byte_idx = p->bit_count >> 3;
    uint8_t bit_idx = 7 - (uint8_t)(p->bit_count & 7);
    if (byte_idx >= (p->bit_capacity >> 3)) return;
    if (bit & 1) p->buf[byte_idx] |= (uint8_t)(1u << bit_idx);
    else p->buf[byte_idx] &= (uint8_t)~(1u << bit_idx);
    p->bit_count++;
}

/* Un octet io encapsule UART : start(0), 8 bits de donnee LSB-first, stop(1). */
static void bp_push_uart_byte(BitPacker *p, uint8_t b) {
    bp_push_bit(p, 0);
    for (int i = 0; i < 8; i++) bp_push_bit(p, (uint8_t)((b >> i) & 1));
    bp_push_bit(p, 1);
}

size_t io_frame_to_fifo(const uint8_t *frame, uint8_t total, uint8_t *out, size_t out_cap) {
    if (!frame || !out || out_cap == 0) return 0;
    for (size_t i = 0; i < out_cap; i++) out[i] = 0;
    out[0] = IOHC_TX_PREFIX_BYTE;
    BitPacker bp = { out, 8, (uint16_t)(out_cap * 8) };   /* residu deja a l'offset 0 */
    for (uint8_t i = 0; i < total; i++) bp_push_uart_byte(&bp, frame[i]);
    while (bp.bit_count & 7) bp_push_bit(&bp, 1);          /* complete le dernier octet (ligne au repos) */
    size_t n = (size_t)(bp.bit_count >> 3);
    /* Depassement de capacite : bit_count s'est arrete au plafond, trame tronquee -> echec. */
    if (n > out_cap) return 0;
    return n;
}
