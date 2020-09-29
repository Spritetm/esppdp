#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "hid_server.h"
#include "bthid.h"

static RingbufHandle_t bthidrb;


static void bthid_task(void *parm) {
	printf("bthid: starting\n");
	hid_init("tetris");
	while(1) {
		hid_update();
		uint8_t buf[1024];
		int i=hid_get(buf, 1024);
		if (i>0) {
			printf("HID report: %d bytes\n", i);
			for (int j=0; j<i; j++) printf("%02hhX ", buf[j]);
			printf("\n");
			char c=0;
			if (buf[2]&4) c='7';
			if (buf[2]&8) c='8';
			if (buf[2]&2) c='9';
			if (buf[2]&1) c='5';
			if (buf[3]&0x10) c='\n';
			if (buf[2]&0x10) c='0';
			if (buf[2]&0x20) c='D';
			if (c!=0) xRingbufferSend(bthidrb, &c, 1, portMAX_DELAY);
		}
		vTaskDelay(2);
	}
}

void bthid_start() {
	bthidrb=xRingbufferCreate(8, RINGBUF_TYPE_BYTEBUF);
	xTaskCreatePinnedToCore(bthid_task, "bthid", 1024*32, NULL, 23, NULL, 0);
//	bthid_task(NULL);
}

int bthid_getchar() {
	char *c;
	int r;
	size_t sz=0;
	c=xRingbufferReceiveUpTo(bthidrb, &sz, 0, 1);
	if (c!=NULL) {
		r=*c;
		vRingbufferReturnItem(bthidrb, c);
		return r;
	}
	return -1;
}

