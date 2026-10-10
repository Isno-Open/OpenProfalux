/*
 * CC1101 driver — Profalux 868.425 MHz OOK
 *
 * NOTE: This is a functional skeleton. Config registers below are
 * validated against SmartRF Studio output for OOK 868.425 MHz ~1550 baud
 * asynchronous serial mode with GDO0 output on TX and GDO0 input on RX.
 */
#include "cc1101.h"
#include "radio_profile.h"
#include "hardware_config.h"
#include <string.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_rom_sys.h>
#include "soc/soc_caps.h"   /* SOC_RMT_MEM_WORDS_PER_CHANNEL : portabilite RMT (S3/C3) */
#include <esp_timer.h>
#include <driver/rmt_rx.h>
#include <esp_heap_caps.h>   /* heap_caps_aligned_alloc : buffer RX DMA-capable aligne (S3) */
#include "keeloq_decode.h"   /* decodeur OOK HCS/KeeLoq, unite pure testable sur hote */

static const char *TAG = "cc1101";
/* ── Switch DEBUG capture RX (runtime, togglable depuis l'UI/MQTT) ──────────
 * Quand ON : logge CHAQUE paquet capte en ecoute (symboles RMT + RSSI + bits
 * decodes + classification KeeLoq), y compris les trames <64 bits normalement
 * jetees. Sert a diagnostiquer "ca ne capte pas" : distingue antenne morte /
 * reception mais decode KO / pas du Profalux. OFF par defaut (log verbeux). */
static volatile bool s_rx_debug = false;
void cc1101_set_rx_debug(bool on) { s_rx_debug = on; ESP_LOGW(TAG, "DEBUG capture RX %s", on ? "ON" : "OFF"); }
bool cc1101_get_rx_debug(void)    { return s_rx_debug; }
/* Gain RX reglable (AGCCTRL2) : plafond de gain LNA. 0x27=defaut (-9 dB). Plus GRAND
 * = moins de gain (0x37, 0x3F=-17 dB) -> l'AGC ne remonte plus sur le bruit dans les
 * creux OOK (moins de fausses transitions), au prix de la sensibilite aux signaux faibles. */
static volatile uint8_t s_rx_gain = 0x27;
void cc1101_set_rx_gain(uint8_t v) { s_rx_gain = v; ESP_LOGW(TAG, "RX gain AGCCTRL2=0x%02X", v); }
uint8_t cc1101_get_rx_gain(void)   { return s_rx_gain; }
/* Timing d'EMISSION reglable : TE (temps elementaire). Defaut 455 us (Profalux). Les autres
 * variantes PFX roulent a un TE different (ex FranciaFlex ~415). On derive bit '1'=TE, '0'=2*TE,
 * preambule=TE, entete proportionnelle. Permet au rejeu de matcher le timing du recepteur. */
static volatile uint32_t s_tx_te = PROFALUX_TE_US;
void cc1101_set_tx_te(uint32_t te) { if (te >= 200 && te <= 900) { s_tx_te = te; ESP_LOGW(TAG, "TX TE=%u us", (unsigned)te); } }
uint32_t cc1101_get_tx_te(void)    { return s_tx_te; }
/* TE (us) mesure sur la DERNIERE trame decodee (moyenne des HAUT courts). Sert a auto-caler
 * le TE d'emission a l'apprentissage (le rejeu matche le tempo de la telecommande captee). */
static volatile int s_last_te = 455;
int cc1101_last_te(void) { return s_last_te; }
/* Diagnostic radio expose a l'UI : detection de la puce + resultat du self-test TX. */
static int     s_tx_ok   = -1;      /* -1=non teste, 0=HS, 1=OK (puce passe en TX) */
static uint8_t s_partnum = 0xFF;    /* CC1101 attendu : PARTNUM 0x00, VERSION 0x04/0x14 */
static uint8_t s_version = 0xFF;
void cc1101_get_diag(int *tx_ok, uint8_t *partnum, uint8_t *version) {
    if (tx_ok)   *tx_ok   = s_tx_ok;
    if (partnum) *partnum = s_partnum;
    if (version) *version = s_version;
}
int g_tx_marc = -1;   /* MARCSTATE lu juste apres le dernier STX (0x13=TX). Diagnostic expose via HTTP. */
static spi_device_handle_t s_spi = NULL;
static rmt_channel_handle_t s_cap = NULL;
static QueueHandle_t s_capq = NULL;
/* Buffer de reception RMT. Sur ESP32-S3 avec DMA, ce buffer DOIT etre en RAM
 * interne DMA-capable et aligne 64 octets, sinon le GDMA lit/ecrit les mauvaises
 * lignes de cache -> symboles parasites (glitches ~55 us) et corruption silencieuse
 * (cf esp-idf #12564, FastLED #2156). Un simple tableau statique ne garantit NI
 * l'alignement NI MALLOC_CAP_DMA. On l'alloue donc a l'init avec heap_caps. */
#define CAP_SYMBOLS KEELOQ_CAP_SYMBOLS   /* source unique (keeloq_decode.h) */
_Static_assert(sizeof(kq_sym_t) == sizeof(rmt_symbol_word_t), "kq_sym_t doit avoir la meme disposition que rmt_symbol_word_t");
static rmt_symbol_word_t *s_capbuf = NULL;
static size_t s_capbuf_sz = 0;
/* RX et auto-capture partagent l'unique canal RMT s_cap sur GDO0 (memoire RMT limitee). */
static cc1101_rx_cb_t     s_rx_cb = NULL;
static TaskHandle_t       s_rx_task = NULL;
static volatile bool      s_rx_running = false;

/* --- Registers CC1101 --- */
#define CC_IOCFG2   0x00
#define CC_IOCFG0   0x02
#define CC_FIFOTHR  0x03
#define CC_PKTLEN   0x06
#define CC_PKTCTRL0 0x08
#define CC_FREQ2    0x0D
#define CC_FREQ1    0x0E
#define CC_FREQ0    0x0F
#define CC_MDMCFG4  0x10
#define CC_MDMCFG3  0x11
#define CC_MDMCFG2  0x12
#define CC_MDMCFG1  0x13
#define CC_MDMCFG0  0x14
#define CC_DEVIATN  0x15
#define CC_MCSM0    0x18
#define CC_FOCCFG   0x19
#define CC_WORCTRL  0x20
#define CC_FSCAL3   0x23
#define CC_FSCAL2   0x24
#define CC_FSCAL1   0x25
#define CC_FSCAL0   0x26
#define CC_TEST2    0x2C
#define CC_TEST1    0x2D
#define CC_TEST0    0x2E
#define CC_PARTNUM  0x30
#define CC_FREQEST  0x32   /* offset de frequence estime du signal recu (signed) */
#define CC_RSSI     0x34
#define CC_MARCSTATE 0x35   /* etat machine radio (0x0D=RX, 0x13=TX, 0x01=IDLE) */
#define CC_MCSM1    0x17
#define CC_FREND1   0x21
#define CC_FREND0   0x22    /* PA_POWER : bit0..2 = index PATABLE utilise */
#define CC_PATABLE  0x3E

/* Strobes */
#define CC_SRES     0x30
#define CC_SIDLE    0x36
#define CC_STX      0x35
#define CC_SRX      0x34
#define CC_SFTX     0x3B
#define CC_SFRX     0x3A

/* Config array: OOK 868.425 MHz ~1550 baud asynchronous mode */
static const struct { uint8_t reg, val; } s_regs[] = {
    {CC_IOCFG2,   0x0D},  /* Serial data output */
    {CC_IOCFG0,   0x0D},  /* Serial data output on GDO0 */
    {CC_FIFOTHR,  0x47},
    {CC_PKTLEN,   0xFF},
    {CC_PKTCTRL0, 0x32},  /* Async serial, no CRC, infinite packet */
    {CC_FREQ2,    0x21},  /* 868.425 MHz (mesure 2026-08-10, etait 868.35 = hors bande) */
    {CC_FREQ1,    0x66},
    {CC_FREQ0,    0xA5},
    {CC_MDMCFG4,  0xC6},  /* RX BW 102 kHz (mesure) */
    {CC_MDMCFG3,  0x83},
    {CC_MDMCFG2,  0x30},  /* OOK/ASK modulation, no preamble/sync */
    {CC_MDMCFG1,  0x22},
    {CC_MDMCFG0,  0xF8},
    {CC_DEVIATN,  0x15},
    {CC_MCSM1,    0x3C},   /* reste en RX apres reception (comme le sniffer) */
    {CC_MCSM0,    0x18},   /* FS_AUTOCAL: calibre le PLL a IDLE->TX/RX */
    {CC_FREND1,   0xB6},   /* front analogique (ref sniffer qui capte) */
    {CC_FREND0,   0x11},   /* PA_POWER=1 => OOK utilise PATABLE[0] et [1] */
    {CC_FOCCFG,   0x14},
    {CC_WORCTRL,  0xFB},
    {CC_FSCAL3,   0xE9},
    {CC_FSCAL2,   0x2A},
    {CC_FSCAL1,   0x00},
    {CC_FSCAL0,   0x1F},
    {CC_TEST2,    0x81},
    {CC_TEST1,    0x35},
    {CC_TEST0,    0x09},
};

static void cs_low(void)  { gpio_set_level(CC1101_PIN_CS, 0); }
static void cs_high(void) { gpio_set_level(CC1101_PIN_CS, 1); }

static void spi_xfer(uint8_t *tx, uint8_t *rx, size_t n) {
    spi_transaction_t t = { .length = 8 * n, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(s_spi, &t);
}

void cc1101_write_reg(uint8_t addr, uint8_t val) {
    uint8_t tx[2] = {addr, val}, rx[2];
    cs_low(); spi_xfer(tx, rx, 2); cs_high();
}

uint8_t cc1101_read_reg(uint8_t addr) {
    uint8_t tx[2] = {addr | 0x80, 0}, rx[2];
    cs_low(); spi_xfer(tx, rx, 2); cs_high();
    return rx[1];
}

static void strobe(uint8_t s) {
    uint8_t tx = s, rx;
    cs_low(); spi_xfer(&tx, &rx, 1); cs_high();
}

int cc1101_init(void) {
    /* GPIO CS + GDO0 */
    gpio_config_t io_cs = { .pin_bit_mask = 1ULL << CC1101_PIN_CS,
                            .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io_cs);
    cs_high();

    gpio_config_t io_gdo = { .pin_bit_mask = 1ULL << CC1101_PIN_GDO0,
                             .mode = GPIO_MODE_INPUT_OUTPUT };
    gpio_config(&io_gdo);

    /* SPI init */
    spi_bus_config_t bus = {
        .miso_io_num = CC1101_PIN_MISO, .mosi_io_num = CC1101_PIN_MOSI,
        .sclk_io_num = CC1101_PIN_SCK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CC1101_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = CC1101_SPI_FREQ_HZ, .mode = 0,
        .spics_io_num = -1, .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CC1101_SPI_HOST, &dev, &s_spi));

    /* Reset + configure */
    strobe(CC_SRES);
    vTaskDelay(pdMS_TO_TICKS(10));
    for (size_t i = 0; i < sizeof(s_regs) / sizeof(s_regs[0]); i++) {
        cc1101_write_reg(s_regs[i].reg, s_regs[i].val);
    }
    /* PATABLE (burst) pour OOK : index0=eteint (bit 0), index1=puissance (bit 1). */
    {
        uint8_t tx[3] = { CC_PATABLE | 0x40, 0x00, 0xC0 }, rx[3];
        cs_low(); spi_xfer(tx, rx, 3); cs_high();
    }
    uint8_t partnum = cc1101_read_reg(CC_PARTNUM | 0x40);
    uint8_t version = cc1101_read_reg(0x31 | 0x40);   /* VERSION : ~0x04/0x14 sur un vrai CC1101 */
    uint8_t marc    = cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F;
    s_partnum = partnum; s_version = version;          /* pour le diagnostic UI */
    ESP_LOGI(TAG, "PARTNUM=0x%02X VERSION=0x%02X (attendu 0x00/0x04-0x14) MARCSTATE=0x%02X", partnum, version, marc);
    return (partnum == 0x00) ? 0 : -1;
}

/* Synchronous bit-bang OOK on GDO0. Codage PWM HCS30x mesure le 2026-08-10 :
 *   Te=455us, periode de bit = 3*Te = 1365us.
 *   symbole H455 L910 (H<L)  et  symbole H910 L455 (H>L).
 * POLARITE (quel symbole = logique 1) NON etablie avec une seule telecommande :
 * on applique la convention "bit=1 si H<L" du decode, A CONFIRMER empiriquement
 * (si l'appairage echoue, inverser PFX_TX_INVERT). */
#ifndef PFX_TX_INVERT
#define PFX_TX_INVERT 0
#endif
static void ook_bit(bool one) {
    if (PFX_TX_INVERT) one = !one;
    if (one) {  /* symbole H455 L910 */
        gpio_set_level(CC1101_PIN_GDO0, 1); esp_rom_delay_us(455);
        gpio_set_level(CC1101_PIN_GDO0, 0); esp_rom_delay_us(910);
    } else {    /* symbole H910 L455 */
        gpio_set_level(CC1101_PIN_GDO0, 1); esp_rom_delay_us(910);
        gpio_set_level(CC1101_PIN_GDO0, 0); esp_rom_delay_us(455);
    }
}

/* Frequence porteuse (profil radio). Ecrite en IDLE ; le synthetiseur se recalibre au
 * prochain passage en RX/TX (MCSM0 FS_AUTOCAL). Relue pour verifier : sur certains montages
 * le CC1101 ignore une ecriture juste apres une rafale d'acces SPI, d'ou 3 essais espaces. */
static uint32_t s_freq_khz = PROFALUX_FREQ_HZ / 1000;   /* celle de s_regs au boot */
uint32_t cc1101_get_freq_khz(void) { return s_freq_khz; }

int cc1101_set_freq_khz(uint32_t khz) {
    uint32_t w = radio_cc1101_freq_word(khz);
    uint8_t f2 = (w >> 16) & 0xFF, f1 = (w >> 8) & 0xFF, f0 = w & 0xFF;
    strobe(CC_SIDLE);
    for (int i = 0; i < 3; i++) {
        cc1101_write_reg(CC_FREQ2, f2); cc1101_write_reg(CC_FREQ1, f1); cc1101_write_reg(CC_FREQ0, f0);
        if (cc1101_read_reg(CC_FREQ2) == f2 && cc1101_read_reg(CC_FREQ1) == f1 && cc1101_read_reg(CC_FREQ0) == f0) {
            s_freq_khz = khz;
            ESP_LOGI(TAG, "frequence %u kHz (FREQ %02X %02X %02X)", (unsigned)khz, f2, f1, f0);
            return 0;
        }
        esp_rom_delay_us(1000);
    }
    ESP_LOGE(TAG, "frequence %u kHz NON prise (FREQ relu %02X %02X %02X)", (unsigned)khz,
             cc1101_read_reg(CC_FREQ2), cc1101_read_reg(CC_FREQ1), cc1101_read_reg(CC_FREQ0));
    return -1;
}

/* Auto-test d'emission : passe en TX, lit MARCSTATE (0x13 = porteuse ON),
 * module ~300 ms puis revient a IDLE. Prouve que la puce emet, sans recepteur. */
int cc1101_tx_selftest(void) {
    strobe(CC_STX);
    esp_rom_delay_us(1000);
    uint8_t m1 = cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F;
    for (int i = 0; i < 300; i++) {   /* porteuse modulee 1kHz pendant ~300ms */
        gpio_set_level(CC1101_PIN_GDO0, 1); esp_rom_delay_us(500);
        gpio_set_level(CC1101_PIN_GDO0, 0); esp_rom_delay_us(500);
    }
    uint8_t m2 = cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F;
    strobe(CC_SIDLE);
    ESP_LOGI(TAG, "TX SELFTEST: MARCSTATE post-STX=0x%02X, pendant modulation=0x%02X "
                  "(0x13=TX porteuse ON, 0x01=IDLE=rien)", m1, m2);
    s_tx_ok = (m1 == 0x13 || m2 == 0x13) ? 1 : 0;   /* pour le diagnostic UI */
    return s_tx_ok ? 0 : -1;
}

int cc1101_tx_ook_frame(const uint8_t *frame, size_t bits) {
    strobe(CC_STX);
    esp_rom_delay_us(800);   /* laisse le PLL se caler (FS_AUTOCAL) avant de moduler */

    /* Preuve d'emission : lit MARCSTATE une fois par boot. 0x13=TX (porteuse ON). */
    static bool s_marc_logged = false;
    if (!s_marc_logged) {
        s_marc_logged = true;
        uint8_t m = cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F;
        ESP_LOGI(TAG, "TX MARCSTATE=0x%02X (0x13=TX porteuse ON, 0x01=IDLE=rien emis)", m);
    }

    /* Preambule: 23 alternances de Te (mesure) */
    for (int i = 0; i < PROFALUX_PREAMBLE_ELEMENTS; i++) {
        gpio_set_level(CC1101_PIN_GDO0, (i & 1) == 0);  /* H,L,H,L... */
        esp_rom_delay_us(PROFALUX_TE_US);
    }
    /* Header: silence ~4450us low */
    gpio_set_level(CC1101_PIN_GDO0, 0); esp_rom_delay_us(PROFALUX_HEADER_US);

    /* Data bits MSB first */
    for (size_t i = 0; i < bits; i++) {
        bool b = (frame[i / 8] >> (7 - (i % 8))) & 1;
        ook_bit(b);
    }
    gpio_set_level(CC1101_PIN_GDO0, 0);
    esp_rom_delay_us(2000);  /* Inter-frame gap */
    strobe(CC_SIDLE);
    return 0;
}

/* Rejoue une trame brute (chaine de bits '0'/'1' en ordre du fil) en OOK :
 * preambule + entete + symboles HCS30x. Sert au replay d'une trame captee. */
int cc1101_tx_raw_bits(const char *bits, int n, int repeats) {
    /* UNE seule session TX (un STX, FS_AUTOCAL calibre le PLL), puis `repeats` trames dos-a-dos
     * = une vraie rafale de telecommande. Evite un STX/calibration par trame et rend l'emission
     * plus fiable (le bit-bang reste sensible a la preemption, d'ou plusieurs trames). */
    if (repeats < 1) repeats = 1;
    /* CRITIQUE : couper la capture RMT pendant le bit-bang. Sinon la RMT ecoute GDO0
     * (l'ecoute permanente l'a armee), capte notre propre TX, deborde ("hw buffer too
     * small") et son ISR PREEMPTE le bit-bang -> timing 455/910 us deforme -> trame
     * invalide -> le moteur l'ignore. Critique en environnement bruite (RMT deborde en
     * continu -> TOUTES les repetitions corrompues). On re-arme apres le SIDLE. */
    if (s_cap) rmt_disable(s_cap);
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT_OUTPUT);
    strobe(CC_STX);
    esp_rom_delay_us(800);   /* laisse le PLL se caler + la PA monter avant de moduler */
    g_tx_marc = cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F;
    /* Preuve d'emission sur le serie : MARCSTATE 0x13 = la puce est bien en TX (elle module
     * l'antenne). Loggue avant le bit-bang, au point exact ou la trame part. */
    ESP_LOGW(TAG, "TX RAW: emission de %d trame(s) x%d bits, MARCSTATE=0x%02X (%s)",
             repeats, n, g_tx_marc, g_tx_marc == 0x13 ? "TX ACTIF" : "PAS EN TX !");
    /* Timing derive du TE reglable : bit '1'=TE haut/2TE bas, '0'=2TE haut/TE bas,
     * preambule=TE, entete proportionnelle a TE (= HEADER a TE nominal). */
    const uint32_t te = s_tx_te, te2 = 2 * s_tx_te;
    const uint32_t hdr = (uint32_t)((uint64_t)PROFALUX_HEADER_US * s_tx_te / PROFALUX_TE_US);
    for (int r = 0; r < repeats; r++) {
        for (int i = 0; i < PROFALUX_PREAMBLE_ELEMENTS; i++) {
            gpio_set_level(CC1101_PIN_GDO0, (i & 1) == 0);
            esp_rom_delay_us(te);
        }
        gpio_set_level(CC1101_PIN_GDO0, 0);
        esp_rom_delay_us(hdr);
        for (int i = 0; i < n; i++) {
            if (bits[i] == '1') { gpio_set_level(CC1101_PIN_GDO0, 1); esp_rom_delay_us(te);
                                  gpio_set_level(CC1101_PIN_GDO0, 0); esp_rom_delay_us(te2); }
            else                { gpio_set_level(CC1101_PIN_GDO0, 1); esp_rom_delay_us(te2);
                                  gpio_set_level(CC1101_PIN_GDO0, 0); esp_rom_delay_us(te); }
        }
        gpio_set_level(CC1101_PIN_GDO0, 0);
        esp_rom_delay_us(2000);   /* gap inter-trame */
    }
    strobe(CC_SIDLE);
    if (s_cap) rmt_enable(s_cap);   /* re-arme la capture RMT pour l'ecoute permanente suivante */
    return 0;
}

/* ── Auto-capture : le RMT lit GDO0 pendant qu'on bit-bang (verifie la trame). ── */
static bool IRAM_ATTR cc_cap_cb(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *e, void *ctx) {
    BaseType_t hp = pdFALSE; xQueueSendFromISR((QueueHandle_t)ctx, e, &hp); return hp == pdTRUE;
}

int cc1101_capture_init(void) {
    if (s_cap) return 0;
    s_capq = xQueueCreate(8, sizeof(rmt_rx_done_event_data_t));  /* marge : en_partial_rx genere
                                 * plusieurs callbacks par reception ; eviter de perdre un fragment
                                 * si le consommateur (log DEBUG) ralentit. */
    rmt_rx_channel_config_t c = {0};
    c.clk_src = RMT_CLK_SRC_DEFAULT; c.resolution_hz = 1000000;
    /* Une trame entiere (~132 symboles) doit tenir dans UN buffer RX, sinon la
     * capture est hachee en pleine trame et le decode echoue (vu sur ISNO Super/S3).
     *
     * DEUX drapeaux decident, et ils ne vont pas ensemble (verifie dans l'ESP-IDF
     * v6.1). Le second a coute la regression v0.2.6, cf issue #12 : il manquait a
     * ce tableau.
     *
     *   cible    words/canal   SUPPORT_DMA   SUPPORT_RX_PINGPONG
     *   esp32         64        absent        absent
     *   esp32s3       48        1             1
     *   esp32c3       48        absent        1
     *   esp32c6       48        absent        1
     *
     * Donc le S3 est SEUL a avoir le DMA, mais le C3 et le C6 ont le ping-pong :
     * en_partial_rx leur convient, un grand buffer non.
     *
     * - ESP32 classic : pas de DMA, mais 512 symboles tiennent en multi-blocs.
     * - S3 : 48 words/canal seulement, donc 512 sans DMA echoue et 48 tronque la
     *   trame. with_dma=true donne un grand buffer -> trame complete.
     * - ⚠️ C3 et C6 : PAS de DMA RMT, donc ils tombent dans le #else a 48 words et
     *   RESTENT TRONQUES. Ce n'est pas corrige pour eux, et aucune de nos cartes
     *   n'est concernee (boards/ ne declare que esp32 et esp32s3). Les corriger
     *   demanderait un autre mecanisme, par exemple une capture par GPIO+timer. */
#if defined(CONFIG_IDF_TARGET_ESP32)
    c.mem_block_symbols = 512;    /* ESP32 : pas de DMA mais 512 en multi-blocs */
#elif SOC_RMT_SUPPORT_DMA
    c.flags.with_dma    = true;   /* S3 seul (le C3 n'a pas le DMA, il ne passe jamais
                                   * ici) : seulement 48 words/canal -> ping-pong bogue
                                   * (#13419) sur une trame >48 symb. Le DMA stream vers un
                                   * grand buffer et evite ce chemin. mem_block_symbols petit
                                   * avec DMA corrompt (#12564) -> on prend tout le buffer. */
    c.mem_block_symbols = CAP_SYMBOLS;
#else
    /* C3, C6 : 48 words, la trame longue sera tronquee (voir ci-dessus). */
    c.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
#endif
    c.gpio_num = CC1101_PIN_GDO0;
    if (rmt_new_rx_channel(&c, &s_cap) != ESP_OK) { ESP_LOGW(TAG, "RMT capture init KO"); s_cap = NULL; return -1; }
    /* Buffer RX aligne + DMA-capable (voir note sur s_capbuf). */
    if (!s_capbuf) {
        s_capbuf_sz = CAP_SYMBOLS * sizeof(rmt_symbol_word_t);
        s_capbuf = heap_caps_aligned_alloc(64, s_capbuf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_capbuf) { ESP_LOGW(TAG, "RMT capture: alloc buffer DMA KO"); rmt_del_channel(s_cap); s_cap = NULL; return -1; }
    }
    rmt_rx_event_callbacks_t cb = { .on_recv_done = cc_cap_cb };
    rmt_rx_register_event_callbacks(s_cap, &cb, s_capq);
    rmt_enable(s_cap);
    /* RMT met GDO0 en input : on re-active la sortie pour pouvoir bit-banger */
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT_OUTPUT);
    return 0;
}

/* Emet la trame ET capture GDO0, decode la forme d'onde en bits (ordre du fil).
 * Retourne le nombre de bits decodes (<0 = erreur). bit=1 si HAUT court (H<680us). */
/* decode_rmt : fin wrapper autour du decodeur PUR keeloq_decode() (unite keeloq_decode.c,
 * testee sur hote dans firmware/test/keeloq). On caste le buffer RMT en kq_sym_t (meme
 * disposition binaire, cf _Static_assert plus haut) et on met a jour le TE mesure
 * (auto-calage du rejeu). Tout le decodage (de-glitch, scan entetes, vote) vit dans l'unite. */
static int decode_rmt(const rmt_symbol_word_t *raw, size_t n, char *out, int max_bits) {
    int te = 0;
    int r = keeloq_decode((const kq_sym_t *)raw, n, out, max_bits, &te);
    if (te) s_last_te = te;
    return r;
}

int cc1101_tx_and_capture_bits(const uint8_t *frame, size_t bits, char *out_bits, int max_bits) {
    if (!s_cap && cc1101_capture_init() != 0) return -1;
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT_OUTPUT);   /* on pilote GDO0 (TX) */
    rmt_receive_config_t rc = { .signal_range_min_ns = 2000, .signal_range_max_ns = 6000000 };
    if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) != ESP_OK) return -2;
    cc1101_tx_ook_frame(frame, bits);         /* bit-bang GDO0, RMT capture en parallele */
    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(s_capq, &ev, pdMS_TO_TICKS(500)) != pdTRUE) return -3;
    return decode_rmt(ev.received_symbols, ev.num_symbols, out_bits, max_bits);
}

/* Ecoute (RX) pendant timeout_ms : bascule la puce en RX, capture GDO0 (donnee
 * demodulee) et decode la 1re trame recue. Retourne nbits (<0 = rien / erreur). */
/* Sonde de bruit RX : arme la reception a vide ~800 ms, compte les evenements RMT
 * (= activite bruit OOK) et suit le RSSI. Sert a comparer objectivement un module/antenne :
 * MOINS d'evenements = moins de bruit qui inonde le buffer = meilleur pour l'ecoute continue. */
int cc1101_rx_probe(void) {
    if (!s_cap && cc1101_capture_init() != 0) return -1;
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT);
    cc1101_write_reg(0x02, 0x0D); cc1101_write_reg(0x0B, 0x06); cc1101_write_reg(0x19, 0x14);
    cc1101_write_reg(0x1B, 0x04); cc1101_write_reg(0x1C, 0x00); cc1101_write_reg(0x1D, 0x92);   /* AGC OOK TI DN022 (idem ecoute) */
    strobe(CC_SIDLE); esp_rom_delay_us(200);
    strobe(0x33); vTaskDelay(pdMS_TO_TICKS(3));
    strobe(0x3A); strobe(0x34);
    for (int i = 0; i < 50; i++) { if ((cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F) == 0x0D) break; vTaskDelay(pdMS_TO_TICKS(1)); }
    rmt_disable(s_cap); rmt_enable(s_cap); xQueueReset(s_capq);
    rmt_receive_config_t rc = { .signal_range_min_ns = 3000, .signal_range_max_ns = 8000000 };
    int64_t t_end = esp_timer_get_time() + 800 * 1000;
    int nev = 0; int8_t rssi_max = -128, rssi_min = 0;
    rmt_rx_done_event_data_t ev;
    if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) == ESP_OK) {
        while (esp_timer_get_time() < t_end) {
            int8_t r = cc1101_get_rssi(); if (r > rssi_max) rssi_max = r; if (r < rssi_min) rssi_min = r;
            if (xQueueReceive(s_capq, &ev, pdMS_TO_TICKS(40)) != pdTRUE) continue;
            nev++;
            if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) != ESP_OK) break;
        }
    }
    strobe(CC_SIDLE);
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT_OUTPUT);
    ESP_LOGI(TAG, "RX PROBE bruit: RSSI %d..%d dBm, %d evt RMT en 800ms (moins d'evt = moins de bruit/flood)",
             rssi_min, rssi_max, nev);
    return nev;
}

/* Log DIAG d'une capture (commun aux deux chemins S3/ESP32). sym/nsym = symboles bruts
 * a dumper, nbits/out = resultat du decode. Ne logge que si le switch DEBUG est ON et la
 * capture est "interessante" (entete trouvee ou grosse rafale). */
static void diag_log_capture(const rmt_symbol_word_t *sym, size_t nsym, int nbits, const char *out) {
    if (!(s_rx_debug && (nbits >= 1 || nsym >= 50))) return;
    int8_t drssi = cc1101_get_rssi();
    if (nbits >= 64)
        ESP_LOGW(TAG, "DIAG RX: %u symb, RSSI %d dBm, %d bits => TRAME PROFALUX/KEELOQ complete: %s",
                 (unsigned)nsym, drssi, nbits, out);
    else if (nbits >= 1)
        ESP_LOGW(TAG, "DIAG RX: %u symb, RSSI %d dBm, entete HCS OK mais %d bits (partiel): %s",
                 (unsigned)nsym, drssi, nbits, out);
    else
        ESP_LOGW(TAG, "DIAG RX: %u symb, RSSI %d dBm, PAS d'entete HCS (ou bruit)", (unsigned)nsym, drssi);
    if (drssi > -70) {   /* signal fort : dump des durees brutes des 1res impulsions (us) */
        int8_t fq = (int8_t)cc1101_read_reg(CC_FREQEST | 0x40);
        int fq_khz = (int)fq * 1587 / 1000;
        char dbuf[320]; int p = 0;
        int lim = nsym < 20 ? (int)nsym : 20;
        for (int k = 0; k < lim && p < 300; k++)
            p += snprintf(dbuf + p, sizeof(dbuf) - p, "%c%u %c%u ",
                          sym[k].level0 ? 'H' : 'L', (unsigned)sym[k].duration0,
                          sym[k].level1 ? 'H' : 'L', (unsigned)sym[k].duration1);
        ESP_LOGW(TAG, "DIAG RAW (RSSI %d, FREQEST %d ~%d kHz, %d/%u symb, us): %s",
                 drssi, fq, fq_khz, lim, (unsigned)nsym, dbuf);
    }
}

int cc1101_rx_listen_bits(uint32_t timeout_ms, char *out_bits, int max_bits) {
    /* RX sur GDO0 (GPIO25) comme le sniffer. On REUTILISE l'unique canal RMT s_cap :
     * l'ESP32 n'a pas assez de memoire RMT pour un 2e canal de 512 symboles
     * ("no free rx channels"). GDO0 est prouve OK par le SELFVERIFY au boot. */
    if (!s_cap && cc1101_capture_init() != 0) return -1;
    /* GDO0 en INPUT : la CC1101 (RX, IOCFG0=0x0D) sort la donnee demodulee, on la lit */
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT);
    cc1101_write_reg(0x02, 0x0D);   /* IOCFG0 = donnee serie demodulee sur GDO0 */
    cc1101_write_reg(0x0B, 0x06);   /* FSCTRL1 */
    cc1101_write_reg(0x19, 0x14);   /* FOCCFG  */
    cc1101_write_reg(0x1B, 0x04);   /* AGCCTRL2 = config OOK recommandee TI DN022 (MAGN_TARGET=4/36 dB,
                                     * MAX_LNA_GAIN=0 plein, MAX_DVGA_GAIN=0) : l'AGC s'adapte sur toute la
                                     * plage -40..-90 dBm. Remplace 0x27/NVS rx_gain qui ne couvrait qu'une
                                     * bande etroite (decrochages hors ~-60/-68 dBm). (void)s_rx_gain. */
    (void)s_rx_gain;
    cc1101_write_reg(0x1C, 0x00);   /* AGCCTRL1 */
    cc1101_write_reg(0x1D, 0x92);   /* AGCCTRL0 : FILTER_LENGTH=10 => frontiere de decision
                                     * ASK/OOK a 12 dB (vs 8 dB en 0x91). Un '0' doit etre >=24 dB
                                     * sous le '1' : le bruit pres du plancher ne declenche plus de
                                     * fausses transitions (TI DN022). Profalux = OOK plein, marge OK. */
    strobe(CC_SIDLE); esp_rom_delay_us(200);
    strobe(0x33);     /* SCAL */
    vTaskDelay(pdMS_TO_TICKS(3));
    strobe(0x3A);     /* SFRX */
    strobe(0x34);     /* SRX */
    for (int i = 0; i < 50; i++) {
        if ((cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F) == 0x0D) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGI(TAG, "RX(GDO0): MARCSTATE=0x%02X (0x0D=RX attendu)", cc1101_read_reg(CC_MARCSTATE | 0x40) & 0x1F);
    /* OOK sans squelch => bruit continu sur GDO0. Une rafale Profalux (preambule + entete
     * + 66 bits) est repetee ~10x. DEUX chemins selon la puce (cf issue #12) :
     *  - S3/C3 (SOC_RMT_SUPPORT_RX_PINGPONG) : en_partial_rx -> le driver remplit s_capbuf
     *    de facon CONTIGUE sur plusieurs callbacks d'UNE meme reception ; on accumule et on
     *    decode tout le buffer (vote majoritaire dans decode_rmt sur les repetitions), on ne
     *    re-arme qu'a la fin de reception (is_last) ou avant que le buffer ne reboucle.
     *  - ESP32 classique : PAS de ping-pong RX -> ESP-IDF v6.1 REFUSE en_partial_rx
     *    (rmt_rx.c: ESP_ERR_NOT_SUPPORTED) et rmt_receive echoue (regression v0.2.6). On
     *    garde donc l'ancien fonctionnement : une reception = un evenement delimite par
     *    l'idle, on decode l'evenement, on re-arme. */
    rmt_disable(s_cap); rmt_enable(s_cap); xQueueReset(s_capq);
    int64_t t_end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int r = -3;
    rmt_rx_done_event_data_t ev;
#if SOC_RMT_SUPPORT_RX_PINGPONG
    /* signal_range_max_ns plafonne a 32767000 ns (compteur 15 bits). 32 ms > la garde HCS
     * (~16-23 ms) -> toute la rafale s'accumule dans UNE reception. en_partial_rx requis. */
    rmt_receive_config_t rc = { .signal_range_min_ns = 3000, .signal_range_max_ns = 32000000,
                                .flags.en_partial_rx = true };
    const size_t cap = CAP_SYMBOLS;   /* capacite du buffer, en symboles */
    size_t prev_end = 0;              /* fin (offset) du fragment precedent, pour detecter le wrap */
    if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) == ESP_OK) {
        while (esp_timer_get_time() < t_end) {
            if (xQueueReceive(s_capq, &ev, pdMS_TO_TICKS(100)) != pdTRUE) continue;  /* tjrs en RX */
            /* en_partial_rx : fin reelle calculee a partir du POINTEUR (robuste a un
             * callback manque : les symboles intermediaires sont bien dans s_capbuf). */
            size_t off = (size_t)(ev.received_symbols - s_capbuf);
            if (off < prev_end) {   /* le buffer a reboucle : on repart sur une capture neuve */
                rmt_disable(s_cap); rmt_enable(s_cap); xQueueReset(s_capq); prev_end = 0;
                if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) != ESP_OK) break;
                continue;
            }
            size_t end = off + ev.num_symbols;
            if (end > cap) end = cap;
            prev_end = end;
            int n = decode_rmt(s_capbuf, end, out_bits, max_bits);
            diag_log_capture(s_capbuf, end, n, out_bits);
            if (n >= 64) { r = n; break; }                                  /* vraie trame */
            if (ev.flags.is_last || end >= cap - 160) {   /* fin de reception / wrap imminent */
                rmt_disable(s_cap); rmt_enable(s_cap); xQueueReset(s_capq); prev_end = 0;
                if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) != ESP_OK) break;
            }
        }
    } else r = -2;
#else
    /* ESP32 classique (pas de ping-pong RX) : en_partial_rx INTERDIT. Une reception =
     * un evenement (idle a 8 ms), on decode, on re-arme. decode_rmt (de-glitch + vote)
     * reste commun et tolere le bruit meme sur un seul evenement. */
    rmt_receive_config_t rc = { .signal_range_min_ns = 3000, .signal_range_max_ns = 8000000 };
    if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) == ESP_OK) {
        while (esp_timer_get_time() < t_end) {
            if (xQueueReceive(s_capq, &ev, pdMS_TO_TICKS(100)) != pdTRUE) continue;  /* tjrs en RX */
            int n = decode_rmt(ev.received_symbols, ev.num_symbols, out_bits, max_bits);
            diag_log_capture(ev.received_symbols, ev.num_symbols, n, out_bits);
            if (n >= 64) { r = n; break; }                                  /* vraie trame */
            if (rmt_receive(s_cap, s_capbuf, s_capbuf_sz, &rc) != ESP_OK) break;  /* re-arme */
        }
    } else r = -2;
#endif
    strobe(CC_SIDLE);
    gpio_set_direction(CC1101_PIN_GDO0, GPIO_MODE_INPUT_OUTPUT);   /* restaure pour le TX bit-bang */
    return r;
}

/* --- RX --- */
static void rx_task(void *pv) {
    ESP_LOGI(TAG, "RX task started");
    strobe(CC_SRX);
    while (s_rx_running) {
        /* TODO: Sample GDO0 with hardware timer + detect preamble + decode bits.
         * This skeleton just polls RSSI and provides infrastructure.
         * Full implementation: use RMT peripheral or GPIO ISR + circular buffer.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    strobe(CC_SIDLE);
    ESP_LOGI(TAG, "RX task stopped");
    s_rx_task = NULL;
    vTaskDelete(NULL);
}

int cc1101_rx_start(cc1101_rx_cb_t cb) {
    if (s_rx_running) return -1;
    s_rx_cb = cb;
    s_rx_running = true;
    xTaskCreate(rx_task, "cc1101_rx", 4096, NULL, 5, &s_rx_task);
    return 0;
}

int cc1101_rx_stop(void) {
    s_rx_running = false;
    return 0;
}

int8_t cc1101_get_rssi(void) {
    uint8_t r = cc1101_read_reg(CC_RSSI | 0x40);
    return (r >= 128) ? (int8_t)((r - 256) / 2 - 74) : (int8_t)(r / 2 - 74);
}
