//Wifi interfacing code for the ESP32. This contains the routines that receive and send
//Ethernet packets from and to certain targets (PDP11, WiFi, LWIP).

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


#include <stdint.h>
#include <stdio.h>
#include "hexdump.h"
#include "wifi_if.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp32/rom/lldesc.h"
#include "sys/queue.h"
#include "soc/soc.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <unistd.h>
#include "xtensa/core-macros.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi_netif.h"
#include "esp_wpa.h"
#include "esp_log.h"
#include "wifi_if_esp32_packet_filter.h"
#include "wifid.h"

/*
As some things (notably DHCP) are handled by native code instead of by the PDP11, we need 
to not only start up the WiFi interface, but also connect it to the native TCP/IP stack
using a custom Rx callback that routes packets either to the internal or PDP11 network
interface.

To do this, we effectively implement a custom netif where we can send/receive packets from.
the LWIP stack. We then hook up the Tx function of that, as well as the PDP11 Tx function,
to the Tx function of the WiFi hardware. The differentiaton between both stacks happens
in the WiFi rx callback: we use a packet filter function to decide if the received packet
should be forwarded to the PDP11, the LWIP stack, both, or neither.
*/

#define MAX_RETRY 10
#define TAG "wifi_if"
#define PDP11_IN_FLIGHT_PACKETS 32

//#define DBG_DUMP_PACKETS

typedef struct {
	int len;
	uint8_t *buffer;
	void *eb;
} rx_packet_t;

QueueHandle_t rxqueue;

static esp_netif_t *netif;
void esp_read_mac(uint8_t *,int);

//Gets called whenever the local tcp/ip stack wants to transmit something
static esp_err_t wifi_netif_tx(void *driver, void *buffer, size_t len) {
#ifdef DBG_DUMP_PACKETS
	printf("LWIP Tx:\n");
	hexdump(buffer, len);
#endif
	esp_err_t ret=esp_wifi_internal_tx(ESP_IF_WIFI_STA, buffer, len);
	ESP_ERROR_CHECK(ret);
	return ret;
}

static esp_err_t wifi_netif_tx_wrap(void *driver, void *buffer, size_t len, void *netstack_buffer) {
#ifdef DBG_DUMP_PACKETS
	printf("LWIP Tx by ref:\n");
	hexdump(buffer, len);
#endif
	esp_err_t ret=esp_wifi_internal_tx_by_ref(ESP_IF_WIFI_STA, buffer, len, netstack_buffer);
	//Ignore any errors, wifi is lossy anyway.
	return ret;
}


//Gets called when the PDP11 wants to write a packet
int wifi_if_write(uint8_t *packet, int len) {
	if (len>1560) return 0; //huh, oversized packet?
	if (wifi_if_filter_pdp11_packet(packet, len)) {
		//filtered out as it's interpreted by the filter code itself
		return len;
	}
#ifdef DBG_DUMP_PACKETS
	printf("PDP11 TX:\n");
	hexdump(packet, len);
#endif
	esp_wifi_internal_tx(ESP_IF_WIFI_STA, packet, len);
	return len;
}


static esp_err_t wlan_send_to_pdp(void *buffer, uint16_t len, void *eb, int do_copy) {
	rx_packet_t p={};
	p.len=len;
	if (do_copy) {
		//Note: we can theoretically also solve this by incrementing the refcount on eb...
		p.buffer=malloc(len);
		if (!p.buffer) {
			printf("wlan_send_to_pdp: out of memory for copied packet\n");
			return ESP_ERR_NO_MEM;
		}
		memcpy(p.buffer, buffer, len);
	} else {
		p.buffer=buffer;
		p.eb=eb;
	}

	int ret = xQueueSend(rxqueue, &p, 0);
	if (ret != pdTRUE) {
		if (!p.eb) free(p.buffer);
		printf("WiFi: rx queue full...\n");
	}
	return ESP_OK;
}

void wifi_if_wifid_send_to_pdp(void *buffer, uint16_t len) {
#ifdef DBG_DUMP_PACKETS
	printf("wifid resp pkt:\n");
	hexdump(buffer, len);
#endif
	//injects a malloc()'ed packet for wifid into the packet stream to the pdp11
	rx_packet_t p={};
	p.len=len;
	p.buffer=buffer;
	int ret = xQueueSend(rxqueue, &p, 0);
	if (ret != pdTRUE) {
		if (!p.eb) free(p.buffer);
		printf("WiFi: wifid: rx queue full...\n");
	}
}


//Gets called whenever the WiFi interface received something
static esp_err_t wlan_sta_rx_callback(void *buffer, uint16_t len, void *eb) {
	if (!buffer || !eb) {
		if (eb) {
			esp_wifi_internal_free_rx_buffer(eb);
		}
		return ESP_OK;
	}
	
	int dest=wifi_if_filter_find_packet_dest(buffer, len);
	if (dest&PACKET_DEST_PDP11) {
		int do_copy=dest&(~PACKET_DEST_PDP11); //see if the packet is also sent to other dests
		esp_err_t res=wlan_send_to_pdp(buffer, len, eb, do_copy);
		if (res!=ESP_OK) {
			if (do_copy) goto ERROR;
		}
	}
	if (dest&PACKET_DEST_LWIP) {
		ESP_ERROR_CHECK(esp_netif_receive(netif, buffer, len, eb));
	}
	return ESP_OK;
ERROR:
	esp_wifi_internal_free_rx_buffer(eb);
	return ESP_OK;
}

void wifi_netif_free_buffer(void *h, void* buffer) {
	esp_wifi_internal_free_rx_buffer(buffer);
}

/*
00000000  00 00 08 06 ff ff ff ff  ff ff a6 0f 06 0c 67 94  |..............g.|
00000010  08 06 00 01 08 00 06 04  00 01 a6 0f 06 0c 67 94  |..............g.|
00000020  ce 8b ca 01 00 00 00 00  00 00 ce 8b ca c9        |..............|
0000002e
*/

//Gets called when the PDP11 tries to read a packet
int wifi_if_read(uint8_t *packet, int maxlen) {
	rx_packet_t p;
	if (xQueueReceive(rxqueue, &p, 0)==pdPASS) {
		if (p.len>maxlen) {
			printf("wifi_if_read: received packet len=%d, larger than maxlen %d\n", p.len, maxlen);
			esp_wifi_internal_free_rx_buffer(p.eb);
			return 0;
		}
		memcpy(packet, p.buffer, p.len);
		if (p.eb) {
			esp_wifi_internal_free_rx_buffer(p.eb);
		} else {
			//copied packet
			free(p.buffer);
		}
		return(p.len);
	} else {
		return 0;
	}
}


int s_retry_num=0;
int sta_connected=0;

static void sta_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
//		esp_netif_dhcpc_stop(netif);
		if (s_retry_num < MAX_RETRY) {
			s_retry_num++;
			ESP_LOGI(TAG, "Disconnected from AP, retrying...");
			esp_wifi_connect();
		} else {
			ESP_LOGI(TAG, "Disconnected from AP, not retrying.");
			sta_connected = 0;
			wifid_signal_noconnect();
//			esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, NULL);
		}
		esp_netif_action_disconnected(netif, event_base, event_id, event_data);
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
		ESP_LOGI(TAG,"connected to AP");
//		esp_netif_dhcpc_start(netif);
		esp_netif_action_connected(netif, event_base, event_id, event_data);
//		esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, (wifi_rxcb_t) wlan_sta_rx_callback);
		s_retry_num=0;
		sta_connected=1;
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ESP_LOGI(TAG,"got IP");
		esp_netif_action_got_ip(netif, event_base, event_id, event_data);
		ip_event_got_ip_t *ip=(ip_event_got_ip_t*)event_data;
		wifid_signal_connected(ip->esp_netif, &ip->ip_info);
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
		wifid_signal_scan_done();
	}
}

void wifi_if_ena_auto_reconnect(int do_reconnect) {
	if (do_reconnect) {
		s_retry_num=0;
	} else {
		s_retry_num=MAX_RETRY+1; //won't reconnect when this is the case
	}
}


static esp_netif_t *wifi_sta_filtered_init_netif() {
	const esp_netif_inherent_config_t base_cfg = {
		.flags=(esp_netif_flags_t)(ESP_NETIF_DHCP_CLIENT | ESP_NETIF_FLAG_GARP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED),
		ESP_COMPILER_DESIGNATED_INIT_AGGREGATE_TYPE_EMPTY(mac)
		ESP_COMPILER_DESIGNATED_INIT_AGGREGATE_TYPE_EMPTY(ip_info)
		.get_ip_event=IP_EVENT_STA_GOT_IP,
		.lost_ip_event=IP_EVENT_STA_LOST_IP,
		.if_key="WIFI_STA_FILTERED",
		.if_desc="sta_filtered",
		.route_prio=100
	};
	esp_netif_driver_ifconfig_t driver_ifconfig={
		.transmit = wifi_netif_tx,
		.transmit_wrap = wifi_netif_tx_wrap,
		.driver_free_rx_buffer = wifi_netif_free_buffer
	};

	const esp_netif_config_t cfg = {
		.base=&base_cfg,
		.driver=&driver_ifconfig,
		.stack=_g_esp_netif_netstack_default_wifi_sta //we override the input function of this later
	};

	esp_netif_t *wifi_filtered_netif = esp_netif_new(&cfg);
	ESP_ERROR_CHECK(esp_netif_set_driver_config(wifi_filtered_netif, &driver_ifconfig));
	ESP_ERROR_CHECK(esp_netif_attach(wifi_filtered_netif, &driver_ifconfig));
	esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, (wifi_rxcb_t) wlan_sta_rx_callback);
	esp_err_t  ret;
	if ((ret = esp_wifi_internal_reg_netstack_buf_cb(esp_netif_netstack_buf_ref, esp_netif_netstack_buf_free)) != ESP_OK) {
		ESP_LOGE(TAG, "netstack cb reg failed with %d", ret);
	}
	uint8_t mac[6];
	esp_read_mac(mac, 0);
	esp_netif_set_mac(wifi_filtered_netif, mac);
	esp_netif_action_start(wifi_filtered_netif, NULL, 0, NULL);
	return wifi_filtered_netif;
}

void wifi_if_open() {
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_err_t result = esp_wifi_init_internal(&cfg);
	if (result != ESP_OK) {
		ESP_LOGE(TAG,"Init internal failed");
		return;
	}
	result = esp_supplicant_init();
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "Failed to init supplicant (0x%x)", result);
		esp_err_t deinit_ret = esp_wifi_deinit_internal();
		if (deinit_ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to deinit Wi-Fi internal (0x%x)", deinit_ret);
			return;
		}
		return;
	}
	result = esp_wifi_start();
	if (result != ESP_OK) {
		ESP_LOGI(TAG,"Failed to start WiFi");
		return;
	}
	//Set a default event handler that takes any events.
	ESP_ERROR_CHECK(esp_event_handler_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, &sta_event_handler, NULL));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_LOGI(TAG,"STA mode set");

	rxqueue=xQueueCreate(PDP11_IN_FLIGHT_PACKETS, sizeof(rx_packet_t));

	esp_netif_init();
	esp_event_loop_create_default();
	netif=wifi_sta_filtered_init_netif();
	esp_netif_dhcpc_start(netif);

	//Note: we do not connect; that is left to the wifid implementation running on the pdp11.
}

void wifi_if_close() {
	//not implemented
}


void wifi_if_get_mac(char *txtmac) {
	uint8_t mac[6];
	esp_read_mac(mac, 0);
	sprintf(txtmac, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

