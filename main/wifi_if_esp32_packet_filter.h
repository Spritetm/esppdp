#pragma once
#include <stdint.h>

#define PACKET_DEST_PDP11 1
#define PACKET_DEST_LWIP 2

int wifi_if_filter_find_packet_dest(uint8_t *buffer, uint16_t len);
int wifi_if_filter_pdp11_packet(uint8_t *buffer, uint16_t len);
