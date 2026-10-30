/* freq_sweep — voir freq_sweep.h. */
#include "freq_sweep.h"
#include <string.h>

#define FSWEEP_MAX 128

/* Frequence ou la courbe croise `thr` entre les pas i (au-dessus) et j (en dessous). */
static uint32_t cross(const uint32_t *khz, const int16_t *v, int i, int j, int thr) {
    int dv = v[i] - v[j];
    if (dv <= 0) return khz[i];
    int64_t df = (int64_t)khz[j] - (int64_t)khz[i];
    return (uint32_t)((int64_t)khz[i] + df * (v[i] - thr) / dv);
}

fsweep_status_t fsweep_analyze(const uint32_t *khz, const int16_t *rssi_hdb, int n, fsweep_result_t *r) {
    memset(r, 0, sizeof(*r));
    if (n < 5 || n > FSWEEP_MAX) return FSWEEP_TOO_FEW;

    int ip = 0;
    for (int i = 1; i < n; i++) if (rssi_hdb[i] > rssi_hdb[ip]) ip = i;
    r->peak_hdb = rssi_hdb[ip];

    /* bruit = 1er quartile : le plateau du signal n'occupe qu'une partie du balayage */
    int16_t s[FSWEEP_MAX];
    memcpy(s, rssi_hdb, (size_t)n * sizeof(int16_t));
    for (int i = 1; i < n; i++) {   /* tri par insertion, n petit */
        int16_t x = s[i]; int k = i - 1;
        while (k >= 0 && s[k] > x) { s[k + 1] = s[k]; k--; }
        s[k + 1] = x;
    }
    r->floor_hdb = s[n / 4];
    if (r->peak_hdb - r->floor_hdb < FSWEEP_MIN_SNR_HDB) return FSWEEP_WEAK;

    int thr = (r->peak_hdb + r->floor_hdb) / 2;   /* mi-hauteur en dB : au milieu des flancs raides */
    int a = ip, b = ip;
    while (a > 0 && rssi_hdb[a - 1] >= thr) a--;
    while (b < n - 1 && rssi_hdb[b + 1] >= thr) b++;
    if (a == 0 || b == n - 1) return FSWEEP_AT_EDGE;

    r->lo_khz = cross(khz, rssi_hdb, a, a - 1, thr);
    r->hi_khz = cross(khz, rssi_hdb, b, b + 1, thr);
    r->center_khz = (r->lo_khz + r->hi_khz + 1) / 2;
    return FSWEEP_OK;
}
