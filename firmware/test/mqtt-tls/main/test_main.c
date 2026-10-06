/* Banc hote de la politique TLS mqtts (mqtt_tls_policy.c). Pur, un simple gcc suffit,
 * rend 1 au premier echec. Issu de la revue codex sur #11 (P1) : le controle du nom (CN)
 * du certificat ne se leve que pour un broker joint par IP ET avec un CA prive fourni.
 *
 * Mutation : passer mqtt_tls_skip_cn a "mqtt_host_is_ipv4(uri)" (sans le "&& has_ca")
 * doit faire echouer le cas "IP + bundle". */
#include "mqtt_tls_policy.h"
#include <stdio.h>
#include <stdbool.h>

static int fails = 0;
static void check(const char *name, bool got, bool want) {
    if (got != want) { printf("FAIL %s : rendu %d, attendu %d\n", name, got, want); fails++; }
    else             printf("ok   %s\n", name);
}

int main(void) {
    /* Reconnaissance d'une IPv4 litterale. */
    check("ipv4 avec port",     mqtt_host_is_ipv4("mqtts://192.168.1.10:8883"), true);
    check("ipv4 sans port",     mqtt_host_is_ipv4("mqtts://1.2.3.4"),           true);
    check("sans schema",        mqtt_host_is_ipv4("10.0.0.1"),                  true);
    check("nom d'hote",         mqtt_host_is_ipv4("mqtts://broker.local:8883"), false);
    check("ipv4 incomplete",    mqtt_host_is_ipv4("mqtts://1.2.3"),             false);
    check("chiffres dans nom",  mqtt_host_is_ipv4("mqtts://host1.2.3.4.lan"),   false);

    /* Decision de lever le controle du CN : IP ET CA prive, et rien d'autre. */
    check("IP + CA prive  -> leve",     mqtt_tls_skip_cn("mqtts://192.168.1.10", true),  true);
    check("IP + bundle    -> controle", mqtt_tls_skip_cn("mqtts://192.168.1.10", false), false);
    check("nom + CA prive -> controle", mqtt_tls_skip_cn("mqtts://broker.local", true),  false);
    check("nom + bundle   -> controle", mqtt_tls_skip_cn("mqtts://broker.local", false), false);

    if (fails) { printf("\n%d echec(s)\n", fails); return 1; }
    printf("\ntous les cas passent\n");
    return 0;
}
