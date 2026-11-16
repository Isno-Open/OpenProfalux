/* Décisions du cloud (bundle d'enrôlement, activation). Unités PURES : voir cloud_enroll.h. */
#include "cloud_enroll.h"

static bool present(const char *s) { return s && s[0]; }

bool cloud_bundle_ok(const char *device_id, const char *broker_url,
                     const char *ca, const char *cert, const char *key) {
    return present(device_id) && present(broker_url) && present(ca) && present(cert) && present(key);
}

bool cloud_want(bool wifi_up, const char *cloud_uri, bool cloud_on) {
    return wifi_up && present(cloud_uri) && cloud_on;
}
