#include <stdint.h>

void wifi_if_open();
void wifi_if_close();
int wifi_if_write(uint8_t *packet, int len);
int wifi_if_read(uint8_t *packet, int maxlen);
void wifi_if_get_mac(char *txtmac);

void wifi_if_wifid_send_to_pdp(void *buffer, uint16_t len);

void wifi_if_ena_auto_reconnect();
