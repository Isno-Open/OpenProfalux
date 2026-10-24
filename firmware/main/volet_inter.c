/* Position intermediaire d'un moteur : decisions pures. Voir volet_inter.h. */
#include "volet_inter.h"

bool inter_btn_goto(uint8_t button) { return button == 0x6 || button == 0xC; }
bool inter_btn_save(uint8_t button) { return button == INTER_BTN_SAVE; }

inter_act_t inter_plan(int dir, int target, float position, int pct, int *ndir) {
    *ndir = 0;
    if (pct < 0 || pct > 100) return dir ? INTER_FREEZE : INTER_KEEP;   /* ou va-t-il ? on ne sait pas */
    int d = pct > position + 1 ? -1 : pct < position - 1 ? +1 : 0;      /* plus ferme -> descendre */
    if (!d) return dir ? INTER_FREEZE : INTER_KEEP;
    if (dir == d && target == pct) return INTER_KEEP;   /* repetitions du meme appui */
    *ndir = d;
    return INTER_MOVE;
}
