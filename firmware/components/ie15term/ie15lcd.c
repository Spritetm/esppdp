/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "sdkconfig.h"

#include "esp_log.h"
#define TAG "ie15lcd"

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

/*
Emulation of a Russian IE15-type terminal on a  ILI9341/ST7789V 320x240 LCD in landscape mode.
*/

//Reference the binary-included character generator ROM contents of an original terminal
extern const uint8_t chargenrom[] asm("_binary_chargen_bin_start");

// The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct.
typedef struct {
	uint8_t cmd;
	uint8_t data[16];
	uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

typedef enum {
	LCD_TYPE_ILI = 1,
	LCD_TYPE_ST,
	LCD_TYPE_MAX,
} type_lcd_t;

//Place data into DRAM. Constant data gets placed into DROM by default, which is not accessible by DMA.
DRAM_ATTR static const lcd_init_cmd_t st_init_cmds[]={
#if DISPLAY_ROTATE
	/* Memory Data Access Control, MX=MV=1, MX=ML=MH=0, RGB=0 */
	{0x36, {(1<<5)|(1<<7)}, 1},
#else
	/* Memory Data Access Control, MX=MV=1, MY=ML=MH=0, RGB=0 */
	{0x36, {(1<<5)|(1<<6)}, 1},
#endif // DISPLAY_ROTATE
	/* Interface Pixel Format, 16bits/pixel for RGB/MCU interface */
	{0x3A, {0x55}, 1},
	/* Porch Setting */
	{0xB2, {0x0c, 0x0c, 0x00, 0x33, 0x33}, 5},
	/* Gate Control, Vgh=13.65V, Vgl=-10.43V */
	{0xB7, {0x45}, 1},
	/* VCOM Setting, VCOM=1.175V */
	{0xBB, {0x2B}, 1},
	/* LCM Control, XOR: BGR, MX, MH */
	{0xC0, {0x2C}, 1},
	/* VDV and VRH Command Enable, enable=1 */
	{0xC2, {0x01, 0xff}, 2},
	/* VRH Set, Vap=4.4+... */
	{0xC3, {0x11}, 1},
	/* VDV Set, VDV=0 */
	{0xC4, {0x20}, 1},
	/* Frame Rate Control, 60Hz, inversion=0 */
	{0xC6, {0x0f}, 1},
	/* Power Control 1, AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V */
	{0xD0, {0xA4, 0xA1}, 1},
	/* Positive Voltage Gamma Control */
	{0xE0, {0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19}, 14},
	/* Negative Voltage Gamma Control */
	{0xE1, {0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19}, 14},
	/* Sleep Out */
	{0x11, {0}, 0x80},
#if DISPLAY_INVERT
	{0x21, {0}, 0x80},
#endif
	/* Display On */
	{0x29, {0}, 0x80},
	{0, {0}, 0xff}
};

DRAM_ATTR static const lcd_init_cmd_t ili_init_cmds[]={
	/* Power contorl B, power control = 0, DC_ENA = 1 */
	{0xCF, {0x00, 0x83, 0X30}, 3},
	/* Power on sequence control,
	 * cp1 keeps 1 frame, 1st frame enable
	 * vcl = 0, ddvdh=3, vgh=1, vgl=2
	 * DDVDH_ENH=1
	 */
	{0xED, {0x64, 0x03, 0X12, 0X81}, 4},
	/* Driver timing control A,
	 * non-overlap=default +1
	 * EQ=default - 1, CR=default
	 * pre-charge=default - 1
	 */
	{0xE8, {0x85, 0x01, 0x79}, 3},
	/* Power control A, Vcore=1.6V, DDVDH=5.6V */
	{0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
	/* Pump ratio control, DDVDH=2xVCl */
	{0xF7, {0x20}, 1},
	/* Driver timing control, all=0 unit */
	{0xEA, {0x00, 0x00}, 2},
	/* Power control 1, GVDD=4.75V */
	{0xC0, {0x26}, 1},
	/* Power control 2, DDVDH=VCl*2, VGH=VCl*7, VGL=-VCl*3 */
	{0xC1, {0x11}, 1},
	/* VCOM control 1, VCOMH=4.025V, VCOML=-0.950V */
	{0xC5, {0x35, 0x3E}, 2},
	/* VCOM control 2, VCOMH=VMH-2, VCOML=VML-2 */
	{0xC7, {0xBE}, 1},
#if DISPLAY_ROTATE
	/* Memory access contorl, MX=MY=1, MV=1, ML=0, BGR=1, MH=0 */
	{0x36, {0xE8}, 1},
#else
	/* Memory access contorl, MX=MY=0, MV=1, ML=0, BGR=1, MH=0 */
	{0x36, {0x28}, 1},
#endif //DISPLAY_ROTATE
	/* Pixel format, 16bits/pixel for RGB/MCU interface */
	{0x3A, {0x55}, 1},
	/* Frame rate control, f=fosc, 70Hz fps */
	{0xB1, {0x00, 0x1B}, 2},
	/* Enable 3G, disabled */
	{0xF2, {0x08}, 1},
	/* Gamma set, curve 1 */
	{0x26, {0x01}, 1},
	/* Positive gamma correction */
	{0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
	/* Negative gamma correction */
	{0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
	/* Column address set, SC=0, EC=0xEF */
	{0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
	/* Page address set, SP=0, EP=0x013F */
	{0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
	/* Memory write */
	{0x2C, {0}, 0},
	/* Entry mode set, Low vol detect disabled, normal display */
	{0xB7, {0x07}, 1},
	/* Display function control */
	{0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
	/* Sleep out */
	{0x11, {0}, 0x80},
#if DISPLAY_INVERT
	{0x21, {0}, 0x80},
#endif	
	/* Display on */
	{0x29, {0}, 0x80},
	{0, {0}, 0xff},
};

/* Send a command to the LCD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
static void lcd_cmd(spi_device_handle_t spi, const uint8_t cmd) {
	esp_err_t ret;
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length=8;						//Command is 8 bits
	t.tx_buffer=&cmd;				//The data is the cmd itself
	t.user=(void*)0;				//D/C needs to be set to 0
	ret=spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

/* Send data to the LCD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
static void lcd_data(spi_device_handle_t spi, const uint8_t *data, int len) {
	esp_err_t ret;
	spi_transaction_t t;
	if (len==0) return;				//no need to send anything
	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length=len*8;					//Len is in bytes, transaction length is in bits.
	t.tx_buffer=data;				//Data
	t.user=(void*)1;				//D/C needs to be set to 1
	ret=spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
static void lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
	int dc=(int)t->user;
	gpio_set_level(DISPLAY_SPI_DC, dc);
}

static uint32_t lcd_get_id(spi_device_handle_t spi) {
	//get_id cmd
	lcd_cmd(spi, 0x04);

	spi_transaction_t t;
	memset(&t, 0, sizeof(t));
	t.length=8*3;
	t.flags = SPI_TRANS_USE_RXDATA;
	t.user = (void*)1;

	esp_err_t ret = spi_device_polling_transmit(spi, &t);
	assert( ret == ESP_OK );

	return *(uint32_t*)t.rx_data;
}

//Initialize the display
static void lcd_init(spi_device_handle_t spi) {
	int cmd=0;
	const lcd_init_cmd_t* lcd_init_cmds;

	//Initialize non-SPI GPIOs
	gpio_set_direction(DISPLAY_SPI_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction(DISPLAY_BCKL, GPIO_MODE_OUTPUT);

	// reset if reset line is given
#if DISPLAY_SPI_RST != GPIO_NUM_NC
	gpio_set_direction(DISPLAY_SPI_RST, GPIO_MODE_OUTPUT);
	//Reset the display
	gpio_set_level(DISPLAY_SPI_RST, 0);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(DISPLAY_SPI_RST, 1);
	vTaskDelay(100 / portTICK_PERIOD_MS);
#endif

	//detect LCD type
#if defined(DISPLAY_ID)
	uint32_t lcd_id = DISPLAY_ID;//lcd_get_id(spi);
#else
	uint32_t lcd_id = lcd_get_id(spi);
#endif
	int lcd_detected_type = 0;
	int lcd_type;

	ESP_LOGI(TAG,"LCD ID: %08X\n", (unsigned int)lcd_id);
	if ( lcd_id == 0 ) {
		//zero, ili
		lcd_detected_type = LCD_TYPE_ILI;
		ESP_LOGI(TAG,"ILI9341 detected.\n");
	} else {
		// none-zero, ST
		lcd_detected_type = LCD_TYPE_ST;
		ESP_LOGI(TAG,"ST7789V detected.\n");
	}

	lcd_type = lcd_detected_type;
	if ( lcd_type == LCD_TYPE_ST ) {
		ESP_LOGI(TAG,"LCD ST7789V initialization.\n");
		lcd_init_cmds = st_init_cmds;
	} else {
		ESP_LOGI(TAG,"LCD ILI9341 initialization.\n");
		lcd_init_cmds = ili_init_cmds;
	}

	//Send all the commands
	while (lcd_init_cmds[cmd].databytes!=0xff) {
		lcd_cmd(spi, lcd_init_cmds[cmd].cmd);
		lcd_data(spi, lcd_init_cmds[cmd].data, lcd_init_cmds[cmd].databytes&0x1F);
		if (lcd_init_cmds[cmd].databytes&0x80) {
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
		cmd++;
	}

	///Enable backlight if DISPLAY_BCKL is given
#if DISPLAY_BCKL != GPIO_NUM_NC
	gpio_set_level(DISPLAY_BCKL, DISPLAY_BCKL_ON );
#endif
}

static inline int get_pix(int c, int x, int y) {
	int p=c*8;
	return (chargenrom[p+y]>>(7-x))&1;
}

// #define CHW 4
// #define CHH 10

static const int lcdrgb(int r, int g, int b) {
	r=r>>3;
	g=g>>2;
	b=b>>3;
	int rr=(r<<11)+(g<<5)+(b);
	return ((rr&0xff)<<8)+(rr>>8);
}

static void draw_char(spi_device_handle_t spi,int c, int cx, int cy) {
	int x=cx*DISPLAY_CHW;
	int y=cy*DISPLAY_CHH;
	int cols[3];
	cols[0]=lcdrgb(0, 0, 0);
	cols[1]=lcdrgb(13, 210, 13);
	cols[2]=lcdrgb(16, 255, 16);

	uint16_t chardata[DISPLAY_CHW*DISPLAY_CHH];
	for (int ly=0; ly<DISPLAY_CHH; ly++) {
		for (int lx=0; lx<DISPLAY_CHW; lx++) {
			if (ly<8) {
				int p;
				p=get_pix(c, lx*2, ly);
				p+=get_pix(c, lx*2+1, ly);
				chardata[ly*DISPLAY_CHW+lx]=cols[p];
			} else {
				chardata[ly*DISPLAY_CHW+lx]=0;
			}
		}
	}

	spi_transaction_t trans[6];
	for (int i=0; i<6; i++) {
		memset(&trans[i], 0, sizeof(spi_transaction_t));
		if ((i&1)==0) {
			//Even transfers are commands
			trans[i].length=8;
			trans[i].user=(void*)0;
		} else {
			//Odd transfers are data
			trans[i].length=8*4;
			trans[i].user=(void*)1;
		}
		trans[i].flags=SPI_TRANS_USE_TXDATA;
	}

	trans[0].tx_data[0]=0x2A;			//Column Address Set
	trans[1].tx_data[0]=x>>8;				//Start Col High
	trans[1].tx_data[1]=x&0xff;				//Start Col Low
	trans[1].tx_data[2]=(x+DISPLAY_CHW-1)>>8;		//End Col High
	trans[1].tx_data[3]=(x+DISPLAY_CHW-1)&0xff;		//End Col Low
	trans[2].tx_data[0]=0x2B;			//Page address set
	trans[3].tx_data[0]=y>>8;		//Start page high
	trans[3].tx_data[1]=y&0xff;		//start page low
	trans[3].tx_data[2]=(y+DISPLAY_CHH-1)>>8;	 //end page high
	trans[3].tx_data[3]=(y+DISPLAY_CHH-1)&0xff;	 //end page low
	trans[4].tx_data[0]=0x2C;			//memory write
	trans[5].tx_buffer=chardata;		//finally send the line data
	trans[5].length=(DISPLAY_CHW*DISPLAY_CHH)*2*8;			 //Data length, in bits
	trans[5].flags=0; //undo SPI_TRANS_USE_TXDATA flag
	
	//Send all transactions.
	for (x=0; x<6; x++) {
		esp_err_t ret=spi_device_polling_transmit(spi, &trans[x]);
		assert(ret==ESP_OK);
	}
}


//Enable for VT50 emulation... turns out this is not necessary, the KOI-7 character set used
//by the IE15 seems to have the lower characters in place of the upper Ascii characters and 
//vice versa...
const int upper_only = 0;

static RingbufHandle_t ie15rb;

static int recv_char() {
	char *c;
	int r;
	size_t sz=0;
	c=xRingbufferReceiveUpTo(ie15rb, &sz, portMAX_DELAY, 1);
	r=*c;
	vRingbufferReturnItem(ie15rb, c);
	return r;
}

static void ie15_task(void *ptr) {
	esp_err_t ret;
	spi_device_handle_t spi;
	spi_bus_config_t buscfg={
		.miso_io_num=DISPLAY_SPI_MISO,
		.mosi_io_num=DISPLAY_SPI_MOSI,
		.sclk_io_num=DISPLAY_SPI_SCLK,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1,
		.max_transfer_sz=4094
	};
	spi_device_interface_config_t devcfg={
	  	.clock_speed_hz=DISPLAY_SPI_HZ,			//Clock out at 20 MHz
		.mode=0,								//SPI mode 0
		.spics_io_num=DISPLAY_SPI_CS,				//CS pin
		.queue_size=7,							//We want to be able to queue 7 transactions at a time
		.pre_cb=lcd_spi_pre_transfer_callback,	//Specify pre-transfer callback to handle D/C line
	};
	//Initialize the SPI bus
	ret=spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, DISPLAY_SPI_DMA);
	ESP_ERROR_CHECK(ret);
	//Attach the LCD to the SPI bus
	ret=spi_bus_add_device(DISPLAY_SPI_HOST, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);
	//Initialize the LCD
	lcd_init(spi);

	for (int y=0; y<240/DISPLAY_CHH; y++) {
		for (int x=0; x<320/DISPLAY_CHW; x++) {
			draw_char(spi, 0, x, y);
		}
	}

	int px=0;
	int py=0;
	int altchar=0;
	char *dispmem;
	dispmem=malloc(80*25);
	memset(dispmem, 0, 80*25);
	int need_redraw=0;
	while(1) {
		int c=recv_char();
		if (c=='\r') { //\r\n
			px=0;
		} else if (c=='\n') {
			py=py+1;
		} else if (c=='\b' || c==127) {
			//backspace / del
			px=px-1;
		} else if (c==0xe) {
			//SO, change to Russian charset
			altchar=1;
		} else if (c==0xF) {
			//SI, change to Latin charset
			altchar=0;
		} else if (c=='\t') {
			int tabstops[]={8,16,24,32,40,48,56,64,72,-1}; //from dec vt-50 user manual
			//janky, doesn't handle tab beyond 72
			for (int i=0; tabstops[i]!=-1; i++) {
				if (tabstops[i]>px) {
					px=tabstops[i];
					break;
				}
			}
		} else if (c==27) { //escape
			int ec=recv_char();
			if (ec=='A') {
				py=py-1;
			} else if (ec=='B') {
				py=py+1;
			} else if (ec=='C') {
				px=px-1;
			} else if (ec=='D') {
				px=px+1;
			} else if (ec=='F') {
				//enter gfx mode
				altchar=1;
			} else if (ec=='G') {
				//exit gfx mode
				altchar=0;
			} else if (ec=='H') { 
				//home
				px=0; py=0;
			} else if (ec=='I') {
				//reverse line feed
			} else if (ec=='J') {
				//clear to end of screen
				for (int i=(py*80)+px; i<80*24; i++) dispmem[i]=0;
				need_redraw=1;
			} else if (ec=='K') {
				//clear to end of line
				for (int i=px; i<80; i++) dispmem[py*80+i]=0;
				need_redraw=1;
			} else if (ec=='L') {
				//insert a line
			} else if (ec=='M') {
				//delete a line
			} else if (ec=='Y') {
				//set cursor position
				py=recv_char()-32;
				px=recv_char()-32;
			}
		} else {
			if (upper_only) {
				//vt50 emulation
#if 0 //IE15 does not do this mapping (real 'original' VT50 does)
				if (c==96) c=64;
				if (c=='{') c='[';
				if (c=='|') c='\\';
				if (c=='}') c=']';
				if (c=='~') c='^';
#endif
				if (altchar) {
					//cyrillic has more 'text' characters that need uppercasing
					if (c>='a' && c<=0x7e) c-=32;
				} else {
					if (c>='a' && c<='z') c-=32;
				}
			}
			if (altchar) c+=128;
			draw_char(spi, c, px, py);
			dispmem[py*80+px]=c;
			px++;
		}
		if (px<0) {
			px+=80;
			py-=1;
		}
		if (py<0) {
			//no scrollback implemented
			py=0;
		}
		if (px>80) {
			px=0;
			py++;
		}
		if (py>23) {
			for (int i=0; i<23; i++) memcpy(&dispmem[i*80], &dispmem[(i+1)*80], 80);
			memset(&dispmem[23*80], 0, 80);
			need_redraw=1;
			py--;
		}

		if (need_redraw) {
			for (int y=0; y<24; y++) {
				for (int x=0; x<80; x++) {
					draw_char(spi, dispmem[y*80+x], x, y);
				}
			}
			need_redraw=0;
		}
	}
}

void ie15_init(void) {
	ie15rb=xRingbufferCreate(8, RINGBUF_TYPE_BYTEBUF);
	xTaskCreatePinnedToCore(ie15_task, "ie15", 4096, NULL, 4, NULL, 1);
}

void ie15_sendchar(char c) {
	xRingbufferSend(ie15rb, &c, 1, portMAX_DELAY);
}
