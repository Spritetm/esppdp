#include "sim_defs.h"
#include "scp.h"
#include "sim_term.h"

#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "bthid.h"
#include "esp_heap_trace.h"
#include "ie15lcd.h"
#include "esp_timer.h"
char last_char;
int64_t autoboot_next_evt;
#endif


uint32_t sim_int_char = 5;
t_bool sim_signaled_int_char=FALSE;
uint32 sim_last_poll_kbd_time=0;
//int32 sim_tt_pchar = 0x00002780;
int32 sim_tt_pchar = 0xffffffff; //pass on all chars

char *read_line (char *cptr, int32 size, FILE *stream){
	printf("unimp: read_line\n");
	return "";
}

t_stat sim_set_pchar (int32 flag, CONST char *cptr) {
	printf("sim_set_pchar: stub\n");
	return SCPE_OK;
}


t_stat sim_poll_kbd (void) {
#ifndef ESP_PLATFORM
	int bytesWaiting;
	ioctl(0, FIONREAD, &bytesWaiting);
	if (bytesWaiting!=0) {
		return getchar() | SCPE_KFLAG;
	}
#else
	int c=getchar();
//If tracing is enabled, '|' dumps the trace.
#if CONFIG_HEAP_TRACING_STANDALONE
	if (c=='|') {
		ESP_ERROR_CHECK( heap_trace_stop() );
		heap_trace_dump();
	}
#endif
	if (c!=EOF) return c|SCPE_KFLAG;
	c=bthid_getchar();
	if (c!=-1) return c|SCPE_KFLAG;
	//Try to automagically boot into 2.11BSD if no keyboard is connected.
#if 0
	if (!bthid_connected() && esp_timer_get_time()>autoboot_next_evt) {
		autoboot_next_evt=esp_timer_get_time()+(1000UL*1000*5);
		if (last_char==':') return '\n'|SCPE_KFLAG;
		if (last_char=='#') return 0x4|SCPE_KFLAG;
	}
#endif

#endif
	return SCPE_OK;
}

/* Input character processing */

int32 sim_tt_inpcvt (int32 c, uint32 mode) {
	uint32 md = mode & TTUF_M_MODE;
	
	if (md != TTUF_MODE_8B) {
		uint32 par_mode = (mode >> TTUF_W_MODE) & TTUF_M_PAR;
		static int32 nibble_even_parity = 0x699600;		/* bit array indicating the even parity for each index (offset by 8) */

		c = c & 0177;
		if (md == TTUF_MODE_UC) {
			if (islower (c)) c = toupper (c);
			if (mode & TTUF_KSR) c = c | 0200;							/* Force MARK parity */
		}
		switch (par_mode) {
			case TTUF_PAR_EVEN:
				c |= (((nibble_even_parity >> ((c & 0xF) + 1)) ^ (nibble_even_parity >> (((c >> 4) & 0xF) + 1))) & 0x80);
				break;
			case TTUF_PAR_ODD:
				c |= ((~((nibble_even_parity >> ((c & 0xF) + 1)) ^ (nibble_even_parity >> (((c >> 4) & 0xF) + 1)))) & 0x80);
				break;
			case TTUF_PAR_MARK:
				c = c | 0x80;
				break;
		}
	} else {
		c = c & 0377;
	}
	return c;
}

/* Output character processing */

int32 sim_tt_outcvt (int32 c, uint32 mode) {
	uint32 md = mode & TTUF_M_MODE;
	if (md != TTUF_MODE_8B) {
		c = c & 0177;
		if (md == TTUF_MODE_UC) {
			if (islower (c)) c = toupper (c);
			if ((mode & TTUF_KSR) && (c >= 0140)) return -1;
		}
		if (((md == TTUF_MODE_UC) || (md == TTUF_MODE_7P)) &&
				((c == 0177) ||
				 ((c < 040) && !((sim_tt_pchar >> c) & 1)))) {
			return -1;
		}
	} else {
		c = c & 0377;
	}
	return c;
}

t_stat tmxr_set_console_units (UNIT *rxuptr, UNIT *txuptr) {
	//tmxr_set_line_unit (&sim_con_tmxr, 0, rxuptr);
	//tmxr_set_line_output_unit (&sim_con_tmxr, 0, txuptr);
	return SCPE_OK;
}

#ifdef ESP_PLATFORM
#include "ie15lcd.h"
#endif

t_stat sim_putchar_s (int32 c) {
#ifdef ESP_PLATFORM
	ie15_sendchar(c);
	if (c!=' ') last_char=c;
#endif
	putchar(c);
	return SCPE_OK;
}

t_stat sim_tt_show_modepar (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
	return SCPE_OK;
}

t_stat sim_tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
	uint32 par_mode = (TT_GET_MODE (uptr->flags) >> TTUF_W_MODE) & TTUF_M_PAR;
	uptr->flags = uptr->flags & ~((TTUF_M_MODE << TTUF_V_MODE) | (TTUF_M_PAR << TTUF_V_PAR) | TTUF_KSR);
	uptr->flags |= val;
	if (val != TT_MODE_8B) uptr->flags |= (par_mode << TTUF_V_PAR);
	return SCPE_OK;
}

t_stat sim_tt_set_parity (UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
	uptr->flags = uptr->flags & ~(TTUF_M_MODE | TTUF_M_PAR);
	uptr->flags |= TT_MODE_7B | val;
	return SCPE_OK;
}

//Called in main to init tt
t_stat sim_ttinit (void) {
#ifndef ESP_PLATFORM
	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag &= ~ICANON;
	tcsetattr(0, TCSANOW, &term);
	setbuf(stdin, NULL);
#else
	autoboot_next_evt=0;
#endif
	return SCPE_OK;
}

