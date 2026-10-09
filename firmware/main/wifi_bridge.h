#ifndef WIFI_BRIDGE_H
#define WIFI_BRIDGE_H
#include <stdbool.h>
int  wifi_bridge_init(void);
int  wifi_bridge_start_sta(const char *ssid, const char *pass);
int  wifi_bridge_start_softap(const char *ssid, const char *pass);
/* Mode secours : point d'acces de configuration ouvert ET client qui continue de chercher le
 * reseau configure par wifi_bridge_start_sta (30 s entre deux essais, 5 min si un appareil
 * est connecte au point d'acces). on_back est appele quand le client obtient une adresse IP. */
int  wifi_bridge_start_fallback(const char *ap_ssid, void (*on_back)(void));
bool wifi_bridge_in_fallback(void);
bool wifi_bridge_is_connected(void);
int  wifi_bridge_rssi(void);
void wifi_bridge_get_ip(char *buf, int len);
#endif
