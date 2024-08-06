//Quick and dirty routines to convert a BT-attached keyboard (special case: or gamepad, to
//control the original PDP11-clone Tetris) to serial link data as sent by a terminal.
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "hid_server.h"
#include "bthid.h"
#include "usb_hid_keys.h"

#include "esp_log.h"
#define TAG "bthid"


static RingbufHandle_t bthidrb;

typedef struct {
	uint8_t scancode;
	const char *mod_none;
	const char *mod_shift;
	const char *mod_ctrl;
	uint8_t flags;
} kb_key_t;

#define FL_CAPS 1

//ToDo: this is a mismatch of VT52 and VT100 keys... separate.
static const kb_key_t kb_key[]={
	{KEY_A, "a", "A", "\001", FL_CAPS},
	{KEY_B, "b", "B", "\002", FL_CAPS},
	{KEY_C, "c", "C", "\003", FL_CAPS},
	{KEY_D, "d", "D", "\004", FL_CAPS},
	{KEY_E, "e", "E", "\005", FL_CAPS},
	{KEY_F, "f", "F", "\006", FL_CAPS},
	{KEY_G, "g", "G", "\007", FL_CAPS},
	{KEY_H, "h", "H", "\010", FL_CAPS},
	{KEY_I, "i", "I", "\011", FL_CAPS},
	{KEY_J, "j", "J", "\012", FL_CAPS},
	{KEY_K, "k", "K", "\013", FL_CAPS},
	{KEY_L, "l", "L", "\014", FL_CAPS},
	{KEY_M, "m", "M", "\015", FL_CAPS},
	{KEY_N, "n", "N", "\016", FL_CAPS},
	{KEY_O, "o", "O", "\017", FL_CAPS},
	{KEY_P, "p", "P", "\020", FL_CAPS},
	{KEY_Q, "q", "Q", "\021", FL_CAPS},
	{KEY_R, "r", "R", "\022", FL_CAPS},
	{KEY_S, "s", "S", "\023", FL_CAPS},
	{KEY_T, "t", "T", "\024", FL_CAPS},
	{KEY_U, "u", "U", "\025", FL_CAPS},
	{KEY_V, "v", "V", "\026", FL_CAPS},
	{KEY_W, "w", "W", "\027", FL_CAPS},
	{KEY_X, "x", "X", "\030", FL_CAPS},
	{KEY_Y, "y", "Y", "\031", FL_CAPS},
	{KEY_Z, "z", "Z", "\032", FL_CAPS},
	{KEY_LEFTBRACE, "[", "{", "\033"},
	{KEY_BACKSLASH, "\\", "|", "\034"},
	{KEY_RIGHTBRACE, "]", "}", "\035"},
	{KEY_GRAVE, "`", "~", "\036"},
	{KEY_SLASH, "/", "?", "\037"},
	{KEY_1, "1", "!", NULL},
	{KEY_2, "2", "@", NULL},
	{KEY_3, "3", "#", NULL},
	{KEY_4, "4", "$", NULL},
	{KEY_5, "5", "%", NULL},
	{KEY_6, "6", "^", NULL},
	{KEY_7, "7", "&", NULL},
	{KEY_8, "8", "*", NULL},
	{KEY_9, "9", "(", NULL},
	{KEY_0, "0", ")", NULL},
	{KEY_MINUS, "-", "_", NULL},
	{KEY_EQUAL, "=", "+", NULL},
	{KEY_ENTER, "\r", "\r", NULL},
	{KEY_ESC, "\033", "\033", NULL},
	{KEY_DELETE, "\010", "\010", NULL},
	{KEY_BACKSPACE, "\177", "\177", NULL},
	{KEY_TAB, "\011", "\011", NULL},
	{KEY_SPACE, "\040", "\040", NULL},
	{KEY_SEMICOLON, ";", ":", NULL},
	{KEY_APOSTROPHE, "'", "\"", NULL},
	{KEY_COMMA, ",", "<", NULL},
	{KEY_DOT, ".", ">", NULL},
	{KEY_F1, "\033OP", "\033P", NULL},
	{KEY_F2, "\033OQ", "\033P", NULL},
	{KEY_F3, "\033OR", "\033P", NULL},
	{KEY_F4, "\033OS", "\033P", NULL},
	{KEY_F5, "\033[[15~", "\033[[15!", NULL},
	{KEY_F6, "\033[[17~", "\033[[17!", NULL},
	{KEY_F7, "\033[[18~", "\033[[18!", NULL},
	{KEY_F8, "\033[[19~", "\033[[19!", NULL},
	{KEY_F9, "\033[[20~", "\033[[20!", NULL},
	{KEY_F10, "\033[[21~", "\033[[21!", NULL},
	{KEY_F11, "\033[[23~", "\033[[23!", NULL},
	{KEY_F12, "\033[[24~", "\033[[24!", NULL},
	{KEY_UP, "\033[A", "\033[A", "\033A"},
	{KEY_DOWN, "\033[B", "\033[B", "\033B"},
	{KEY_RIGHT, "\033[C", "\033[C", "\033C"},
	{KEY_LEFT, "\033[D", "\033[D", "\033D"},
	{KEY_KP1, "1"},
	{KEY_KP2, "2"},
	{KEY_KP3, "3"},
	{KEY_KP4, "4"},
	{KEY_KP5, "5"},
	{KEY_KP6, "6"},
	{KEY_KP7, "7"},
	{KEY_KP8, "8"},
	{KEY_KP9, "9"},
	{KEY_KP0, "0"},
	{KEY_KPSLASH, "/"},
	{KEY_KPASTERISK, "*"},
	{KEY_KPMINUS, "-"},
	{KEY_KPPLUS, "+"},
	{KEY_KPENTER, "\r"},
	{0, NULL, NULL, NULL}
};


static void handle_kb_key(uint8_t modifiers, uint8_t key) {
	for (int i=0; kb_key[i].scancode!=0; i++) {
		if (kb_key[i].scancode==key) {
			const char *p=kb_key[i].mod_none;
			if ((modifiers&KEY_MOD_LCTRL) || (modifiers&KEY_MOD_RCTRL)) {
				if (kb_key[i].mod_ctrl) p=kb_key[i].mod_ctrl;
			} else if ((modifiers&KEY_MOD_LSHIFT) || (modifiers&KEY_MOD_RSHIFT)) {
				if (kb_key[i].mod_shift) p=kb_key[i].mod_shift;
			}
			if (p!=NULL) {
				while (*p!=0) {
					xRingbufferSend(bthidrb, p, 1, portMAX_DELAY);
					p++;
				}
			}
		}
	}
}


static void bthid_task(void *parm) {
        ESP_LOGI(TAG,"bthid: starting\n");
	hid_init("esppdp");
	uint8_t old_kbrep[64];
	int old_kbrep_len=0;
	while(1) {
		hid_update();
		uint8_t buf[64];
		int len=hid_get(buf, 64);
		if (len>0 && buf[0]==0xa1) {
			ESP_LOGI(TAG,"HID report: %d bytes\n", len);
			for (int j=0; j<len; j++) ESP_LOGI(TAG,"%02hhX ", buf[j]);
			ESP_LOGI(TAG,"\n");
			char c=0;
			if (buf[1]==0x1) {
				//keyboard
				for (int i=3; i<len; i++) {
					if (buf[i]!=0) {
						//check if key already was pressed
						int already_pressed=0;
						for (int j=3; j<old_kbrep_len; j++) {
							if (old_kbrep[j]==buf[i]) already_pressed=1;
						}
						if (!already_pressed) handle_kb_key(buf[2], buf[i]);
					}
				}
				memcpy(old_kbrep, buf, len);
				old_kbrep_len=len;
			} else if (buf[1]==0x3F) {
				//gamepad
				if (buf[2]&4) c='7';
				if (buf[2]&8) c='8';
				if (buf[2]&2) c='9';
				if (buf[2]&1) c='5';
				if (buf[3]&0x10) c='\n';
				if (buf[2]&0x10) c='0';
				if (buf[2]&0x20) c='D';
				if (c!=0) xRingbufferSend(bthidrb, &c, 1, portMAX_DELAY);
			}
		}
		vTaskDelay(2);
	}
}

void bthid_start() {
	bthidrb=xRingbufferCreate(16, RINGBUF_TYPE_BYTEBUF);
	xTaskCreatePinnedToCore(bthid_task, "bthid", 1024*16, NULL, 23, NULL, 0);
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

int bthid_connected() {
	return hid_connected_num()>0;
}
