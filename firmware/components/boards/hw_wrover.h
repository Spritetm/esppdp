// HW definition for Jeroens wrover based hardware
// Sven Muehlberg


#define ESP32_WROVER

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define DISPLAY_CHW 4
#define DISPLAY_CHH 10
#define DISPLAY_BCKL 5
#define DISPLAY_BCKL_ON 0
#define DISPLAY_BCKL_OFF 1
// #define DISPLAY_ID 1
#define DISPLAY_INVERT 0

#define DISPLAY_SPI
#define DISPLAY_SPI_HOST HSPI_HOST
#define DISPLAY_SPI_DMA SPI_DMA_CH2
#define DISPLAY_SPI_MODE 0
#define DISPLAY_SPI_MISO GPIO_NUM_NC
#define DISPLAY_SPI_MOSI 23
#define DISPLAY_SPI_SCLK 19
#define DISPLAY_SPI_CS 22
#define DISPLAY_SPI_DC 21
#define DISPLAY_SPI_RST GPIO_NUM_NC
#define DISPLAY_SPI_HZ 24000000

// #define SD_SPI
// #define SD_SPI_HOST SPI3_HOST
// #define SD_SPI_DMA SPI_DMA_CH1
// #define SD_SPI_CS 5
// #define SD_SPI_MOSI 23
// #define SD_SPI_SCLK 18
// #define SD_SPI_MISO 19

#define SD_MMC
#define SD_MMC_CMD 15
#define SD_MMC_D0 2
#define SD_MMC_D1 4
#define SD_MMC_D2 12
#define SD_MMC_D3 13

// #define SD_NONE
