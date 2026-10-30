#include "mqtt_tls_policy.h"
#include <string.h>
#include <ctype.h>

bool mqtt_host_is_ipv4(const char *uri) {
    const char *h = strstr(uri, "://");
    h = h ? h + 3 : uri;
    int groups = 0, digits = 0;
    for (; *h && *h != ':' && *h != '/'; h++) {
        if (*h == '.')      { if (!digits) return false; groups++; digits = 0; }
        else if (isdigit((unsigned char)*h)) digits++;
        else return false;
    }
    return groups == 3 && digits > 0;
}

bool mqtt_tls_skip_cn(const char *uri, bool has_ca) {
    return mqtt_host_is_ipv4(uri) && has_ca;
}
