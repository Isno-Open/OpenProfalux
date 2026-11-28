#pragma once
/* enrol_tx : les reglages d'emission de la SESSION d'enrolement (issue #159).
 *
 * Le panneau « Reglages avances d'emission » envoie porteuse, TE, bit de repetition et RMT. Ils
 * n'etaient lus qu'a la capture : une valeur changee ensuite n'etait jamais emise, alors que l'ecran
 * la montrait. Regle : la capture ouvre la session avec ses reglages, l'apprentissage les REMPLACE
 * s'il en apporte et les garde sinon (ancien client, sans corps), la fin de session (identite
 * deselectionnee ou enregistree en volet) revient a « heriter ». Ce que la session rend est ce que
 * la radio emet, ce que la fiche du volet enregistre et ce que l'ecran affiche.
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/enrol-tx. */
#include <stdbool.h>
#include <stdint.h>
#include "radio.h"         /* tx_params_t */
#include "tx_override.h"   /* tx_override_t */

/* Reglages d'une session, ou d'une requete. freq_khz / te_us a 0 = heriter du profil. */
typedef struct {
    uint32_t freq_khz;
    uint32_t te_us;
    uint8_t  rpt;   /* tx_override_t */
    uint8_t  rmt;   /* tx_override_t */
} enrol_tx_t;

void enrol_tx_clear(enrol_tx_t *s);                                  /* fin de session : tout herite */
void enrol_tx_capture(enrol_tx_t *s, const enrol_tx_t *request);     /* la capture ouvre la session */
/* L'apprentissage : given = la requete porte des reglages. Sans eux, ceux de la capture restent. */
void enrol_tx_learn(enrol_tx_t *s, const enrol_tx_t *request, bool given);
/* Les reglages tels que la radio les attend (rpt / rmt : -1 heriter, 0 / 1 forcer). */
tx_params_t enrol_tx_params(const enrol_tx_t *s);
/* TE reellement emis avec ces reglages, pour le journal : celui de la session, sinon celui de la radio.
 * radio_tx_device restaure le TE de la radio apres l'emission, le relire affichait toujours le defaut. */
uint32_t enrol_tx_emitted_te(const tx_params_t *p, uint32_t radio_te);
