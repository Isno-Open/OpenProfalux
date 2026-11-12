/* Mapping des topics cloud (isolation ACL par device_id). Unite PURE : voir cloud_topic.h. */
#include "cloud_topic.h"
#include <string.h>
#include <stdio.h>

bool cloud_topic_from_local(const char *local, const char *device_id, const char *base,
                            char *out, size_t cap) {
    size_t rl = strlen(OPF_ROOT);
    if (strncmp(local, OPF_ROOT, rl) != 0) return false;        /* hors namespace : local-only */
    const char *rest = local + rl;                              /* "cover/X/state" | "<id>/status" */
    size_t idl = strlen(device_id);
    if (idl && strncmp(rest, device_id, idl) == 0 && rest[idl] == '/')
        return (size_t)snprintf(out, cap, "%s", local) < cap;   /* deja prefixe device_id */
    return (size_t)snprintf(out, cap, "%s/%s", base, rest) < cap;
}

bool local_topic_from_cloud(const char *cloud_topic, int len, const char *base,
                            char *out, size_t cap) {
    char buf[160];
    int n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    memcpy(buf, cloud_topic, n); buf[n] = 0;
    size_t bl = strlen(base);
    if (strncmp(buf, base, bl) != 0 || buf[bl] != '/') return false;
    return (size_t)snprintf(out, cap, OPF_ROOT "%s", buf + bl + 1) < cap;
}
