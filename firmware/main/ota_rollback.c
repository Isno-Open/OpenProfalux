/* ota_rollback : voir ota_rollback.h. */
#include "ota_rollback.h"

int8_t orb_auto_observe(bool known, uint32_t state, int8_t stored) {
    if (!known) return stored;
    if (state == ORB_PENDING_VERIFY) return 1;   /* le bootloader a mis l'image en essai */
    if (state == ORB_NEW) return 0;              /* demarree mais laissee NEW : bootloader d'avant */
    return stored;
}

bool orb_validate_now(bool known, uint32_t state) { return known && state == ORB_PENDING_VERIFY; }

bool orb_on_trial(bool known, uint32_t state) { return known && state == ORB_PENDING_VERIFY; }

orb_discard_t orb_discarded(bool exists, uint32_t state) {
    if (!exists) return ORB_DISCARD_NONE;
    if (state == ORB_ABORTED) return ORB_DISCARD_AUTO;
    if (state == ORB_INVALID) return ORB_DISCARD_MANUAL;
    return ORB_DISCARD_NONE;
}
