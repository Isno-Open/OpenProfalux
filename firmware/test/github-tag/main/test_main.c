/* Banc de test de la recherche de "tag_name" par morceaux (main/github_tag.c), sur PC.
 * La reponse de l'API GitHub est decoupee a TOUTES les positions et en morceaux de toutes les
 * tailles : la valeur doit sortir identique, quelle que soit la coupure.
 * cc -std=gnu11 -Wall -Wextra -fsanitize=address -g -I ../../main main/test_main.c ../../main/github_tag.c */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "github_tag.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

/* Fait passer body en morceaux de taille step ; rend le tag trouve (vide sinon). */
static const char *run(const char *body, size_t step, github_tag_t *st) {
    github_tag_init(st);
    size_t n = strlen(body);
    for (size_t i = 0; i < n; i += step) {
        size_t m = n - i < step ? n - i : step;
        if (github_tag_feed(st, body + i, m)) break;
    }
    return st->tag;
}

int main(void) {
    printf("Banc github_tag\n");
    /* Debut reel d'une reponse de /releases/latest (raccourci), tag apres ~400 octets. */
    static const char body[] =
        "{\"url\":\"https://api.github.com/repos/Isno-Open/OpenProfalux/releases/123\","
        "\"assets_url\":\"https://api.github.com/repos/Isno-Open/OpenProfalux/releases/123/assets\","
        "\"upload_url\":\"https://uploads.github.com/repos/Isno-Open/OpenProfalux/releases/123/assets{?name,label}\","
        "\"html_url\":\"https://github.com/Isno-Open/OpenProfalux/releases/tag/v0.2.15\","
        "\"id\":123,\"author\":{\"login\":\"Shad107\"},\"node_id\":\"RE_kwDO\","
        "\"tag_name\":\"v0.2.15\",\"target_commitish\":\"main\",\"name\":\"v0.2.15\"}";
    github_tag_t st;

    bool all = true;
    for (size_t step = 1; step <= sizeof(body); step++)
        if (strcmp(run(body, step, &st), "v0.2.15")) { all = false; printf("    coupe en %zu : \"%s\"\n", step, st.tag); }
    check(all, "T0 tag trouve pour toutes les tailles de morceau (1 a toute la reponse)");

    /* La cle elle-meme coupee a chaque position : prefixe de 1 octet puis le reste d'un coup. */
    all = true;
    const char *k = strstr(body, "\"tag_name\"");
    for (size_t cut = 0; cut < 20; cut++) {
        size_t at = (size_t)(k - body) + cut;
        github_tag_init(&st);
        github_tag_feed(&st, body, at);
        github_tag_feed(&st, body + at, strlen(body) - at);
        if (strcmp(st.tag, "v0.2.15")) { all = false; printf("    cle coupee a +%zu : \"%s\"\n", cut, st.tag); }
    }
    check(all, "T1 cle et valeur coupees a chaque position");

    check(run("{\"name\":\"x\",\"body\":\"aucun tag\"}", 7, &st)[0] == 0 && !st.done,
          "T2 pas de tag_name : rien, et la lecture continue");

    check(run("{\"tag_name\":\"v0.2.16-cloudtest3-avec-un-nom-bien-trop-long\"}", 5, &st)[0] == 0 && st.done,
          "T3 valeur trop longue : refusee, lecture arretee");

    check(run("{\"name\":\"tag_name\",\"tag_name\":\"v1.0.0\"}", 3, &st)[0] && !strcmp(st.tag, "v1.0.0"),
          "T4 le mot tag_name en valeur ne trompe pas la recherche");

    printf(g_fail ? "ECHEC : %d\n" : "TOUT OK\n", g_fail);
    return g_fail ? 1 : 0;
}
