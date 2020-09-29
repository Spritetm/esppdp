/* sim_disk.c: simulator disk support library

   Copyright (c) 2011, Mark Pizzolato

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.



   This is the place which hides processing of various disk formats,
   as well as OS-specific direct hardware access.

   25-Jan-11    MP      Initial Implemementation

Public routines:

   sim_disk_attach           attach disk unit
   sim_disk_attach_ex        attach disk unit extended parameters
   sim_disk_detach           detach disk unit
   sim_disk_attach_help      help routine for attaching disks
   sim_disk_rdsect           read disk sectors
   sim_disk_rdsect_a         read disk sectors asynchronously
   sim_disk_wrsect           write disk sectors
   sim_disk_wrsect_a         write disk sectors asynchronously
   sim_disk_unload           unload or detach a disk as needed
   sim_disk_reset            reset unit
   sim_disk_wrp              TRUE if write protected
   sim_disk_isavailable      TRUE if available for I/O
   sim_disk_size             get disk size
   sim_disk_set_fmt          set disk format
   sim_disk_show_fmt         show disk format
   sim_disk_set_capac        set disk capacity
   sim_disk_show_capac       show disk capacity
   sim_disk_set_async        enable asynchronous operation
   sim_disk_clr_async        disable asynchronous operation
   sim_disk_data_trace       debug support
   sim_disk_test             unit test routine

Internal routines:

   sim_os_disk_open_raw      platform specific open raw device
   sim_os_disk_close_raw     platform specific close raw device
   sim_os_disk_size_raw      platform specific raw device size
   sim_os_disk_unload_raw    platform specific disk unload/eject
   sim_os_disk_rdsect        platform specific read sectors
   sim_os_disk_wrsect        platform specific write sectors

   sim_vhd_disk_open         platform independent open virtual disk file
   sim_vhd_disk_create       platform independent create virtual disk file
   sim_vhd_disk_create_diff  platform independent create differencing virtual disk file
   sim_vhd_disk_close        platform independent close virtual disk file
   sim_vhd_disk_size         platform independent virtual disk size
   sim_vhd_disk_rdsect       platform independent read virtual disk sectors
   sim_vhd_disk_wrsect       platform independent write virtual disk sectors


*/

#include "sim_defs.h"
#include "sim_disk.h"
#include "sim_ether.h"
#include <ctype.h>
#include <sys/stat.h>

#define disk_ctx up8                        /* Field in Unit structure which points to the disk_context */

static uint32
NtoHl(uint32 value)
{
uint8 *l = (uint8 *)&value;

if (sim_end)
    return l[3] | (l[2]<<8) | (l[1]<<16) | (l[0]<<24);
return value;
}

struct disk_context {
    t_offset            container_size;     /* Size of the data portion (of the pseudo disk) */
    uint32              sector_size;        /* Disk Sector Size (of the pseudo disk) */
    uint32              capac_factor;       /* Units of Capacity (8 = quadword, 2 = word, 1 = byte) */
    uint32              xfer_element_size;  /* Disk Bus Transfer size (1 - byte, 2 - word, 4 - longword) */
    uint32              storage_sector_size;/* Sector size of the containing storage */
	DEVICE              *dptr;              /* Device for unit (access to debug flags) */
    uint32              dbit;               /* debugging bit */
    };


t_stat sim_disk_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
	printf("sim_disk_set_fmt %s\n", cptr);
	return SCPE_OK;
}

t_stat sim_disk_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
	fprintf (st, "sim_disk_show_fmt unimplemented\n");
	return SCPE_OK;
}

/* Set disk capacity */

t_stat sim_disk_set_capac (UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
	printf("sim_disk_set_capac unimpl\n");
	return SCPE_OK;
}

/* Show disk capacity */

t_stat sim_disk_show_capac (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
	fprintf (st, "sim_disk_show_capac unimplemented\n");
	return SCPE_OK;
}

/* Test for available */

t_bool sim_disk_isavailable (UNIT *uptr) {
	struct disk_context *ctx;
	t_bool ret=TRUE;
	if (!(uptr->flags & UNIT_ATT)) ret=FALSE;
	printf("sim_disk_isavailable %d\n", ret);
	return ret;
}

/* Test for write protect */

t_bool sim_disk_wrp (UNIT *uptr) {
	return (uptr->flags & DKUF_WRP)? TRUE: FALSE;
}

/* Get Disk size */

t_offset sim_disk_size (UNIT *uptr) {
	struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
	t_offset physical_size, filesystem_size;

	if ((uptr->flags & UNIT_ATT) == 0) return (t_offset)-1;
	physical_size = ctx->container_size;
	filesystem_size = physical_size;
	printf("sim_disk_size: phys %d fs %d\n", (int)physical_size, (int)filesystem_size);
	if ((filesystem_size == (t_offset)-1) || (filesystem_size < physical_size)) return physical_size;
	return filesystem_size;
}

/* Enable asynchronous operation */

t_stat sim_disk_set_async (UNIT *uptr, int latency) {
	char *msg = "Disk: can't operate asynchronously\r\n";
	sim_printf ("%s", msg);
	return SCPE_NOFNC;
}

/* Disable asynchronous operation */

t_stat sim_disk_clr_async (UNIT *uptr) {
	return SCPE_NOFNC;
}

/* Read Sectors */

t_stat sim_disk_rdsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects) {
	t_offset da;
	uint32 err, tbc;
	size_t i;
	struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
//	printf("sim_disk_rdsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);

	da = ((t_offset)lba) * ctx->sector_size;
	tbc = sects * ctx->sector_size;
	if (sectsread) *sectsread = 0;

	while (tbc) {
		size_t sectbytes;

		err = fseek(uptr->fileref, da, SEEK_SET);			 /* set pos */
		if (err) return SCPE_IOERR;
		i = fread(buf, 1, tbc, uptr->fileref);
		if (i < tbc) memset (&buf[i], 0, tbc-i);
		if (sectsread) *sectsread += i / ctx->sector_size;
		sectbytes = (i / ctx->sector_size) * ctx->sector_size;
		err = ferror (uptr->fileref);
		if (err) return SCPE_IOERR;
		tbc -= sectbytes;
		if ((tbc == 0) || (i == 0)) return SCPE_OK;
		da += sectbytes;
		buf += sectbytes;
	}
	return SCPE_OK;
}

t_stat sim_disk_rdsect_a (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectsread, t_seccnt sects, DISK_PCALLBACK callback) {
	t_stat r=sim_disk_rdsect(uptr, lba, buf, sectsread, sects);
	callback(uptr, r);
	return SCPE_OK;
}

/* Write Sectors */

t_stat sim_disk_wrsect (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects) {
	t_offset da;
	uint32 err, tbc;
	size_t i;
	struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

//	printf("_sim_disk_wrsect(unit=%d, lba=0x%X, sects=%d)\n", (int)(uptr - ctx->dptr->units), lba, sects);
	
	da = ((t_offset)lba) * ctx->sector_size;
	tbc = sects * ctx->sector_size;
	if (sectswritten) *sectswritten = 0;
	err = fseek(uptr->fileref, da, SEEK_SET);          /* set pos */
	if (err) return SCPE_IOERR;
	i = fwrite(buf, ctx->xfer_element_size, tbc/ctx->xfer_element_size, uptr->fileref);
	if (sectswritten) {
		*sectswritten += (t_seccnt)((i * ctx->xfer_element_size + ctx->sector_size - 1)/ctx->sector_size);
	}
	err = ferror (uptr->fileref);
	if (err) return SCPE_IOERR;
	return SCPE_OK;
}

t_stat sim_disk_wrsect_a (UNIT *uptr, t_lba lba, uint8 *buf, t_seccnt *sectswritten, t_seccnt sects, DISK_PCALLBACK callback) {
	t_stat r=sim_disk_wrsect(uptr, lba, buf, sectswritten, sects);
	callback(uptr, r);
	return SCPE_OK;
}

t_stat sim_disk_unload (UNIT *uptr) {
	struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;
	fclose(uptr->fileref);  /* remove/eject disk */
	return SCPE_OK;
}


static t_stat get_disk_footer (UNIT *uptr) {
	return SCPE_OK;
}

static t_stat store_disk_footer (UNIT *uptr, const char *dtype) {
	return SCPE_OK;
}

t_stat sim_disk_attach (UNIT *uptr, const char *cptr, size_t sector_size, size_t xfer_element_size, t_bool dontchangecapac,
                        uint32 dbit, const char *dtype, uint32 pdp11tracksize, int completion_delay)
{
return sim_disk_attach_ex (uptr, cptr, sector_size, xfer_element_size, dontchangecapac, dbit, dtype, pdp11tracksize, completion_delay, NULL);
}

t_stat sim_disk_attach_ex (UNIT *uptr, const char *cptr, size_t sector_size, size_t xfer_element_size, t_bool dontchangecapac,
                           uint32 dbit, const char *dtype, uint32 pdp11tracksize, int completion_delay, const char **drivetypes) {
	struct disk_context *ctx;
	t_offset container_size, filesystem_size, current_unit_size;
	size_t tmp_size = 1;
	if (uptr->flags & UNIT_DIS) return SCPE_UDIS;
	if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
	DEVICE *dptr;
	if ((dptr = find_dev_from_unit (uptr)) == NULL) return SCPE_NOATT;

	uptr->filename = (char *) calloc (CBUFSIZE, sizeof (char));/* alloc name buf */
	uptr->disk_ctx = ctx = (struct disk_context *)calloc(1, sizeof(struct disk_context));
	if ((uptr->filename == NULL) || (uptr->disk_ctx == NULL))  return SCPE_MEM;
	strncpy (uptr->filename, cptr, CBUFSIZE);               /* save name */
	ctx->sector_size = (uint32)sector_size;                 /* save sector_size */
	ctx->capac_factor = ((dptr->dwidth / dptr->aincr) >= 32) ? 8 : ((dptr->dwidth / dptr->aincr) == 16) ? 2 : 1; /* save capacity units (quadword: 8, word: 2, byte: 1) */
	ctx->xfer_element_size = (uint32)xfer_element_size;     /* save xfer_element_size */
	ctx->dptr = dptr;                                       /* save DEVICE pointer */
	ctx->dbit = dbit;                                       /* save debug bit */
	sim_debug_unit (ctx->dbit, uptr, "sim_disk_attach(unit=%d,filename='%s')\n", (int)(uptr - ctx->dptr->units), uptr->filename);
	ctx->storage_sector_size = (uint32)sector_size;         /* Default */
    uptr->fileref = fopen(cptr, "rb+");                 /* open r/w */
    if (uptr->fileref == NULL) return SCPE_OPENERR;
	fseek(uptr->fileref, 0, SEEK_END);
	ctx->container_size=ftell(uptr->fileref)/sector_size;

	uptr->flags |= UNIT_ATT;
	uptr->pos = 0;

	filesystem_size = sim_disk_size (uptr);
	container_size = sim_disk_size (uptr);
	current_unit_size = ((t_offset)uptr->capac)*ctx->capac_factor*((dptr->flags & DEV_SECTORS) ? 512 : 1);
	printf("sim_disk_attach(unit=%d,filename='%s') OK\n", (int)(uptr - ctx->dptr->units), uptr->filename);
	return SCPE_OK;
}

t_stat sim_disk_detach (UNIT *uptr) {
	struct disk_context *ctx;

	if (uptr == NULL) return SCPE_IERR;
	if (!(uptr->flags & UNIT_ATT)) return SCPE_UNATT;

	ctx = (struct disk_context *)uptr->disk_ctx;

	printf("sim_disk_detach(unit=%d,filename='%s')\n", (int)(uptr - ctx->dptr->units), uptr->filename);

	if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
	if (!(uptr->flags & UNIT_ATT)) return SCPE_OK;
	if (NULL == find_dev_from_unit (uptr)) return SCPE_OK;

	uptr->flags &= ~(UNIT_ATT | UNIT_RO);
	uptr->dynflags &= ~(UNIT_NO_FIO | UNIT_DISK_CHK);
	free(uptr->filename);
	uptr->filename = NULL;
	uptr->fileref = NULL;
	free(uptr->disk_ctx);
	uptr->disk_ctx = NULL;
	uptr->io_flush = NULL;
	fclose(uptr->fileref);

	return SCPE_OK;
}

t_stat sim_disk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) {
	return SCPE_OK;
}

t_bool sim_disk_vhd_support (void) {
	return 0;
}

t_bool sim_disk_raw_support (void) {
	return 0;
}

t_stat sim_disk_reset (UNIT *uptr) {
	struct disk_context *ctx = (struct disk_context *)uptr->disk_ctx;

	if (!(uptr->flags & UNIT_ATT))                          /* attached? */
		return SCPE_OK;

	sim_debug_unit (ctx->dbit, uptr, "sim_disk_reset(unit=%d)\n", (int)(uptr - ctx->dptr->units));

	return SCPE_OK;
}

t_stat sim_disk_perror (UNIT *uptr, const char *msg) {
	int saved_errno = errno;
	if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
	perror (msg);
	sim_printf ("%s %s: %s\n", sim_uname(uptr), msg, sim_get_os_error_text (saved_errno));
	return SCPE_OK;
}

t_stat sim_disk_clearerr (UNIT *uptr) {
	if (!(uptr->flags & UNIT_ATTABLE)) return SCPE_NOATT;
	return SCPE_OK;
}

t_stat sim_disk_pdp11_bad_block (UNIT *uptr, int32 sec, int32 wds) {
    return SCPE_NOFNC;
}

void sim_disk_data_trace(UNIT *uptr, const uint8 *data, size_t lba, size_t len, const char* txt, int detail, uint32 reason) {
/*
	DEVICE *dptr = find_dev_from_unit (uptr);
	if (sim_deb && ((uptr->dctrl | dptr->dctrl) & reason)) {
		char pos[32];
		sprintf (pos, "lbn: %08X ", (unsigned int)lba);
		sim_data_trace(dptr, uptr, (detail ? data : NULL), pos, len, txt, reason);
	}
*/
}


t_stat sim_disk_info_cmd (int32 flag, CONST char *cptr) {
	return SCPE_NOFNC;
}

t_stat sim_disk_test (DEVICE *dptr) {
	return SCPE_NOFNC;
}
