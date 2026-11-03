/* io_radio : voir io_radio.h. Config CC1101 io reprise de samr037/iohc-flipper (MIT). */
#include "io_radio.h"
#include "cc1101.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "io_unframer.h"
#include "io_framer.h"

static const char *TAG = "io_radio";

/* Strobes et registres CC1101 en mode paquet (adresses standard). Les registres d'etat se
 * lisent avec le bit burst (via cc1101_read_reg(addr | 0x40)). */
#define CC_SIDLE        0x36
#define CC_STX          0x35
#define CC_SRX          0x34
#define CC_SFRX         0x3A
#define CC_SFTX         0x3B
#define CC_TXFIFO       0x3F
#define CC_RXFIFO       0x3F
#define CC_REG_PKTLEN   0x06
#define CC_STAT_MARC    0x35   /* MARCSTATE */
#define CC_STAT_RXBYTES 0x3B   /* RXBYTES (7 bits bas = octets en FIFO) */
#define MARC_IDLE       0x01

/* Table de registres CC1101 pour io-homecontrol, d'apres iohc-flipper radio_cc1101_regs.h (MIT) :
 * 2-FSK, DRATE ~38 383 bps, deviation ~19 kHz, sync 16 bits 0x57FD (queue de preambule + 0xFF
 * encode UART), FIFO longueur fixe, sans whitening ni CRC materiel. Adresses CC1101 standard.
 * FREQ2/1/0 sont posees par io_radio_set_channel (via cc1101_set_freq_khz). */
typedef struct { uint8_t addr, val; } io_reg_t;
static const io_reg_t IO_REGS[] = {
    {0x00, 0x2E},  /* IOCFG2 : GDO2 desactive */
    {0x02, 0x06},  /* IOCFG0 : GDO0 = sync word recu/emis */
    {0x03, 0x07},  /* FIFOTHR */
    {0x04, 0x57},  /* SYNC1 */
    {0x05, 0xFD},  /* SYNC0 */
    {0x06, 0xFF},  /* PKTLEN */
    {0x07, 0x00},  /* PKTCTRL1 : pas de filtre d'adresse, pas de status */
    {0x08, 0x00},  /* PKTCTRL0 : FIFO, pas de whitening, pas de CRC materiel, longueur fixe */
    {0x09, 0x00},  /* ADDR */
    {0x0A, 0x00},  /* CHANNR */
    {0x0B, 0x06},  /* FSCTRL1 */
    {0x0C, 0x00},  /* FSCTRL0 */
    {0x10, 0x6A},  /* MDMCFG4 : BW 270 kHz, DRATE_E */
    {0x11, 0x83},  /* MDMCFG3 : DRATE_M -> ~38 383 bps */
    {0x12, 0x06},  /* MDMCFG2 : 2-FSK, pas de Manchester, sync 16/16 + carrier sense */
    {0x13, 0x22},  /* MDMCFG1 */
    {0x14, 0xF8},  /* MDMCFG0 */
    {0x15, 0x34},  /* DEVIATN : ~19 kHz */
    {0x16, 0x07},  /* MCSM2 */
    {0x17, 0x00},  /* MCSM1 : toujours TX (ignore RSSI), RX/TX -> IDLE */
    {0x18, 0x18},  /* MCSM0 : calibration IDLE -> RX/TX */
    {0x19, 0x16},  /* FOCCFG */
    {0x1A, 0x6C},  /* BSCFG */
    {0x1B, 0x43},  /* AGCCTRL2 */
    {0x1C, 0x40},  /* AGCCTRL1 */
    {0x1D, 0x91},  /* AGCCTRL0 */
    {0x21, 0x56},  /* FREND1 */
    {0x22, 0x10},  /* FREND0 */
    {0x23, 0xE9},  /* FSCAL3 */
    {0x24, 0x2A},  /* FSCAL2 */
    {0x25, 0x00},  /* FSCAL1 */
    {0x26, 0x1F},  /* FSCAL0 */
    {0x2C, 0x81},  /* TEST2 */
    {0x2D, 0x35},  /* TEST1 */
    {0x2E, 0x09},  /* TEST0 */
};

static const uint32_t IO_CH_KHZ[3] = { 868250u, 868950u, 869850u };

void io_radio_set_channel(io_channel_t ch) {
    if (ch < IO_CH1 || ch > IO_CH3) ch = IO_CH2;
    cc1101_set_freq_khz(IO_CH_KHZ[ch]);
}

void io_radio_config(void) {
    for (size_t i = 0; i < sizeof(IO_REGS) / sizeof(IO_REGS[0]); i++)
        cc1101_write_reg(IO_REGS[i].addr, IO_REGS[i].val);
    cc1101_write_reg(0x3E, 0xC0);   /* PATABLE[0] : ~+10 dBm, puissance d'emission (TX) */
    io_radio_set_channel(IO_CH2);
    ESP_LOGW(TAG, "config CC1101 io posee (2-FSK 868 MHz, sync 0x57FD, CH2)");
}

int io_radio_tx_packet(const uint8_t *buf, size_t len) {
    if (!buf || len == 0 || len > 60) return -1;     /* trame io encapsulee ~40 o, < taille FIFO 64 */
    cc1101_strobe_cmd(CC_SIDLE);
    cc1101_strobe_cmd(CC_SFTX);                       /* vide la TXFIFO */
    cc1101_write_reg(CC_REG_PKTLEN, (uint8_t)len);    /* longueur fixe = ce paquet */
    for (size_t i = 0; i < len; i++)                  /* FIFO un octet a la fois : fiable au-dela du burst */
        cc1101_write_reg(CC_TXFIFO, buf[i]);
    cc1101_strobe_cmd(CC_STX);
    for (int i = 0; i < 200; i++) {                   /* MCSM1 TXOFF=IDLE : retour IDLE = paquet parti */
        if ((cc1101_read_reg(CC_STAT_MARC | 0x40) & 0x1F) == MARC_IDLE) return 0;
        esp_rom_delay_us(500);
    }
    cc1101_strobe_cmd(CC_SIDLE);
    return -1;
}

/* Recoit UNE trame io et la rend DESASSEMBLEE (octets io, pas les octets FIFO encapsules UART).
 * Le CC1101 n'ecrit dans la RXFIFO qu'apres detection du sync 0x57FD (donc aligne sur l'octet) ;
 * on vide la FIFO par morceaux (burst borne a 31) et on alimente l'unframer, qui termine la trame
 * a la longueur lue dans l'en-tete io. Modele repris de iohc-flipper phy_rx.c (MIT). Reste a
 * affiner au banc : erratum "ne pas lire le dernier octet FIFO en cours de reception". */
int io_radio_rx_packet(uint8_t *buf, size_t max, uint32_t timeout_ms) {
    if (!buf || max == 0) return -1;
    cc1101_strobe_cmd(CC_SIDLE);
    cc1101_strobe_cmd(CC_SFRX);
    cc1101_strobe_cmd(CC_SRX);
    IohcUnframer u;
    iohc_unframer_reset(&u);
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < end) {
        uint8_t avail = cc1101_read_reg(CC_STAT_RXBYTES | 0x40) & 0x7F;
        if (avail == 0) { esp_rom_delay_us(1000); continue; }
        uint8_t n = avail > 31 ? 31 : avail;   /* burst borne a 31 (max_transfer_sz) */
        uint8_t fifo[31];
        cc1101_read_burst(CC_RXFIFO, fifo, n);
        for (uint8_t i = 0; i < n; i++) {
            IohcUnframerResult r = iohc_unframer_feed_byte(&u, fifo[i]);
            if (r == IohcUnframerFrameDone) {
                size_t len = u.frame_pos < max ? u.frame_pos : max;
                for (size_t k = 0; k < len; k++) buf[k] = u.frame[k];
                cc1101_strobe_cmd(CC_SIDLE);
                return (int)len;
            }
            if (r == IohcUnframerFramingError || r == IohcUnframerOverflow) {
                iohc_unframer_reset(&u);            /* bruit ou desync : on repart propre */
                cc1101_strobe_cmd(CC_SIDLE);
                cc1101_strobe_cmd(CC_SFRX);
                cc1101_strobe_cmd(CC_SRX);
                break;                              /* relire RXBYTES apres re-armement */
            }
        }
    }
    cc1101_strobe_cmd(CC_SIDLE);
    return 0;   /* rien recu dans le delai */
}

int io_radio_tx_frame(const uint8_t *io_bytes, uint8_t n) {
    char hex[2 * 32 + 1];
    uint8_t m = n > 32 ? 32 : n;
    for (uint8_t i = 0; i < m; i++) snprintf(hex + 2 * i, 3, "%02X", io_bytes[i]);
    hex[2 * m] = '\0';
    ESP_LOGW(TAG, "Trame io emise (%u octets) : %s", (unsigned)n, hex);

    uint8_t fifo[64];
    size_t k = io_frame_to_fifo(io_bytes, n, fifo, sizeof fifo);
    if (k == 0 || k > 60) {
        ESP_LOGE(TAG, "encapsulation refusee (%u octets io)", (unsigned)n);
        return -1;
    }
    return io_radio_tx_packet(fifo, k);
}
