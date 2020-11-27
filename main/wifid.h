#include "esp_event.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"

void wifid_parse_packet(uint8_t *buffer, int len);
void wifid_signal_scan_done();
void wifid_signal_connected(esp_netif_t *netif, esp_netif_ip_info_t *ip);
