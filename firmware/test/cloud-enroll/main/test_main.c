/* Banc de test des décisions du cloud (cloud_enroll.c) : validation du bundle d'appairage
 * (handler /api/cloud/pair) et gate d'activation (boot + /api/cloud/toggle). Sur PC. Voir README. */
#include <stdbool.h>
#include <stdio.h>
#include "cloud_enroll.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}

int main(void) {
    printf("Banc cloud_enroll\n");
    const char *dev = "opf-a1b2c3d4e5", *uri = "mqtts://mqtt.example.com:8883";
    const char *ca = "-----BEGIN CERTIFICATE-----...", *cert = ca, *key = "-----BEGIN PRIVATE KEY-----...";

    /* Bundle d'appairage : complet => accepté ; un champ manquant ou vide => refusé. */
    check(cloud_bundle_ok(dev, uri, ca, cert, key), "T0 bundle complet : accepté");
    check(!cloud_bundle_ok(NULL, uri, ca, cert, key), "T1 device_id manquant : refusé");
    check(!cloud_bundle_ok(dev, uri, ca, cert, NULL), "T2 clé manquante : refusée");
    check(!cloud_bundle_ok(dev, "", ca, cert, key), "T3 broker_url vide : refusé");
    check(!cloud_bundle_ok(dev, uri, "", cert, key), "T4 CA vide : refusé");

    /* Gate d'activation : il faut Wi-Fi + URL + interrupteur. */
    check(cloud_want(true, uri, true), "T5 wifi+uri+on : actif");
    check(!cloud_want(false, uri, true), "T6 wifi down : inactif");
    check(!cloud_want(true, "", true), "T7 pas d'URL (non appairé) : inactif");
    check(!cloud_want(true, uri, false), "T8 interrupteur coupé : inactif");
    check(!cloud_want(true, NULL, true), "T9 URL nulle : inactif");

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTous les tests cloud_enroll passent\n", g_fail);
    return g_fail ? 1 : 0;
}
