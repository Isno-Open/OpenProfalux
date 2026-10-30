/* Banc d'essai de l'encapsulation io (io_framer) et du desassemblage (io_unframer), sur PC.
 *
 * ⚠️ POURQUOI CE BANC EXISTE. Au 2026-10-28, apres HUIT versions d'essai avec le testeur
 * (issue #87), aucun essai ne couvrait la couche liaison io, et la carte ne journalisait pas
 * ce qu'elle EMET : on a toujours compare notre intention au trafic de la box de copie, jamais notre
 * emission a la sienne. Ce banc ferme la moitie hors ligne de ce trou.
 *
 * Les trames de reference sont de VRAIES trames captees en l'air chez le testeur (journaux 6, 7
 * et 8 de l'issue #87, plus la capture d'enrolement la box de copie du commentaire #20). Elles ne sont
 * pas construites pour l'essai : c'est ce que le moteur recoit et accepte.
 *
 * Compiler et lancer : voir le README du dossier. Rend 0 si tout passe, 1 sinon.
 */
#include <stdio.h>
#include <string.h>
#include "io_framer.h"
#include "io_unframer.h"

/* Mot de synchronisation que le CC1101 pose devant la trame (SYNC1:SYNC0 de io_radio.c), et
 * preambule io. Le banc en REDERIVE l'octet de residu, au lieu de recopier la constante. */
#define IO_SYNC_WORD   0x57FDu
#define IO_PREAMBLE_B  0x55u
#define IO_SYNC_LOGIQUE_1 0xFFu
#define IO_SYNC_LOGIQUE_0 0x33u

static int from_hex(const char *h, uint8_t *out, int cap) {
    int n = 0;
    while (h[0] && h[1] && n < cap) { unsigned v; sscanf(h, "%2x", &v); out[n++] = (uint8_t)v; h += 2; }
    return n;
}

/* Trames SYNTHETIQUES pour le banc de framing : longueurs et octet de controle reels, identites
 * et cles anonymisees (src = AABBCC, remplissage a zero). Le banc ne verifie que la regle de
 * longueur et les bits hauts de l'octet 0, pas la src/cle/CRC : l'anonymisation ne change rien. */
static const char *REELLES[] = {
    "FC2000003FAABBCC3000000000000000000000000000000000000000000000", /* 0x30, 31 o */
    "FC0000003FAABBCC3000000000000000000000000000000000000000000000", /* 0x30, 31 o */
    "F50000003FAABBCC20000000000000000000000000000000",               /* 0x20, 24 o */
    "F10000003FAABBCC2E0000000000000000000000",                       /* 0x2E, 20 o */
    "F10000003FAABBCC390000000000000000000000",                       /* 0x39, 20 o */
    "F80000003FAABBCC20000000000000000000000000000000000000",         /* 0x20, 27 o */
    "F60000003FAABBCC0000000000000000000000000000000000",             /* 0x00, 25 o */
    NULL
};

static int ok = 0, ko = 0;
#define VERIF(cond, fmt, ...) do { if (cond) ok++; else { ko++; \
    printf("  ECHEC : " fmt "\n", ##__VA_ARGS__); } } while (0)

/* Pousse un octet io encapsule UART (start 0, 8 bits LSB d'abord, stop 1) dans une suite de bits. */
static void uart_bits(unsigned char b, char *dst, int *n) {
    dst[(*n)++] = '0';
    for (int i = 0; i < 8; i++) dst[(*n)++] = (char)('0' + ((b >> i) & 1));
    dst[(*n)++] = '1';
}

int main(void) {
    printf("== regle de longueur : total == 1 + (octet0 & 0x1F) + 2\n");
    for (int i = 0; REELLES[i]; i++) {
        uint8_t f[64]; int n = from_hex(REELLES[i], f, sizeof f);
        VERIF(1 + (f[0] & 0x1F) + 2 == n,
              "trame %d : octet0=0x%02X annonce %d octets, la trame en fait %d",
              i, f[0], 1 + (f[0] & 0x1F) + 2, n);
    }

    printf("== octet de controle 0 : ordre (0xC0) et protocole 1W (0x20) poses\n");
    for (int i = 0; REELLES[i]; i++) {
        uint8_t f[64]; from_hex(REELLES[i], f, sizeof f);
        VERIF((f[0] & 0xE0) == 0xE0, "trame %d : octet0=0x%02X, bits hauts 0x%02X au lieu de 0xE0",
              i, f[0], f[0] & 0xE0);
    }

    /* ⚠️ Ce controle-la est le seul qui attache notre emission a la REALITE de l'air. Sans lui,
     * changer l'octet de residu laisse tout le reste vert (mesure le 2026-10-28) : le
     * desassembleur jette ses 8 premiers bits sans les lire, donc l'aller-retour ne le voit pas.
     * On redérive le residu : le flux encapsule du preambule et du sync logique, moins la
     * fenetre de 16 bits que le CC1101 consomme comme mot de synchronisation. */
    printf("== l'octet de residu est bien la queue du sync, pas une constante en l'air\n");
    {
        char bits[256]; int n = 0;
        for (int i = 0; i < 4; i++) uart_bits(IO_PREAMBLE_B, bits, &n);
        uart_bits(IO_SYNC_LOGIQUE_1, bits, &n);
        uart_bits(IO_SYNC_LOGIQUE_0, bits, &n);
        bits[n] = '\0';

        char motif[17];
        for (int i = 0; i < 16; i++) motif[i] = (char)('0' + ((IO_SYNC_WORD >> (15 - i)) & 1));
        motif[16] = '\0';

        const char *p = strstr(bits, motif);
        VERIF(p != NULL, "le mot de synchronisation 0x%04X est absent du flux io", IO_SYNC_WORD);
        if (p) {
            const char *reste = p + 16;
            int lr = (int)strlen(reste);
            VERIF(lr == 8, "apres le sync il reste %d bits, pas 8 : le residu ne tient pas sur un octet", lr);
            if (lr == 8) {
                unsigned attendu = 0;
                for (int i = 0; i < 8; i++) attendu = (attendu << 1) | (unsigned)(reste[i] - '0');
                uint8_t f[4] = { 0xE8, 0x00, 0x00, 0x00 }, fifo[32];
                size_t m = io_frame_to_fifo(f, 1, fifo, sizeof fifo);
                VERIF(m > 0 && fifo[0] == attendu,
                      "octet de residu emis 0x%02X, l'air en attend 0x%02X", m ? fifo[0] : 0, attendu);
            }
        }
    }

    printf("== aller-retour : io_frame_to_fifo puis le desassembleur rend la trame\n");
    for (int i = 0; REELLES[i]; i++) {
        uint8_t f[64], fifo[96];
        int n = from_hex(REELLES[i], f, sizeof f);
        size_t m = io_frame_to_fifo(f, (uint8_t)n, fifo, sizeof fifo);
        if (m == 0) { ko++; printf("  ECHEC : trame %d, encapsulation refusee\n", i); continue; }
        IohcUnframer u; iohc_unframer_reset(&u);
        IohcUnframerResult r = IohcUnframerNeedBits;
        for (size_t k = 0; k < m; k++) {
            r = iohc_unframer_feed_byte(&u, fifo[k]);
            if (r == IohcUnframerFrameDone || r == IohcUnframerFramingError || r == IohcUnframerOverflow) break;
        }
        if (r != IohcUnframerFrameDone) {
            ko++; printf("  ECHEC : trame %d, desassemblage rend %d au lieu de FrameDone\n", i, r);
            continue;
        }
        VERIF(u.frame_pos == n && memcmp(u.frame, f, (size_t)n) == 0,
              "trame %d : aller-retour non conforme (%d octets rendus sur %d)", i, u.frame_pos, n);
    }

    printf("\n%d verifications OK, %d echecs\n", ok, ko);
    return ko ? 1 : 0;
}
