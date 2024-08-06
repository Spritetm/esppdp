//Main code for pdp11 emulator thing
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------------
 * Added SPI connected SDcards and more generalized hw definition approach.
 * SvenMb
 * ----------------------------------------------------------------------------
 */


#include <stdio.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

// load hardware specific definitions, use 'idf.py menuconfig' to set it 

#if CONFIG_ESPPDP_HW_WROVER_KIT
#include "hw_wrover.h"
#elif CONFIG_ESPPDP_HW_2432S028
#include "hw_2432S028.h"
#elif CONFIG_ESPPDP_HW_FINAL
#include "hw_final.h"
#else
#error ESPPDP Hardware not configured, use 'idf.py menuconfig' to choose
#endif

#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "ie15lcd.h"
#include "nvs_flash.h"
#include "bthid.h"
#include "esp_vfs_fat.h"
#ifdef SD_MMC
  #include "driver/sdmmc_host.h"
#endif // SD_MMC
#ifdef SD_SPI
#include "driver/sdspi_host.h"
#endif // SD_SPI
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "wifi_if.h"
#include "wifid.h"
#include "wifid_iface.h"
#include "esp_timer.h"

#define TAG "main"

int main(int argc, char **argv);

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif

//ESP-IDF doesn't implement nanosleep, but SIMH needs it. We implement it here
//using an esp_timer. (Note this code is not re-entrant, do not try to sleep
//from multiple threads, if you need a generic nanosleep implementation look
//elsewhere!)

static esp_timer_handle_t nanosleep_timer;
static TaskHandle_t nanosleep_task = NULL;

void nanosleep_callback(void *arg) {
	xTaskNotifyGive(nanosleep_task);
}

void nanosleep_init() {
	const esp_timer_create_args_t args={
		.callback=nanosleep_callback,
		.arg=NULL,
		.dispatch_method=ESP_TIMER_TASK
	};
	esp_timer_create(&args, &nanosleep_timer);
}

//note: not reentrant!
int nanosleep(const struct timespec *req, struct timespec *rem) {
	//Note: We don't have signals; no need to write to rem
	nanosleep_task=xTaskGetCurrentTaskHandle();
	uint64_t wait_us=req->tv_nsec/1000UL+req->tv_sec*1000000UL;
	esp_timer_start_once(nanosleep_timer, wait_us);
	ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
	return 0;
}



void app_main(void) {
	esp_err_t ret;
#if CONFIG_HEAP_TRACING_STANDALONE
	ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
#endif

	//Initialize NVS
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

#if !defined(SD_NONE)
	//Initialize SD-card, if possible
	ESP_LOGI(TAG,"Initialize SD-card");
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 2,
		.allocation_unit_size = 16 * 1024
	};
	sdmmc_card_t* card;
  #if defined(SD_MMC)
	ESP_LOGI(TAG,"Create SDMMC_HOST");
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
//	host.max_freq_khz=5000;
	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	// GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
	// Internal pull-ups are not sufficient. However, enabling internal pull-ups
	// does make a difference some boards, so we do that here.
	gpio_set_pull_mode(SD_MMC_CMD, GPIO_PULLUP_ONLY);	// CMD, needed in 4- and 1- line modes
	gpio_set_pull_mode(SD_MMC_D0, GPIO_PULLUP_ONLY);	// D0, needed in 4- and 1-line modes
	gpio_set_pull_mode(SD_MMC_D1, GPIO_PULLUP_ONLY);	// D1, needed in 4-line mode only
	gpio_set_pull_mode(SD_MMC_D2, GPIO_PULLUP_ONLY);	// D2, needed in 4-line mode only
	gpio_set_pull_mode(SD_MMC_D3, GPIO_PULLUP_ONLY);	// D3, needed in 4- and 1-line modes
	ESP_LOGI(TAG,"Initialize VFS via SDMMC\n");
	ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
  #elif defined(SD_SPI)
	ESP_LOGI(TAG,"Create SDSPI_HOST\n");
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.slot = SD_SPI_HOST;
	spi_bus_config_t bus_cfg = {
        	.mosi_io_num = SD_SPI_MOSI,
        	.miso_io_num = SD_SPI_MISO,
        	.sclk_io_num = SD_SPI_SCLK,
        	.quadwp_io_num = -1,
        	.quadhd_io_num = -1,
        	.max_transfer_sz = 4000,
	};
	ESP_LOGI(TAG,"Initialize SPI_BUS\n");
	// ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
	ret = spi_bus_initialize(host.slot, &bus_cfg, SD_SPI_DMA);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to initialize bus.");
		return;
	}    
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = SD_SPI_CS;
	slot_config.host_id = host.slot;

	ESP_LOGI(TAG,"Initialize VFS via SDSPI\n");
	//ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
	ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
  #else
    #error SD_MMC or SD_SPI or SD_NONE must be defined
#endif // defined(SD_MMC) // defined(SD_SPI)
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "SD-card: Failed to mount filesystem.");
		const char noflopstr[]="No SD card. Trying boot from built-in floppy.\r\n";
		for (const char *p=noflopstr; *p!=0; p++) ie15_sendchar(*p);
	} else {
		sdmmc_card_print_info(stdout, card);
	}
#else
	ESP_LOGI(TAG,"No SD-Card defined");
#endif // defined(SD_NONE)

#if !defined(SPIFFS_NONE)
	//Mount spiffs. This contains (a) floppy image(s).
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 2,
		.format_if_mount_failed = true
	};
	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
		}
		return;
	}
#else
	ESP_LOGI(TAG,"No SPIFFS defined");
#endif // defined(SPIFFS_NONE)
	
	bthid_start();

	nanosleep_init();

#if CONFIG_HEAP_TRACING_STANDALONE
	ESP_ERROR_CHECK( heap_trace_init_standalone(trace_record, NUM_RECORDS) );
	ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
#endif
	
	//Start up main SIMH emulator
	char *args[]={"simh", 0};
	main(1, args);		//Note: This is in scp.c and is the SIMH main routine.
	fflush(stdout);
	esp_restart();
}


