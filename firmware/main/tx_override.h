#pragma once
/* Tri-etat d'une option d'emission PAR APPAREIL (bit de repetition, RMT). 0 = heriter du defaut
 * (profil, puis firmware) ; on force explicitement ensuite. La valeur 0 est la sentinelle "heriter"
 * pour qu'un volet fraichement memset() herite naturellement, et pour que la cle soit OMISE du JSON
 * quand on herite (sauvegardes existantes inchangees, issue #111).
 *
 * En-tete sans dependance : la fiche d'un volet (cfg_model.h) et la session d'enrolement
 * (enrol_tx.h, testee sur PC) partagent cette seule definition. */
typedef enum {
    TXOVR_INHERIT = 0,   /* heriter du defaut du profil */
    TXOVR_OFF     = 1,   /* forcer desactive pour cet appareil */
    TXOVR_ON      = 2,   /* forcer active pour cet appareil */
} tx_override_t;
