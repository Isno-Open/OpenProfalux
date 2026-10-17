/* captive_dns — voir captive_dns.h. */
#include "captive_dns.h"
#include <string.h>
#include "lwip/udp.h"
#include "lwip/tcpip.h"
#include "esp_log.h"

static const char *TAG = "dns";
static struct udp_pcb *s_pcb;

/* Repond a chaque requete : en-tete recopie, drapeaux reponse, et pour une question de type A
 * une reponse 192.168.4.1 (TTL 60 s). Les autres types (AAAA...) repondent sans adresse. */
static void dns_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg;
    uint8_t q[512];
    u16_t n = pbuf_copy_partial(p, q, sizeof(q), 0);
    pbuf_free(p);
    if (n < 12 || (q[2] & 0x80) || q[4] != 0 || q[5] != 1) return;   /* pas une requete a 1 question */
    size_t i = 12;                                                    /* fin du nom de la question */
    while (i < n && q[i]) i += (size_t)q[i] + 1;
    if (i + 5 > n) return;
    size_t qend = i + 5;                                              /* nom + 0 + type(2) + classe(2) */
    bool type_a = q[i + 1] == 0 && q[i + 2] == 1;
    struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(qend + (type_a ? 16 : 0)), PBUF_RAM);
    if (!r) return;
    uint8_t *o = r->payload;
    memcpy(o, q, qend);
    o[2] = 0x81; o[3] = 0x80;                                         /* reponse, recursion dispo, OK */
    o[6] = 0; o[7] = type_a ? 1 : 0; o[8] = o[9] = o[10] = o[11] = 0;
    if (type_a) {
        static const uint8_t ans[16] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
        memcpy(o + qend, ans, sizeof(ans));
    }
    udp_sendto(pcb, r, addr, port);
    pbuf_free(r);
}

static void dns_setup(void *arg) {   /* dans le thread tcpip */
    (void)arg;
    if (s_pcb) return;
    s_pcb = udp_new();
    if (!s_pcb || udp_bind(s_pcb, IP_ANY_TYPE, 53) != ERR_OK) { ESP_LOGE(TAG, "portail : DNS non demarre"); return; }
    udp_recv(s_pcb, dns_recv, NULL);
    ESP_LOGI(TAG, "portail captif : DNS du point d'acces actif");
}

void captive_dns_start(void) { tcpip_callback(dns_setup, NULL); }
