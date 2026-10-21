#pragma once
/* Encapsulation UART des trames io pour l'EMISSION (pendant de io_unframer). Repris de
 * samr037/iohc-flipper (iohc/frame_build.c, MIT). Chaque octet io devient 10 bits (start 0,
 * 8 bits de donnee LSB-first, stop 1), precede d'un octet de residu 0x99 (apres le sync
 * 0x57FD que le CC1101 ajoute), bits empaquetes MSB-first, dernier octet complete a 1. Pur. */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encode `total` octets io (la trame) en octets FIFO CC1101, dans out (capacite out_cap).
 * Rend le nombre d'octets FIFO ecrits, 0 si out_cap est trop petit. */
size_t io_frame_to_fifo(const uint8_t *frame, uint8_t total, uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
