/* enrol_tx : voir enrol_tx.h. Pur, teste sur PC (firmware/test/enrol-tx). */
#include "enrol_tx.h"

static int8_t forced(uint8_t override) {
    return override == TXOVR_ON ? 1 : override == TXOVR_OFF ? 0 : -1;
}

void enrol_tx_clear(enrol_tx_t *s) {
    *s = (enrol_tx_t){ .freq_khz = 0, .te_us = 0, .rpt = TXOVR_INHERIT, .rmt = TXOVR_INHERIT };
}
void enrol_tx_capture(enrol_tx_t *s, const enrol_tx_t *request) { *s = *request; }
void enrol_tx_learn(enrol_tx_t *s, const enrol_tx_t *request, bool given) { if (given) *s = *request; }

tx_params_t enrol_tx_params(const enrol_tx_t *s) {
    return (tx_params_t){ .freq_khz = s->freq_khz, .te_us = s->te_us, .rpt = forced(s->rpt), .rmt = forced(s->rmt) };
}
uint32_t enrol_tx_emitted_te(const tx_params_t *p, uint32_t radio_te) {
    return p && p->te_us ? p->te_us : radio_te;
}
