#include "sim_defs.h"
#include "sim_rev.h"
#include "sim_disk.h"
#include "sim_ether.h"
#include "sim_card.h"
#include "sim_term.h"
#include "sim_serial.h"
#include "sim_sock.h"
#include "sim_frontpanel.h"
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <setjmp.h>
#include "wifi_if.h"

FILE *sim_deb = NULL;                                   /* debug file */

t_value (*sim_vm_pc_value) (void) = NULL;
t_bool (*sim_vm_is_subroutine_call) (t_addr **ret_addrs) = NULL;
uint32 sim_brk_dflt = 0;
uint32 sim_brk_types = 0;
BRKTYPTAB *sim_brk_type_desc = NULL;
const char **sim_clock_precalibrate_commands = NULL;
t_value *sim_eval = NULL;
extern uint32_t sim_emax;
int32 sim_switches=0;
int32_t sim_switch_number=0;
int sim_internal_device_count=0;
DEVICE **sim_internal_devices;

const t_value width_mask[] = { 0,
    0x1, 0x3, 0x7, 0xF,
    0x1F, 0x3F, 0x7F, 0xFF,
    0x1FF, 0x3FF, 0x7FF, 0xFFF,
    0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF,
    0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF,
    0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF,
    0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF,
    0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
    };



t_stat sprint_val (char *buffer, t_value val, uint32 radix,
    uint32 width, uint32 format)
{
#define MAX_WIDTH ((int) ((CHAR_BIT * sizeof (t_value) * 4 + 3)/3))
t_value owtest, wtest;
t_bool negative = FALSE;
int32 d, digit, ndigits, commas = 0;
char dbuf[MAX_WIDTH + 1];

if (((format == PV_LEFTSIGN) || (format == PV_RCOMMASIGN)) &&
    (0 > (t_svalue)val)) {
    val = (t_value)(-((t_svalue)val));
    negative = TRUE;
    }
for (d = 0; d < MAX_WIDTH; d++)
    dbuf[d] = (format == PV_RZRO)? '0': ' ';
dbuf[MAX_WIDTH] = 0;
d = MAX_WIDTH;
do {
    d = d - 1;
    digit = (int32) (val % radix);
    val = val / radix;
    dbuf[d] = (char)((digit <= 9)? '0' + digit: 'A' + (digit - 10));
    } while ((d > 0) && (val != 0));
if (negative && (format == PV_LEFTSIGN))
    dbuf[--d] = '-';

switch (format) {
    case PV_LEFT:
    case PV_LEFTSIGN:
        break;
    case PV_RCOMMA:
    case PV_RCOMMASIGN:
        for (digit = 0; digit < MAX_WIDTH; digit++)
            if (dbuf[digit] != ' ')
                break;
        ndigits = MAX_WIDTH - digit;
        commas = (ndigits - 1)/3;
        for (digit=0; digit<ndigits-3; digit++)
            dbuf[MAX_WIDTH + (digit - ndigits) - (ndigits - digit - 1)/3] = dbuf[MAX_WIDTH + (digit - ndigits)];
        for (digit=1; digit<=commas; digit++)
            dbuf[MAX_WIDTH - (digit * 4)] = ',';
        d = d - commas;
        if (negative && (format == PV_RCOMMASIGN))
            dbuf[--d] = '-';
        if (width > MAX_WIDTH) {
            if (!buffer)
                return width;
            sprintf (buffer, "%*s", -((int)width), dbuf);
            return SCPE_OK;
            }
        else
            if (width > 0)
                d = MAX_WIDTH - width;
        break;
    case PV_RZRO:
    case PV_RSPC:
        wtest = owtest = radix;
        ndigits = 1;
        while ((wtest < width_mask[width]) && (wtest >= owtest)) {
            owtest = wtest;
            wtest = wtest * radix;
            ndigits = ndigits + 1;
            }
        if ((MAX_WIDTH - (ndigits + commas)) < d)
            d = MAX_WIDTH - (ndigits + commas);
        break;
    }
if (!buffer)
    return strlen(dbuf+d);
*buffer = '\0';
if (width < strlen(dbuf+d))
    return SCPE_IOERR;
strcpy(buffer, dbuf+d);
return SCPE_OK;
}


t_stat fprint_val (FILE *stream, t_value val, uint32 radix, uint32 width, uint32 format) {
	char dbuf[MAX_WIDTH + 1];
	
	if (!stream) return sprint_val (NULL, val, radix, width, format);
	if (width > MAX_WIDTH) width = MAX_WIDTH;
	sprint_val (dbuf, val, radix, width, format);
	if (fprintf (stream, "%s", dbuf) < 0) return SCPE_IOERR;
	return SCPE_OK;
}



static void _sim_vdebug (uint32 dbits, DEVICE* dptr, UNIT *uptr, const char* fmt, va_list arglist) {
	if (sim_deb && dptr && ((dptr->dctrl | (uptr ? uptr->dctrl : 0)) & dbits)) {
		vfprintf(sim_deb, fmt, arglist);
	}
}

void _sim_debug_unit (uint32 dbits, UNIT *uptr, const char* fmt, ...) {
	DEVICE *dptr = (uptr ? uptr->dptr : NULL);
	
	if (sim_deb && (((dptr ? dptr->dctrl : 0) | (uptr ? uptr->dctrl : 0)) & dbits)) {
		va_list arglist;
		va_start (arglist, fmt);
		_sim_vdebug (dbits, dptr, uptr, fmt, arglist);
		va_end (arglist);
	}
}


void _sim_debug_device (uint32 dbits, DEVICE* dptr, const char* fmt, ...) {
	if (sim_deb && dptr && (dptr->dctrl & dbits)) {
		va_list arglist;
		va_start (arglist, fmt);
		_sim_vdebug (dbits, dptr, NULL, fmt, arglist);
		va_end (arglist);
	}
}

t_stat sim_messagef (t_stat stat, const char* fmt, ...) {
	va_list arglist;
	va_start (arglist, fmt);
	vfprintf(sim_deb, fmt, arglist);
	va_end (arglist);
	return SCPE_OK;
}

void fprint_fields (FILE *stream, t_value before, t_value after, BITFIELD* bitdefs) {
	const char *debug_bstates = "01_^";
	int32 i, fields, offset;
	uint32 value, beforevalue, mask;

	for (fields=offset=0; bitdefs[fields].name; ++fields) {
		if (bitdefs[fields].offset == 0xffffffff) {		/* fixup uninitialized offsets */
			bitdefs[fields].offset = offset;
		}
		offset += bitdefs[fields].width;
	}
	for (i = fields-1; i >= 0; i--) {					/* print xlation, transition */
		if (bitdefs[i].name[0] == '\0')
			continue;
		if ((bitdefs[i].width == 1) && (bitdefs[i].valuenames == NULL)) {
			int off = ((after >> bitdefs[i].offset) & 1) + (((before ^ after) >> bitdefs[i].offset) & 1) * 2;
			fprintf(stream, "%s%c ", bitdefs[i].name, debug_bstates[off]);
		} else {
			const char *delta = "";
	
			mask = 0xFFFFFFFF >> (32-bitdefs[i].width);
			value = (uint32)((after >> bitdefs[i].offset) & mask);
			beforevalue = (uint32)((before >> bitdefs[i].offset) & mask);
			if (value < beforevalue)
				delta = "_";
			if (value > beforevalue)
				delta = "^";
			if (bitdefs[i].valuenames)
				fprintf(stream, "%s=%s%s ", bitdefs[i].name, delta, bitdefs[i].valuenames[value]);
			else
				if (bitdefs[i].format) {
					fprintf(stream, "%s=%s", bitdefs[i].name, delta);
					fprintf(stream, bitdefs[i].format, value);
					fprintf(stream, " ");
				} else {
					fprintf(stream, "%s=%s0x%X ", bitdefs[i].name, delta, value);
				}
		}
	}
}

/* get_uint             unsigned number

   Inputs:
        cptr    =       pointer to input string
        radix   =       input radix
        max     =       maximum acceptable value
        *status =       pointer to error status
   Outputs:
        val     =       value
*/

t_value get_uint (const char *cptr, uint32 radix, t_value max, t_stat *status) {
	t_value val;
	char *tptr;
	
	*status = SCPE_OK;
	val = strtol ((CONST char *)cptr, &tptr, radix);
	if ((cptr == tptr) || (val > max)) {
		*status = SCPE_ARG;
	} else {
		while (isspace (*tptr)) tptr++;
		if (toupper (*tptr) == 'K') {
			val *= 1000;
			++tptr;
		} else {
			if (toupper (*tptr) == 'M') {
				val *= 1000000;
				++tptr;
			}
		}
		if ((*tptr != 0) || (val > max)) *status = SCPE_ARG;
	}
	return val;
}



/* Prints state of a register: bit translation + state (0,1,_,^)
   indicating the state and transition of the bit and bitfields. States:
   0=steady(0->0), 1=steady(1->1), _=falling(1->0), ^=rising(0->1) */

void sim_debug_bits_hdr(uint32 dbits, DEVICE* dptr, const char *header, 
			BITFIELD* bitdefs, uint32 before, uint32 after, int terminate) {
	if (sim_deb && dptr && (dptr->dctrl & dbits)) {
		if (header) fprintf(sim_deb, "%s: ", header);
		fprint_fields (sim_deb, (t_value)before, (t_value)after, bitdefs);	/* print xlation, transition */
		if (terminate) fprintf(sim_deb, "\n");
	}
}

void sim_debug_bits(uint32 dbits, DEVICE* dptr, BITFIELD* bitdefs,
    uint32 before, uint32 after, int terminate) {
	sim_debug_bits_hdr(dbits, dptr, NULL, bitdefs, before, after, terminate);
}

DEVICE *find_dev (const char *cptr) {
	for (int i = 0; (sim_devices[i] != NULL); i++) {
		if (strcmp(sim_devices[i]->name, cptr)==0) {
			return sim_devices[i];
		}
	}
	for (int i = 0; i<sim_internal_device_count && sim_internal_devices[i]; i++) {
		if (strcmp(sim_internal_devices[i]->name, cptr)==0) {
			return sim_devices[i];
		}
	}

//	printf("find_dev %s failed\n", cptr);
	return NULL;
}

DEVICE *find_unit (const char *cptr, UNIT **uptr) {
	printf("find_unit: %s STUB\n", cptr);
	return NULL;
}

DEVICE *find_dev_from_unit (UNIT *uptr) {
	if (uptr == NULL) return NULL;
	if (uptr->dptr) return uptr->dptr;
	DEVICE *dptr;
	for (int i = 0; (dptr = sim_devices[i]) != NULL; i++) {
		for (int j = 0; j < dptr->numunits; j++) {
			if (uptr == (dptr->units + j)) {
				uptr->dptr = dptr;
				return dptr;
			}
		}
	}
	for (int i = 0; i<sim_internal_device_count; i++) {
		dptr = sim_internal_devices[i];
		for (int j = 0; j < dptr->numunits; j++) {
			if (uptr == (dptr->units + j)) {
				uptr->dptr = dptr;
				return dptr;
			}
		}
	}
	return NULL;
}



/* find_reg             find register matching input string

   Inputs:
        cptr    =       pointer to input string
        optr    =       pointer to output pointer (can be null)
        dptr    =       pointer to device
   Outputs:
        result  =       pointer to register, NULL if error
        *optr   =       pointer to next character in input string
*/

const REG *find_reg (CONST char *cptr, CONST char **optr, DEVICE *dptr) {
	CONST char *tptr;
	const REG *rptr;
	size_t slnt;

	if ((cptr == NULL) || (dptr == NULL) || (dptr->registers == NULL)) return NULL;
	tptr = cptr;
	do {
		tptr++;
	} while (isalnum (*tptr) || (*tptr == '*') || (*tptr == '_') || (*tptr == '.'));
	slnt = tptr - cptr;
	for (rptr = dptr->registers; rptr->name != NULL; rptr++) {
		if ((slnt == strlen (rptr->name)) && (strncmp (cptr, rptr->name, slnt) == 0)) {
			if (optr != NULL)
			*optr = tptr;
			return rptr;
		}
	}
	return NULL;
}

t_stat reset_all (uint32 start) {
	DEVICE *dptr;
	uint32 i;
	t_stat reason;
	for (i = 0; i < start; i++) {
		if (sim_devices[i] == NULL) return SCPE_IERR;
	}
	for (i = start; (dptr = sim_devices[i]) != NULL; i++) {
		if (dptr->reset != NULL) {
			reason = dptr->reset (dptr);
			if (reason != SCPE_OK) return reason;
		}
	}
	for (i = 0; sim_internal_device_count && (dptr = sim_internal_devices[i]); ++i) {
		if (dptr->reset != NULL) {
			reason = dptr->reset (dptr);
			if (reason != SCPE_OK) return reason;
		}
	}
	return SCPE_OK;
}

t_stat reset_all_p (uint32 start) {
	return reset_all(start);
}


const size_t size_map[] = { sizeof (int8),
    sizeof (int8), sizeof (int16), sizeof (int32), sizeof (int32)
};

#define SZ_D(dp) (size_map[((dp)->dwidth + CHAR_BIT - 1) / CHAR_BIT])
#define SZ_R(rp) \
    (size_map[((rp)->width + (rp)->offset + CHAR_BIT - 1) / CHAR_BIT])
#define SZ_LOAD(sz,v,mb,j) \
    if (sz == sizeof (uint8)) v = *(((uint8 *) mb) + ((uint32) j)); \
    else if (sz == sizeof (uint16)) v = *(((uint16 *) mb) + ((uint32) j)); \
    else v = *(((uint32 *) mb) + ((uint32) j));
#define SZ_STORE(sz,v,mb,j) \
    if (sz == sizeof (uint8)) *(((uint8 *) mb) + ((uint32) j)) = (uint8) v; \
    else if (sz == sizeof (uint16)) *(((uint16 *) mb) + ((uint32) j)) = (uint16) v; \
    else *(((uint32 *) mb) + ((uint32) j)) = v;

/* Get address routine

   Inputs:
        addr    =       address to examine
        dptr    =       pointer to device
        uptr    =       pointer to unit
   Outputs: (sim_eval is an implicit output)
        return  =       error status
*/
t_stat get_aval (t_addr addr, DEVICE *dptr, UNIT *uptr)	{
	int32 i;
	t_value mask;
	t_addr j, loc;
	size_t sz;
	t_stat reason = SCPE_OK;
	
	if ((dptr == NULL) || (uptr == NULL)) return SCPE_IERR;
	mask = width_mask[dptr->dwidth];
	for (i = 0; i < sim_emax; i++){
		sim_eval[i] = 0;
	}
	for (i = 0, j = addr; i < sim_emax; i++, j = j + dptr->aincr) {
		if (dptr->examine != NULL) {
			reason = dptr->examine (&sim_eval[i], j, uptr, sim_switches);
			if (reason != SCPE_OK) break;
		} else {
			if (!(uptr->flags & UNIT_ATT)) return SCPE_UNATT;
			if ((uptr->dynflags & UNIT_NO_FIO) || (uptr->fileref == NULL)) return SCPE_NOFNC;
			if ((uptr->flags & UNIT_FIX) && (j >= uptr->capac)) {
				reason = SCPE_NXM;
				break;
			}
			sz = SZ_D (dptr);
			loc = j / dptr->aincr;
			if (uptr->flags & UNIT_BUF) {
				SZ_LOAD (sz, sim_eval[i], uptr->filebuf, loc);
			} else {
				if (sim_fseek (uptr->fileref, (t_addr)(sz * loc), SEEK_SET)) {
					clearerr (uptr->fileref);
					reason = SCPE_IOERR;
					break;
				}
				(void)sim_fread (&sim_eval[i], sz, 1, uptr->fileref);
				if ((feof (uptr->fileref)) &&
				   !(uptr->flags & UNIT_FIX)) {
					reason = SCPE_EOF;
					break;
				}
				else if (ferror (uptr->fileref)) {
					clearerr (uptr->fileref);
					reason = SCPE_IOERR;
					break;
				}
			}
		}
		sim_eval[i] = sim_eval[i] & mask;
	}
	if ((reason != SCPE_OK) && (i == 0)) return reason;
	return SCPE_OK;
}

const char *sim_dname (DEVICE *dptr) {
	return (dptr ? (dptr->lname? dptr->lname: dptr->name) : "");
}

const char *sim_uname (UNIT *uptr) {

	if (!uptr) return "";
	if (uptr->uname) return uptr->uname;
	return "";
}

const char *sim_error_text (t_stat stat) {
	static char msgbuf[64];
	stat &= ~(SCPE_KFLAG|SCPE_BREAK|SCPE_NOMESSAGE);        /* remove any flags */
	if (stat == SCPE_OK) return "No Error";
	sprintf(msgbuf, "Error %d", stat);
	return msgbuf;
}

t_stat attach_err (UNIT *uptr, t_stat stat) {
	free (uptr->filename);
	uptr->filename = NULL;
	return stat;
}

/* Attach unit to file */

t_stat attach_unit (UNIT *uptr, CONST char *cptr) {
	DEVICE *dptr;
	t_bool open_rw = FALSE;
	if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
	if ((dptr = find_dev_from_unit (uptr)) == NULL) return SCPE_NOATT;
	uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char)); /* alloc name buf */
	if (uptr->filename == NULL) return SCPE_MEM;
	strncpy (uptr->filename, cptr, CBUFSIZE);				/* save name */
	uptr->fileref = sim_fopen (cptr, "rb+");		/* open r/w */
	printf("Ref %p\n", uptr->fileref );
	if (uptr->fileref == NULL) {					/* open fail? */
//		if ((errno == EROFS) || (errno == EACCES)) {/* read only? */
			return attach_err (uptr, SCPE_OPENERR); /* yes, error */
//		} else {
//			open_rw = TRUE;
//		}												/* end else */
	}
	if (uptr->flags & UNIT_BUFABLE) {						/* buffer? */
		uint32 cap = ((uint32) uptr->capac) / dptr->aincr;	/* effective size */
	printf("Ref %p\n", uptr->fileref );
		if (uptr->flags & UNIT_MUSTBUF) uptr->filebuf = calloc (cap, SZ_D (dptr));		/* allocate */
	printf("Ref %p\n", uptr->fileref );
		if (uptr->filebuf == NULL) {
			printf("Eek! Couldn't allocate memory for disk buffer (%d bytes)\n", cap*SZ_D(dptr));
			return attach_err (uptr, SCPE_MEM);				/* error */
		}
		sim_messagef (SCPE_OK, "%s: buffering file in memory\n", sim_dname (dptr));
	printf("Ref %p\n", uptr->fileref );
		uptr->hwmark = (uint32)sim_fread(uptr->filebuf, SZ_D (dptr), cap, uptr->fileref);
		uptr->flags = uptr->flags | UNIT_BUF;				/* set buffered */
	}
	uptr->flags = uptr->flags | UNIT_ATT;
	uptr->pos = 0;
	return SCPE_OK;
}

/* Detach unit from file */
t_stat detach_unit (UNIT *uptr) {
	DEVICE *dptr;

	if (uptr == NULL) return SCPE_IERR;
	if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
	if (!(uptr->flags & UNIT_ATT)) {						/* not attached? */
		if (sim_switches & SIM_SW_REST) {						/* restoring? */
			return SCPE_OK;									/* allow detach */
		} else {
			return SCPE_UNATT;								/* complain */
		}
	}
	if ((dptr = find_dev_from_unit (uptr)) == NULL) return SCPE_OK;
	if ((uptr->flags & UNIT_BUF) && (uptr->filebuf)) {
		uint32 cap = (uptr->hwmark + dptr->aincr - 1) / dptr->aincr;
		if (uptr->hwmark && ((uptr->flags & UNIT_RO) == 0)) {
			sim_messagef (SCPE_OK, "%s: writing buffer to file\n", sim_dname (dptr));
			rewind (uptr->fileref);
			sim_fwrite (uptr->filebuf, SZ_D (dptr), cap, uptr->fileref);
			if (ferror (uptr->fileref)) sim_printf ("%s: I/O error - %s", sim_dname (dptr), strerror (errno));
		}
		if (uptr->flags & UNIT_MUSTBUF) {					/* dyn alloc? */
			free (uptr->filebuf);							/* free buf */
			uptr->filebuf = NULL;
		}
		uptr->flags = uptr->flags & ~UNIT_BUF;
	}
	uptr->flags = uptr->flags & ~(UNIT_ATT | ((uptr->flags & UNIT_ROABLE) ? UNIT_RO : 0));
	free (uptr->filename);
	uptr->filename = NULL;
	if (uptr->fileref) {						/* Only close open file */
		if (fclose (uptr->fileref) == EOF) {
			uptr->fileref = NULL;
			return SCPE_IOERR;
		}
		uptr->fileref = NULL;
	}
	return SCPE_OK;
}

const char *sim_set_uname (UNIT *uptr, const char *uname) {
	free (uptr->uname);
	return uptr->uname = strcpy ((char *)malloc (1 + strlen (uname)), uname);
}



/* get_glyph            get next glyph (force upper case)
   get_glyph_nc         get next glyph (no conversion)
   get_glyph_quoted     get next glyph (potentially enclosed in quotes, no conversion)
   get_glyph_cmd        get command glyph (force upper case, extract leading !)
   get_glyph_gen        get next glyph (general case)

   Inputs:
        iptr        =   pointer to input string
        optr        =   pointer to output string
        mchar       =   optional end of glyph character
        uc          =   TRUE for convert to upper case (_gen only)
        quote       =   TRUE to allow quote enclosing values (_gen only)
        escape_char =   optional escape character within quoted strings (_gen only)

   Outputs
        result      =   pointer to next character in input string
*/
static const char *get_glyph_gen (const char *iptr, char *optr, char mchar, t_bool uc, t_bool quote, char escape_char) {
	t_bool quoting = FALSE;
	t_bool escaping = FALSE;
	t_bool got_quoted = FALSE;
	char quote_char = 0;

	while ((*iptr != 0) && (!got_quoted) &&
				((quote && quoting) || ((isspace (*iptr) == 0) && (*iptr != mchar)))) {
		if (quote) {
			if (quoting) {
				if (!escaping) {
					if (*iptr == escape_char) {
						escaping = TRUE;
					} else {
						if (*iptr == quote_char) {
							quoting = FALSE;
							got_quoted = TRUE;
						}
					}
				} else {
					escaping = FALSE;
				}
			} else {
				if ((*iptr == '"') || (*iptr == '\'')) {
					quoting = TRUE;
					quote_char = *iptr;
				}
			}
		}
		if (islower (*iptr) && uc) {
			*optr = (char)toupper (*iptr);
		} else {
			 *optr = *iptr;
		}
		iptr++; optr++;
	}
	if (mchar && (*iptr == mchar)) iptr++;				/* skip input terminator */
	*optr = 0;									/* terminate result string */
	while (isspace (*iptr)) iptr++;					/* absorb additional input spaces */
	return iptr;
}

CONST char *get_glyph (const char *iptr, char *optr, char mchar) {
	return (CONST char *)get_glyph_gen (iptr, optr, mchar, TRUE, FALSE, 0);
}

CONST char *get_glyph_nc (const char *iptr, char *optr, char mchar) {
	return (CONST char *)get_glyph_gen (iptr, optr, mchar, FALSE, FALSE, 0);
}

CONST char *get_glyph_quoted (const char *iptr, char *optr, char mchar) {
	return (CONST char *)get_glyph_gen (iptr, optr, mchar, FALSE, TRUE, '\\');
}

CONST char *get_glyph_cmd (const char *iptr, char *optr) {
	/* Tolerate "!subprocess" vs. requiring "! subprocess" */
	if ((iptr[0] == '!') && (!isspace(iptr[1]))) {
		strcpy (optr, "!");						/* return ! as command glyph */
		return (CONST char *)(iptr + 1);		/* and skip over the leading ! */
	}
	return (CONST char *)get_glyph_gen (iptr, optr, 0, TRUE, FALSE, 0);
}

char *sim_trim_endspc (char *cptr) {
	char *tptr;

	tptr = cptr + strlen (cptr);
	while ((--tptr >= cptr) && isspace (*tptr)) *tptr = 0;
	return cptr;
}


CTAB *find_ctab (CTAB *tab, const char *gbuf) {
	if (!tab) return NULL;
	for (; tab->name != NULL; tab++) {
		if (strcmp(gbuf, tab->name) == 0) return tab;
	}
	return NULL;
}

const char *sim_fmt_secs (double seconds) {
	static char buf[60];
	char frac[16] = "";
	const char *sign = "";
	double val = seconds;
	double days, hours, mins, secs, msecs, usecs;

	if (val == 0.0) return "";
	if (val < 0.0) {
		sign = "-";
		val = -val;
	}
	days = floor (val / (24.0*60.0*60.0));
	val -= (days * 24.0*60.0*60.0);
	hours = floor (val / (60.0*60.0));
	val -= (hours * 60.0 * 60.0);
	mins = floor (val / 60.0);
	val -= (mins * 60.0);
	secs = floor (val);
	val -= secs;
	val *= 1000.0;
	msecs = floor (val);
	val -= msecs;
	val *= 1000.0;
	usecs = floor (val+0.5);
	if (usecs == 1000.0) {
	    usecs = 0.0;
	    msecs += 1;
	    }
	if ((msecs > 0.0) || (usecs > 0.0)) {
	    sprintf (frac, ".%03.0f%03.0f", msecs, usecs);
	    while (frac[strlen (frac) - 1] == '0')
	        frac[strlen (frac) - 1] = '\0';
	    if (strlen (frac) == 1)
	        frac[0] = '\0';
	    }
	if (days > 0)
	    sprintf (buf, "%s%.0f day%s %02.0f:%02.0f:%02.0f%s hour%s", sign, days, (days != 1)? "s" : "", hours, mins, secs, frac, (days == 1) ? "s" : "");
	else
	    if (hours > 0)
	        sprintf (buf, "%s%.0f:%02.0f:%02.0f%s hour", sign, hours, mins, secs, frac);
	    else
	        if (mins > 0)
	            sprintf (buf, "%s%.0f:%02.0f%s minute", sign, mins, secs, frac);
	        else
	            if (secs > 0)
	                sprintf (buf, "%s%.0f%s second", sign, secs, frac);
	            else
	                if (msecs > 0) {
	                    if (usecs > 0)
	                        sprintf (buf, "%s%.0f.%s msec", sign, msecs, frac+4);
	                    else
	                        sprintf (buf, "%s%.0f msec", sign, msecs);
	                    }
	                else
	                    sprintf (buf, "%s%.0f usec", sign, usecs);
	if (0 != strncmp ("1 ", buf, 2))
	    strcpy (&buf[strlen (buf)], "s");
	return buf;
}



/* strlcat() and strlcpy() are not available on all platforms */
/* Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com> */
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t sim_strlcat(char *dst, const char *src, size_t size)
{
char *d = dst;
const char *s = src;
size_t n = size;
size_t dlen;

/* Find the end of dst and adjust bytes left but don't go past end */
while (n-- != 0 && *d != '\0')
    d++;
dlen = d - dst;
n = size - dlen;

if (n == 0)
    return (dlen + strlen(s));
while (*s != '\0') {
    if (n != 1) {
        *d++ = *s;
        n--;
        }
    s++;
    }
*d = '\0';

return (dlen + (s - src));          /* count does not include NUL */
}

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t sim_strlcpy (char *dst, const char *src, size_t size)
{
char *d = dst;
const char *s = src;
size_t n = size;

/* Copy as many bytes as will fit */
if (n != 0) {
    while (--n != 0) {
        if ((*d++ = *s++) == '\0')
            break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (size != 0)
            *d = '\0';              /* NUL-terminate dst */
        while (*s++)
            ;
        }
return (s - src - 1);               /* count does not include NUL */
}

const char *sim_fmt_numeric (double number) {
	static char buf[60];
	char tmpbuf[60];
	size_t len;
	uint32 c;
	char *p;
	
	sprintf (tmpbuf, "%.0f", number);
	len = strlen (tmpbuf);
	for (c=0, p=buf; c < len; c++) {
		if ((c > 0) && (isdigit (tmpbuf[c])) && (0 == ((len - c) % 3))) {
			*(p++) = ',';
		}
		*(p++) = tmpbuf[c];
	}
	*p = '\0';
	return buf;
}


t_stat sim_register_internal_device (DEVICE *dptr) {
	uint32 i;
	
	for (i = 0; i < sim_internal_device_count; i++) {
		if (sim_internal_devices[i] == dptr) return SCPE_OK;
	}
	for (i = 0; (sim_devices[i] != NULL); i++) {
		if (sim_devices[i] == dptr) return SCPE_OK;
	}
	++sim_internal_device_count;
	sim_internal_devices = (DEVICE **)realloc(sim_internal_devices, (sim_internal_device_count+1)*sizeof(*sim_internal_devices));
	sim_internal_devices[sim_internal_device_count-1] = dptr;
	sim_internal_devices[sim_internal_device_count] = NULL;
	return SCPE_OK;
}

//Hacky way to resolve/set modifiers on a device
t_stat set_mod(DEVICE *dev, UNIT *unit, const char *mod, const char *cp, void *dp) {
	MTAB *p=dev->modifiers;
	while (p->mask) {
//		printf("Modifier '%s' '%s'\n", p->pstring?p->pstring:"NULL", p->mstring?p->mstring:"NULL");
		if ((p->pstring && strcmp(mod, p->pstring)==0) || (p->mstring && strcmp(mod, p->mstring)==0)) {
			return p->valid(unit, p->match, cp, dp);
		}
		p++;
	}
	printf("scp.c:set_mod(): Modifier not found on device: %s\n", mod);
	return 0;
}

#ifndef ESP_PLATFORM
#define RA92_DISK_PATH "media/root.dsk"
#define RX_FLOPPY_PATH "../../spiffs/floppy.dsk"
#else
#define RA92_DISK_PATH "/sdcard/rq.dsk"
#define RX_FLOPPY_PATH "/spiffs/floppy.dsk"
#endif

//1 to run 2.11BSD
#define RUN_BSD 1

int main (int argc, char *argv[]) {
	t_stat stat=SCPE_OK;
	sim_deb=stderr;
	sim_init_sock ();										/* init socket capabilities */
	sim_finit ();											/* init fio package */

/*
	sim_time = 0;
	stop_cpu = FALSE;
	sim_interval = 0;
	noqueue_time = 0;
	sim_clock_queue = QUEUE_LIST_END;
	sim_is_running = FALSE;
	sim_log = NULL;
	if (sim_emax <= 0)
		sim_emax = 1;
*/
	if (sim_timer_init ()) {
		fprintf (stderr, "Fatal timer initialization error\n");
		return EXIT_FAILURE;
		}
//	sim_register_internal_device (&sim_scp_dev);
//	sim_register_internal_device (&sim_expect_dev);
//	sim_register_internal_device (&sim_step_dev);
//	sim_register_internal_device (&sim_flush_dev);
//	sim_register_internal_device (&sim_runlimit_dev);
	
	if ((stat = sim_ttinit ()) != SCPE_OK) {
		fprintf (stderr, "Fatal terminal initialization error\n%s\n",
			sim_error_text (stat));
		return EXIT_FAILURE;
		}
	if ((sim_eval = (t_value *) calloc (sim_emax, sizeof (t_value))) == NULL) {
		fprintf (stderr, "Unable to allocate examine buffer\n");
		return EXIT_FAILURE;
	};
//	if (sim_dflt_dev == NULL)								/* if no default */
//		sim_dflt_dev = sim_devices[0];

	//Set main memory capacity...
	DEVICE *cpudev=find_dev("CPU");
#if RUN_BSD
	cpudev->units[0].capac=3.5*1024*1024;
#else
	//Assign less memory as the floppy needs to be read into memory entirely
	cpudev->units[0].capac=512*1024;
#endif

	if ((stat = reset_all (0)) != SCPE_OK) {
		fprintf (stderr, "Fatal simulator initialization error\n%s\n",
			sim_error_text (stat));
		return EXIT_FAILURE;
	}
//	if ((stat = sim_brk_init ()) != SCPE_OK) {
//		fprintf (stderr, "Fatal breakpoint table initialization error\n%s\n",
//			sim_error_text (stat));
//		return EXIT_FAILURE;
//	}
	/* always check for register definition problems */
//	sim_sanity_check_register_declarations ();
	sim_timer_precalibrate_execution_rate ();
//	show_version (stdnul, NULL, NULL, 1, NULL);				/* Quietly set SIM_OSTYPE */
	
	//Set up network device
	DEVICE *dev=find_dev("XQ");
	//dev->dctrl=0xfffffffff //enable for debugging info
	char mac[32];
	wifi_if_get_mac(mac);
	set_mod(dev, dev->units, "MAC", mac, NULL);
	set_mod(dev, dev->units, "TYPE", mac, "DEQNA");
	dev->attach(dev->units, "WIFI");

	//find rq device, boot off it
#if RUN_BSD
	dev=find_dev("RQ");
	set_mod(dev, dev->units, "RA92", NULL, NULL);
	printf("Attach RA92 disk to RQ\n");
	stat=dev->attach(dev->units, RA92_DISK_PATH);
	if (stat!=SCPE_OK) printf("Attach failed...\n");
	printf("Boot from RQ\n");
#else
	printf("Find RX\n");
	dev=find_dev("RX");
	printf("Attach disk to RX\n");
	stat=attach_unit(dev->units, RX_FLOPPY_PATH);
	if (stat!=SCPE_OK) printf("Attach failed...\n");
	printf("Boot from RX\n");
#endif
	stat=dev->boot(0, dev);
	if (stat!=SCPE_OK) printf("Boot failed...\n");

	sim_set_throt(1, "90%");

	printf("Main sim start\n");
	while(1) {
		stat=sim_instr();
		if (stat!=SCPE_OK) {
//			printf("Sim_instr exited with 0x%X\n", stat);
//			exit(0);
		}
	}
	
//	detach_all (0, TRUE);									/* close files */
	//sim_set_deboff (0, NULL);								/* close debug */
	//sim_set_logoff (0, NULL);								/* close log */
	//sim_set_notelnet (0, NULL);								/* close Telnet */
	//vid_close ();											/* close video */
	//sim_ttclose ();											/* close console */
	//AIO_CLEANUP;											/* Asynch I/O */
	sim_cleanup_sock ();									/* cleanup sockets */
	//fclose (stdnul);										/* close bit bucket file handle */
	return 0;
}
