/* Banc de test de req_guard (envois venus d'un autre site), sur PC. Voir README.md. */
#include <stdbool.h>
#include <stdio.h>
#include "req_guard.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

int main(void) {
    printf("Banc req_guard\n");
    /* Ce qui doit TOUJOURS passer : aucun usage legitime ne doit changer. */
    check(rg_write_ok(NULL, NULL, "192.168.1.44"), "T0 sans en-tete (Home Assistant en REST, curl, scripts) : accepte");
    check(rg_write_ok(NULL, NULL, NULL), "T1 sans en-tete ni Host : accepte");
    check(rg_write_ok("same-origin", "http://192.168.1.44", "192.168.1.44"), "T2 l'interface elle-meme (same-origin) : accepte");
    check(rg_write_ok("same-origin", "https://volets.example.org", "192.168.1.44"), "T3 derriere un proxy (Host reecrit) : accepte");
    check(rg_write_ok("same-site", "http://a.lan", "b.lan"), "T4 same-site : accepte");
    check(rg_write_ok("none", NULL, "192.168.1.44"), "T5 none (adresse tapee, favori) : accepte");
    check(rg_write_ok(" Same-Origin ", NULL, NULL), "T6 Sec-Fetch-Site : casse et espaces ignores");
    /* Une iframe dans Home Assistant : la page de l'iframe est celle du boitier, ses requetes sont same-origin. */
    check(rg_write_ok("same-origin", "http://openprofalux.local", "openprofalux.local"), "T7 iframe dans Home Assistant : accepte");

    /* L'attaque : une page d'un autre site fait faire un POST au navigateur. */
    check(!rg_write_ok("cross-site", "http://site-piege.example", "192.168.1.44"), "T8 cross-site : refuse");
    check(!rg_write_ok("cross-site", "http://192.168.1.44", "192.168.1.44"), "T9 cross-site : refuse meme si l'origine ressemble");
    check(!rg_write_ok("CROSS-SITE", NULL, NULL), "T10 cross-site en majuscules : refuse");
    check(!rg_write_ok("  cross-site ", NULL, NULL), "T10b cross-site entoure d'espaces : refuse");

    /* Navigateurs anciens (sans Sec-Fetch-Site) : l'hote de l'origine doit etre celui de Host. */
    check(rg_write_ok(NULL, "http://192.168.1.44", "192.168.1.44"), "T11 ancien navigateur, meme hote : accepte");
    check(rg_write_ok(NULL, "http://192.168.1.44:80", "192.168.1.44"), "T12 port ignore (origine)");
    check(rg_write_ok(NULL, "http://192.168.1.44", "192.168.1.44:80"), "T13 port ignore (Host)");
    check(rg_write_ok(NULL, "http://OpenProfalux.LOCAL", "openprofalux.local."), "T14 casse et point final ignores");
    check(rg_write_ok(NULL, "http://[fe80::1]", "[FE80::1]:80"), "T15 IPv6 entre crochets");
    check(rg_write_ok(NULL, " http://192.168.4.1 ", "192.168.4.1"), "T16 portail captif (192.168.4.1), espaces ignores");
    check(!rg_write_ok(NULL, "http://site-piege.example", "192.168.1.44"), "T17 ancien navigateur, autre hote : refuse");
    check(!rg_write_ok(NULL, "http://192.168.1.44.site-piege.example", "192.168.1.44"), "T18 hote qui commence pareil : refuse");
    check(!rg_write_ok(NULL, "http://192.168.1.4", "192.168.1.44"), "T19 hote qui est un debut : refuse");
    check(!rg_write_ok(NULL, "null", "192.168.1.44"), "T20 Origin null (page isolee, fichier local) : refuse");
    check(!rg_write_ok(NULL, "http://192.168.1.44", NULL), "T21 Origin sans Host : refuse");
    check(!rg_write_ok(NULL, "http://", "192.168.1.44"), "T22 origine sans hote : refuse");
    check(!rg_write_ok(NULL, "http://[fe80::1", "[fe80::1]"), "T23 IPv6 mal fermee : refusee");

    printf(g_fail ? "\n%d echec(s)\n" : "\nTout passe\n", g_fail);
    return g_fail ? 1 : 0;
}
