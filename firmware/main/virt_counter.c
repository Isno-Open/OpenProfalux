/* virt_counter : voir virt_counter.h. */
#include "virt_counter.h"

bool vc_ahead(uint16_t a, uint16_t b) {
    uint16_t d = (uint16_t)(a - b);
    return d != 0 && d < 0x8000u;
}

uint16_t vc_keep(uint16_t restored, uint16_t current) {
    return vc_ahead(current, restored) ? current : restored;
}
