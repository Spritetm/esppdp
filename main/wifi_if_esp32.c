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
#include "esp_wpa.h"

#define MAX_RETRY 5
#define TAG "wifi_if"

typedef struct {
	int len;
	uint8_t *buffer;
	void *eb;
} rx_packet_t;

QueueHandle_t rxqueue;

static esp_err_t wlan_sta_rx_callback(void *buffer, uint16_t len, void *eb) {
	esp_err_t ret = ESP_OK;

	if (!buffer || !eb) {
		if (eb) {
			esp_wifi_internal_free_rx_buffer(eb);
		}
		return ESP_OK;
	}

	rx_packet_t p;
	p.len=len;
	p.buffer=buffer;
	p.eb=eb;

	ret = xQueueSend(rxqueue, &p, 0);
	if (ret != pdTRUE) {
		printf("WiFi: rx queue full...\n");
		goto DONE;
	}
	return ESP_OK;
DONE:
	esp_wifi_internal_free_rx_buffer(eb);
	return ESP_OK;
}

int s_retry_num=0;
int sta_connected=0;

static void sta_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < MAX_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			ESP_LOGI(TAG,"sta disconncted");
			sta_connected = 0;
			esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA,NULL);
		}
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
		ESP_LOGI(TAG,"connected to AP");
		esp_wifi_internal_reg_rxcb(ESP_IF_WIFI_STA, (wifi_rxcb_t) wlan_sta_rx_callback);
		s_retry_num = 0;
		sta_connected = 1;
	}
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
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &sta_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &sta_event_handler, NULL));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_LOGI(TAG,"STA mode set");

	esp_wifi_set_ps(WIFI_PS_NONE);

	rxqueue=xQueueCreate(16, sizeof(rx_packet_t));

	wifi_config_t* wifi_cfg = (wifi_config_t *)calloc(1,sizeof(wifi_config_t));
	const char *ssid="Internet";
	const char *pass="pCHY6HWJ44";
	strncpy((char*)wifi_cfg->sta.ssid, ssid, sizeof(wifi_cfg->sta.ssid));
	strncpy((char*)wifi_cfg->sta.password, pass, sizeof(wifi_cfg->sta.password));
	wifi_cfg->sta.pmf_cfg.capable = true;
	wifi_cfg->sta.pmf_cfg.required = false;
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_cfg));
	ESP_ERROR_CHECK(esp_wifi_connect());
}

void wifi_if_close() {
	//not implemented
}

int wifi_if_write(uint8_t *packet, int len) {
	if (len>1560) return 0; //huh, oversized packet?
//	printf("Writing to WiFi:\n");
//	hexdump(packet, len);
	esp_wifi_internal_tx(ESP_IF_WIFI_STA, packet, len);
	return len;
}

/*
00000000  00 00 08 06 ff ff ff ff  ff ff a6 0f 06 0c 67 94  |..............g.|
00000010  08 06 00 01 08 00 06 04  00 01 a6 0f 06 0c 67 94  |..............g.|
00000020  ce 8b ca 01 00 00 00 00  00 00 ce 8b ca c9        |..............|
0000002e
*/

int wifi_if_read(uint8_t *packet, int maxlen) {
	rx_packet_t p;
	if (xQueueReceive(rxqueue, &p, 0)==pdPASS) {
		if (p.len>maxlen) {
			printf("wifi_if_read: received packet len=%d, larger than maxlen %d\n", p.len, maxlen);
			esp_wifi_internal_free_rx_buffer(p.eb);
			return 0;
		}
		memcpy(packet, p.buffer, p.len);
		esp_wifi_internal_free_rx_buffer(p.eb);
		printf("wifi_read: %d\n", p.len);
		return(p.len);
	} else {
		return 0;
	}
}

void wifi_if_get_mac(char *txtmac) {
	uint8_t mac[6];
	esp_read_mac(mac, 0);
	sprintf(txtmac, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

