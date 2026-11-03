#pragma once
/* Desassemblage des trames io-homecontrol recues (couche liaison UART sur l'air).
 * Repris de samr037/iohc-flipper (iohc/unframer.h, licence MIT). Chaque octet io est
 * transmis encapsule UART (10 bits : start=0, 8 bits de donnee LSB-first, stop=1). Apres
 * la detection du sync 0x57FD par le CC1101, on jette les 8 bits de residu du 0x33 encode,
 * puis on regroupe par 10 bits et on reconstruit les octets io. La longueur vient du 1er
 * octet io (MsgLen = bits 4..0 ; trame totale = 1 + MsgLen + 2 pour le CRC). Unite pure. */
#include <stdint.h>
#include <stdbool.h>

#define IOHC_FRAME_MAX_LEN 32

typedef enum {
    IohcUnframerOk,            /* octet ajoute */
    IohcUnframerNeedBits,      /* pas assez de bits, aucun octet produit */
    IohcUnframerFrameDone,     /* fin de trame atteinte (pilotee par la longueur) */
    IohcUnframerFramingError,  /* bit start/stop invalide, trame abandonnee */
    IohcUnframerOverflow,      /* trame plus longue que IOHC_FRAME_MAX_LEN */
} IohcUnframerResult;

typedef struct {
    uint32_t bit_buffer;   /* zone de montage, MSB d'abord */
    uint8_t bit_count;
    uint8_t skip_bits;     /* queue du 0x33 encode a jeter apres le sync (8) */
    uint8_t expected_len;  /* 0 jusqu'au Ctrl B1 lu, puis nb d'octets io (CRC compris) */
    uint8_t frame_pos;
    uint8_t frame[IOHC_FRAME_MAX_LEN];
} IohcUnframer;

void iohc_unframer_reset(IohcUnframer *u);
IohcUnframerResult iohc_unframer_feed_byte(IohcUnframer *u, uint8_t fifo_byte);
