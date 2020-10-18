#include <stdint.h>

void wifi_if_open();
void wifi_if_close();
int wifi_if_write(uint8_t *packet, int len);
int wifi_if_read(uint8_t *packet, int maxlen);
void wifi_if_get_mac(char *txtmac);
