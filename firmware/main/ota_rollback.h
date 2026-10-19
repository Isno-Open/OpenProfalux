#pragma once
/* ota_rollback : ce que le firmware conclut des etats OTA pour le retour automatique.
 *
 * Avec CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE (sdkconfig.defaults), une mise a jour demarre EN
 * ESSAI (PENDING_VERIFY) : si elle redemarre avant d'etre validee (plantage, chien de garde),
 * le bootloader l'abandonne (ABORTED) et repart tout seul sur l'autre partition. L'option vit
 * dans le BOOTLOADER, que l'OTA ne remplace jamais : un boitier flashe avant elle garde un
 * bootloader qui laisse l'image NEW et ne revient jamais en arriere.
 *
 * Unite PURE (aucune dependance ESP-IDF) : testee sur PC, cf firmware/test/ota-rollback. Les
 * etats reprennent les valeurs d'esp_ota_img_states_t, verifiees a la compilation dans ota.c. */
#include <stdbool.h>
#include <stdint.h>

#define ORB_NEW            0x0u          /* ecrite, jamais demarree (ou bootloader d'avant) */
#define ORB_PENDING_VERIFY 0x1u          /* en essai : pas encore validee */
#define ORB_VALID          0x2u
#define ORB_INVALID        0x3u          /* ecartee a la main (bouton Version precedente) */
#define ORB_ABORTED        0x4u          /* abandonnee par le bootloader : redemarree en essai */
#define ORB_UNDEFINED      0xFFFFFFFFu   /* jamais passee par une mise a jour */

/* Retour automatique, d'apres l'etat de l'image en cours AU DEMARRAGE : 1 actif, 0 inactif
 * (bootloader d'avant), -1 inconnu. Seul un bootloader avec l'option fait passer NEW en essai :
 * hors de ces deux etats (pas de mise a jour recente), on garde ce qu'on savait (stored). */
int8_t orb_auto_observe(bool known, uint32_t state, int8_t stored);

/* Valider l'image en cours maintenant ? Seulement si elle est en essai. Sert a un redemarrage
 * VOULU et au debut d'une nouvelle mise a jour : la version tourne. Jamais apres le bouton
 * Version precedente, qui vient de la marquer INVALID : la revalider annulerait le retour. */
bool orb_validate_now(bool known, uint32_t state);

/* L'image en cours est-elle en essai (affichage) ? */
bool orb_on_trial(bool known, uint32_t state);

typedef enum {
    ORB_DISCARD_NONE = 0,
    ORB_DISCARD_AUTO,      /* abandonnee par le bootloader : la mise a jour n'a pas demarre */
    ORB_DISCARD_MANUAL,    /* ecartee par le bouton Version precedente */
} orb_discard_t;

/* Nature de la derniere image ecartee (esp_ota_get_last_invalid_partition) : exists = trouvee. */
orb_discard_t orb_discarded(bool exists, uint32_t state);
