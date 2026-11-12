/* Banc de test du mapping de topics cloud (cloud_topic.c) : le prefixe par device_id qui isole
 * chaque boitier dans openprofalux/{device_id}/# (propriete d'isolation ACL du broker), et le
 * retour des commandes recues a la forme locale. Sur PC. Voir README.md. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cloud_topic.h"

static int g_fail = 0;
static void check(bool ok, const char *name) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", name);
    if (!ok) g_fail++;
}
static bool eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

int main(void) {
    printf("Banc cloud_topic\n");
    const char *id = "opf-a1b2c3d4e5";
    const char *base = "openprofalux/opf-a1b2c3d4e5";
    char out[192];

    /* Local -> cloud : prefixe par le device_id (isolation ACL). */
    check(cloud_topic_from_local("openprofalux/cover/salon/state", id, base, out, sizeof out)
          && eq(out, "openprofalux/opf-a1b2c3d4e5/cover/salon/state"),
          "T0 etat d'un cover : prefixe par le device_id");
    check(cloud_topic_from_local("openprofalux/system/status", id, base, out, sizeof out)
          && eq(out, "openprofalux/opf-a1b2c3d4e5/system/status"),
          "T1 status systeme : prefixe");

    /* Hors namespace openprofalux/ : ne part PAS au cloud (Discovery HA = local-only). */
    check(!cloud_topic_from_local("homeassistant/cover/x/config", id, base, out, sizeof out),
          "T2 topic homeassistant/ : refuse (local-only)");

    /* Deja prefixe par NOTRE device_id : laisse tel quel (pas de double prefixe). */
    check(cloud_topic_from_local("openprofalux/opf-a1b2c3d4e5/cover/salon/state", id, base, out, sizeof out)
          && eq(out, "openprofalux/opf-a1b2c3d4e5/cover/salon/state"),
          "T3 deja prefixe : inchange");

    /* Le prefixe d'un AUTRE device n'est pas pris pour le notre : il est re-prefixe (anti-usurpation). */
    check(cloud_topic_from_local("openprofalux/opf-autre/cover/x/state", id, base, out, sizeof out)
          && eq(out, "openprofalux/opf-a1b2c3d4e5/opf-autre/cover/x/state"),
          "T4 prefixe d'un autre device : re-prefixe");

    /* Tampon trop petit : refuse, pas de debordement. */
    check(!cloud_topic_from_local("openprofalux/cover/salon/state", id, base, out, 10),
          "T5 tampon trop petit : refuse");

    /* Cloud -> local : retire le prefixe du device. */
    const char *c = "openprofalux/opf-a1b2c3d4e5/cover/salon/set";
    check(local_topic_from_cloud(c, (int)strlen(c), base, out, sizeof out)
          && eq(out, "openprofalux/cover/salon/set"),
          "T6 commande cloud ramenee a la forme locale");

    /* Cloud hors de notre base (autre device) : refuse (on ne traite pas la commande d'un autre). */
    const char *c2 = "openprofalux/opf-autre/cover/salon/set";
    check(!local_topic_from_cloud(c2, (int)strlen(c2), base, out, sizeof out),
          "T7 commande d'un autre device : refusee");

    /* Base en prefixe PARTIEL (sans le / suivant) : refuse. */
    const char *c3 = "openprofalux/opf-a1b2c3d4e5X/cover/x/set";
    check(!local_topic_from_cloud(c3, (int)strlen(c3), base, out, sizeof out),
          "T8 base en prefixe partiel (pas de /) : refusee");

    printf(g_fail ? "\n%d test(s) en echec\n" : "\nTous les tests cloud_topic passent\n", g_fail);
    return g_fail ? 1 : 0;
}
