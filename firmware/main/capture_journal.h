#pragma once
/* Journal de capture RF COMMUN : entrees io (decode 2-FSK) ET raw-ook (impulsions RMT), en RAM +
 * persistant NVS (survit au flash), sectionne par marqueurs d'etape. Lu par la couche assistance
 * (rapport -> issue GitHub + email). Generalise depuis io_trace.
 *
 * Proprietaire : la session io/assistance (elle lit le journal et l'envoie). Le moteur de capture
 * raw-ook (module rf_capture, autre session) N'EDITE PAS ce fichier : il appelle seulement
 * capture_journal_add_rawook(). La session io appelle capture_journal_add_io().
 *
 * Schema d'une entree (fige en coordination 2026-10-30) :
 *   { radio, band:"433|868", type:"raw-ook|io", t_ms, rssi, freq_khz, cmd, data }
 *   data = impulsions [[lvl,us]] pour raw-ook ; hex de la trame pour io.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CAP_IO = 0, CAP_RAW_OOK = 1 } cap_type_t;

/* Remet le journal a zero (debut d'une instance de capture) et efface la NVS. */
void capture_journal_reset(void);

/* Insere un marqueur de section (nom d'etape), pour savoir de quel geste vient la suite
 * (ex. "1 - compteur", "2 - cle", "3 - PROG", "capture CAME"). */
void capture_journal_mark(const char *label);

/* Ajoute une trame io DECODEE (2-FSK, 868). dir = 'R' recu / 'T' emis. bytes = trame (<= 32 o) ;
 * le dump la rend en hex. cmd = octet de commande decode (ou -1). band/freq fixes (868 / 868950).
 * Le masquage du champ-cle d'un 0x30 est fait par la couche assistance au moment du rapport,
 * PAS ici (le journal garde la donnee brute, c'est l'envoi qui masque). */
void capture_journal_add_io(char dir, uint8_t radio, int8_t rssi, int cmd,
                            const uint8_t *bytes, int n);

/* Ajoute une capture RAW-OOK (impulsions RMT). Pour la i-e impulsion : levels[i] = 0|1,
 * us[i] = duree en microsecondes. freq_khz = porteuse de la capture (INDISPENSABLE au rejeu,
 * le CC1101 est mono-frequence). band_mhz = 433 ou 868. n = nombre d'impulsions.
 * Rend 0 si ok, <0 si plein ou trop long (le journal est borne). */
int capture_journal_add_rawook(uint8_t radio, uint16_t band_mhz, uint32_t freq_khz, int8_t rssi,
                               const uint8_t *levels, const uint16_t *us, uint16_t n);

/* Ecrit le journal en NVS (survit au flash). A appeler en FIN de capture ou d'enrolement,
 * jamais par trame (un commit NVS prend ~10-20 ms et ferait rater des trames). */
void capture_journal_persist(void);

/* Rend le journal en texte (une ligne par entree, sections par marqueur). Rend les octets ecrits. */
int capture_journal_dump(char *out, size_t cap);

/* Nombre d'entrees (marqueurs compris). */
int capture_journal_count(void);

#ifdef __cplusplus
}
#endif
