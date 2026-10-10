/*
 * CC1101 driver — TX + RX for Profalux 868.35 MHz OOK
 *
 * Provides:
 *   - Init SPI + register config for OOK 868.35 MHz ~1550 baud
 *   - TX single frame (=synthesize OOK signal via GDO0 bit-banging)
 *   - RX capture — invokes callback for each detected frame
 */
#ifndef CC1101_H
#define CC1101_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*cc1101_rx_cb_t)(const uint8_t *frame, size_t bits, int8_t rssi_dbm);

int  cc1101_init(void);

/* Transmit one frame using OOK modulation (=synthesize preamble + header + data) */
int  cc1101_tx_ook_frame(const uint8_t *frame, size_t bits);

/* Enter async RX mode. Callback invoked from ISR-friendly task */
int  cc1101_rx_start(cc1101_rx_cb_t cb);
int  cc1101_rx_stop(void);

/* Auto-test emission : renvoie 0 si la puce passe bien en TX (MARCSTATE 0x13). */
int  cc1101_tx_selftest(void);
/* Frequence porteuse en kHz (profil radio, voir radio_profile.h). 0=OK, -1=non prise. */
int      cc1101_set_freq_khz(uint32_t khz);
uint32_t cc1101_get_freq_khz(void);
int  cc1101_rx_probe(void);   /* sonde de bruit RX au boot (RSSI + nb d'evenements RMT a vide) */

/* Auto-capture : initialise le RMT sur GDO0 pour relire notre propre emission. */
int  cc1101_capture_init(void);
/* Emet frame + capture GDO0 + decode en bits (ordre du fil). Retourne nbits (<0=err). */
int  cc1101_tx_and_capture_bits(const uint8_t *frame, size_t bits, char *out_bits, int max_bits);
/* Ecoute en RX pendant timeout_ms, decode la 1re trame recue. Retourne nbits (<0=rien). */
int  cc1101_rx_listen_bits(uint32_t timeout_ms, char *out_bits, int max_bits);
/* Rejoue (TX) une trame brute (chaine '0'/'1' en ordre du fil) en OOK HCS30x. */
int  cc1101_tx_raw_bits(const char *bits, int n, int repeats);

/* Utils */
extern int g_tx_marc;   /* MARCSTATE lu apres le dernier STX (0x13=TX). Diagnostic. */
int8_t cc1101_get_rssi(void);
void cc1101_set_rx_debug(bool on);   /* switch DEBUG : logge chaque capture RX (symboles+RSSI+bits+classif) */
bool cc1101_get_rx_debug(void);
void cc1101_set_rx_gain(uint8_t agcctrl2);   /* plafond de gain RX (AGCCTRL2) reglable ; defaut 0x27 */
uint8_t cc1101_get_rx_gain(void);
void cc1101_set_tx_te(uint32_t te_us);       /* TE d'emission (us) reglable ; defaut 455 (Profalux) */
uint32_t cc1101_get_tx_te(void);
int cc1101_last_te(void);                    /* TE (us) mesure sur la derniere trame decodee */
void cc1101_get_diag(int *tx_ok, uint8_t *partnum, uint8_t *version); /* diag UI : puce + self-test TX */
uint8_t cc1101_read_reg(uint8_t addr);
void   cc1101_write_reg(uint8_t addr, uint8_t val);

#endif
