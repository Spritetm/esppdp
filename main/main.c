/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "ie15lcd.h"
#include "nvs_flash.h"
#include "bthid.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "wifi_if.h"
#include "wifid.h"
#include "wifid_iface.h"


#define TAG "main"

int main(int argc, char **argv);

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

static void wifi_if_autoconnect() {
	vTaskDelay(2000/portTICK_PERIOD_MS);
	wifid_cmd_t cmd2={
		.cmd=CMD_CONNECT,
		.connect.ssid="Sprite",
		.connect.pass="pannenkoek"
	};
	wifid_parse_packet((uint8_t *)&cmd2, sizeof(wifid_cmd_t));
}


void app_main(void) {
    esp_err_t ret;
#if CONFIG_HEAP_TRACING_STANDALONE
	ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
#endif

    /* Initialize NVS — it is used to store PHY calibration data */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

	ie15_init();
	//We cheat here: as the buffer in the IE15 emu is only 8 bytes, the following routine
	//will only finish after the initialization is complete.
	const char signon[]="Initializing emulator...\r\n";
	for (const char *p=signon; *p!=0; p++) ie15_sendchar(*p);

#if 1
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};
	sdmmc_card_t* card;

	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	host.max_freq_khz=5000;
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	// GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
	// Internal pull-ups are not sufficient. However, enabling internal pull-ups
	// does make a difference some boards, so we do that here.
	gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);	// CMD, needed in 4- and 1- line modes
	gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);	// D0, needed in 4- and 1-line modes
	gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);	// D1, needed in 4-line mode only
	gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);	// D2, needed in 4-line mode only
	gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);	// D3, needed in 4- and 1-line modes
	ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "SD-card: Failed to mount filesystem.");
		return;
	}
    sdmmc_card_print_info(stdout, card);
#endif

	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 5,
		.format_if_mount_failed = true
	};
	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		return;
	}

	bthid_start();

#if 0 //wifid test code
	wifi_if_open(); //note: normally called from pdp11 sim code
	vTaskDelay(5000/portTICK_PERIOD_MS);
	wifid_cmd_t cmd={
		.cmd=CMD_CONNECT,
		.connect.ssid="SpriteVpn",
		.connect.pass="pannenkoek"
	};
	wifid_parse_packet((uint8_t *)&cmd, sizeof(wifid_cmd_t));
	vTaskDelay(15000/portTICK_PERIOD_MS);
	wifid_cmd_t cmd3={
		.cmd=CMD_CONNECT,
		.connect.ssid="Sprite",
		.connect.pass="pxannenkoek"
	};
	wifid_parse_packet((uint8_t *)&cmd3, sizeof(wifid_cmd_t));
	vTaskDelay(15000/portTICK_PERIOD_MS);
	wifid_cmd_t cmd2={
		.cmd=CMD_CONNECT,
		.connect.ssid="Sprite",
		.connect.pass="pannenkoek"
	};
	wifid_parse_packet((uint8_t *)&cmd2, sizeof(wifid_cmd_t));
	return;
#endif

#if 1 //auto-connect to Sprite
	esp_timer_handle_t h;
	esp_timer_create_args_t ta={
		.callback=wifi_if_autoconnect,
		.name="wifi_autocon"
	};
	esp_timer_create(&ta, &h);
	esp_timer_start_once(h, 5*1000*1000);
#endif


#if CONFIG_HEAP_TRACING_STANDALONE
	ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
	ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
#endif
	char *args[]={"simh", 0};
	main(1, args);
    fflush(stdout);
    esp_restart();
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
	//We don't have signals; no need to write to rem
	int wait_ms=req->tv_nsec/1000000L+req->tv_sec*1000;
//	printf("sleep %d ms (%ld ns)\n", wait_ms, req->tv_nsec);
	int wait_ticks=wait_ms/portTICK_PERIOD_MS;
//	if (wait_ticks==0) wait_ticks=1;
	if (wait_ticks) vTaskDelay(wait_ticks);
	return 0;
}

