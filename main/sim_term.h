#include "scp.h"

extern int32 sim_tt_pchar;
extern t_bool sim_signaled_int_char;
extern uint32_t sim_int_char;
extern uint32 sim_last_poll_kbd_time;
t_stat sim_set_pchar (int32 flag, CONST char *cptr);
t_stat sim_poll_kbd (void);
int32 sim_tt_inpcvt (int32 c, uint32 mode);
int32 sim_tt_outcvt (int32 c, uint32 mode);
t_stat tmxr_set_console_units (UNIT *rxuptr, UNIT *txuptr);
t_stat sim_putchar_s (int32 c);
t_stat sim_tt_show_modepar (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat sim_tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_tt_set_parity (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat sim_ttinit (void);
char *read_line (char *cptr, int32 size, FILE *stream);

#define TTUF_V_MODE     (UNIT_V_UF + 0)
#define TTUF_W_MODE     2
#define  TTUF_MODE_7B   0
#define  TTUF_MODE_8B   1
#define  TTUF_MODE_UC   2
#define  TTUF_MODE_7P   3
#define TTUF_M_MODE     ((1u << TTUF_W_MODE) - 1)
#define TTUF_V_PAR      (TTUF_V_MODE + TTUF_W_MODE)
#define TTUF_W_PAR      2
#define  TTUF_PAR_SPACE 0
#define  TTUF_PAR_MARK  1
#define  TTUF_PAR_EVEN  2
#define  TTUF_PAR_ODD   3
#define TTUF_M_PAR      ((1u << TTUF_W_PAR) - 1)
#define  TTUF_KSR       (1u << (TTUF_W_MODE + TTUF_W_PAR))
#define TTUF_V_UF       (TTUF_V_MODE + TTUF_W_MODE + TTUF_W_PAR)
#define TT_MODE         (TTUF_M_MODE << TTUF_V_MODE)
#define  TT_MODE_7B     (TTUF_MODE_7B << TTUF_V_MODE)
#define  TT_MODE_8B     (TTUF_MODE_8B << TTUF_V_MODE)
#define  TT_MODE_UC     (TTUF_MODE_UC << TTUF_V_MODE)
#define  TT_MODE_7P     (TTUF_MODE_7P << TTUF_V_MODE)
#define  TT_MODE_KSR    (TT_MODE_UC|TT_PAR_MARK)
/* 7 bit modes allow for an 8th bit parity mode */
#define TT_PAR          (TTUF_M_PAR << TTUF_V_PAR)
#define  TT_PAR_SPACE   (TTUF_PAR_SPACE << TTUF_V_PAR)
#define  TT_PAR_MARK    (TTUF_PAR_MARK  << TTUF_V_PAR)
#define  TT_PAR_EVEN    (TTUF_PAR_EVEN  << TTUF_V_PAR)
#define  TT_PAR_ODD     (TTUF_PAR_ODD   << TTUF_V_PAR)
/* TT_GET_MODE returns both the TT_MODE and TT_PAR fields 
   since they together are passed into sim_tt_inpcvt() */
#define TT_GET_MODE(x)  (((x) >> TTUF_V_MODE) & (TTUF_M_MODE | (TTUF_M_PAR << TTUF_W_MODE)))

/* Modem Control Bits */

#define TMXR_MDM_DTR        0x01    /* Data Terminal Ready */
#define TMXR_MDM_RTS        0x02    /* Request To Send     */
#define TMXR_MDM_DCD        0x04    /* Data Carrier Detect */
#define TMXR_MDM_RNG        0x08    /* Ring Indicator      */
#define TMXR_MDM_CTS        0x10    /* Clear To Send       */
#define TMXR_MDM_DSR        0x20    /* Data Set Ready      */
#define TMXR_MDM_INCOMING   (TMXR_MDM_DCD|TMXR_MDM_RNG|TMXR_MDM_CTS|TMXR_MDM_DSR)  /* Settable Modem Bits */
#define TMXR_MDM_OUTGOING   (TMXR_MDM_DTR|TMXR_MDM_RTS)  /* Settable Modem Bits */

/* Unit flags */

#define TMUF_V_NOASYNCH   (UNIT_V_UF + 12)              /* Asynch Disabled unit */
#define TMUF_NOASYNCH     (1u << TMUF_V_NOASYNCH)       /* This flag can be defined */
                                                        /* statically in a unit's flag field */
                                                        /* This will disable the unit from */
                                                        /* supporting asynchronmous mux behaviors */
/* Receive line speed limits */

#define TMLN_SPD_50_BPS     200000 /* usec per character */
#define TMLN_SPD_75_BPS     133333 /* usec per character */
#define TMLN_SPD_110_BPS     90909 /* usec per character */
#define TMLN_SPD_134_BPS     74626 /* usec per character */
#define TMLN_SPD_150_BPS     66666 /* usec per character */
#define TMLN_SPD_300_BPS     33333 /* usec per character */
#define TMLN_SPD_600_BPS     16666 /* usec per character */
#define TMLN_SPD_1200_BPS     8333 /* usec per character */
#define TMLN_SPD_1800_BPS     5555 /* usec per character */
#define TMLN_SPD_2000_BPS     5000 /* usec per character */
#define TMLN_SPD_2400_BPS     4166 /* usec per character */
#define TMLN_SPD_3600_BPS     2777 /* usec per character */
#define TMLN_SPD_4800_BPS     2083 /* usec per character */
#define TMLN_SPD_7200_BPS     1388 /* usec per character */
#define TMLN_SPD_9600_BPS     1041 /* usec per character */
#define TMLN_SPD_19200_BPS     520 /* usec per character */
#define TMLN_SPD_25000_BPS     400 /* usec per character */
#define TMLN_SPD_38400_BPS     260 /* usec per character */
#define TMLN_SPD_40000_BPS     250 /* usec per character */
#define TMLN_SPD_50000_BPS     200 /* usec per character */
#define TMLN_SPD_57600_BPS     173 /* usec per character */
#define TMLN_SPD_76800_BPS     130 /* usec per character */
#define TMLN_SPD_80000_BPS     125 /* usec per character */
#define TMLN_SPD_115200_BPS     86 /* usec per character */
