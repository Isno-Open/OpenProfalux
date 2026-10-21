/* io_radio : voir io_radio.h. Config CC1101 io reprise de samr037/iohc-flipper (MIT). */
#include "io_radio.h"
#include "cc1101.h"
#include "esp_log.h"

static const char *TAG = "io_radio";

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
    (void)buf; (void)len;
    /* A CABLER : ecriture TXFIFO (acces burst) + strobe STX, via des primitives FIFO a exposer
     * dans cc1101.c. Reference : iohc-flipper radio_cc1101.c (MIT). */
    ESP_LOGW(TAG, "io_radio_tx_packet : FIFO pas encore cablee");
    return -1;
}

int io_radio_rx_packet(uint8_t *buf, size_t max, uint32_t timeout_ms) {
    (void)buf; (void)max; (void)timeout_ms;
    /* A CABLER : strobe SRX, attente de GDO0 (sync word), lecture RXFIFO. Reference : phy_rx.c (MIT). */
    ESP_LOGW(TAG, "io_radio_rx_packet : FIFO pas encore cablee");
    return -1;
}
