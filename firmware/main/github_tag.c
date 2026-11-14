/* Recherche de "tag_name" par morceaux : voir github_tag.h. */
#include "github_tag.h"
#include <string.h>

static const char KEY[] = "\"tag_name\":\"";
#define KEY_LEN (sizeof(KEY) - 1)

void github_tag_init(github_tag_t *st) { memset(st, 0, sizeof(*st)); }

/* Cherche la cle puis la valeur dans win[0..have). Rend true si c'est tranche. */
static bool scan(github_tag_t *st) {
    for (size_t i = 0; i + KEY_LEN <= st->have; i++) {
        if (memcmp(st->win + i, KEY, KEY_LEN)) continue;
        const char *v = st->win + i + KEY_LEN;
        size_t room = st->have - i - KEY_LEN;
        const char *e = memchr(v, '"', room);
        if (e) {
            size_t len = (size_t)(e - v);
            if (len >= sizeof(st->tag)) { st->done = true; return true; }   /* trop long : refus */
            memcpy(st->tag, v, len); st->tag[len] = 0;
            st->done = true; return true;
        }
        if (room >= sizeof(st->tag)) { st->done = true; return true; }      /* pas de fin : trop long */
        /* valeur coupee : on garde depuis la cle, la suite viendra */
        memmove(st->win, st->win + i, st->have - i);
        st->have -= i;
        return false;
    }
    /* pas de cle : on ne garde que ce qui peut en etre un debut */
    if (st->have > KEY_LEN - 1) {
        memmove(st->win, st->win + st->have - (KEY_LEN - 1), KEY_LEN - 1);
        st->have = KEY_LEN - 1;
    }
    return false;
}

bool github_tag_feed(github_tag_t *st, const char *data, size_t n) {
    while (n && !st->done) {
        size_t take = sizeof(st->win) - st->have;
        if (take > n) take = n;
        memcpy(st->win + st->have, data, take);
        st->have += take; data += take; n -= take;
        scan(st);
    }
    return st->done;
}
