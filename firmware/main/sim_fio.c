/* sim_fio.c: simulator file I/O library

   Copyright (c) 1993-2008, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   03-Jun-11    MP      Simplified VMS 64b support and made more portable
   02-Feb-11    MP      Added sim_fsize_ex and sim_fsize_name_ex returning t_addr
                        Added export of sim_buf_copy_swapped and sim_buf_swap_data
   28-Jun-07    RMS     Added VMS IA64 support (from Norm Lastovica)
   10-Jul-06    RMS     Fixed linux conditionalization (from Chaskiel Grundman)
   15-May-06    RMS     Added sim_fsize_name
   21-Apr-06    RMS     Added FreeBSD large file support (from Mark Martinec)
   19-Nov-05    RMS     Added OS/X large file support (from Peter Schorn)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   17-Jul-04    RMS     Fixed bug in optimized sim_fread (reported by Scott Bailey)
   26-May-04    RMS     Optimized sim_fread (suggested by John Dundas)
   02-Jan-04    RMS     Split out from SCP

   This library includes:

   sim_finit         -       initialize package
   sim_fopen         -       open file
   sim_fread         -       endian independent read (formerly fxread)
   sim_fwrite        -       endian independent write (formerly fxwrite)
   sim_fseek         -       conditionally extended (>32b) seek (
   sim_fseeko        -       extended seek (>32b if available)
   sim_fsize         -       get file size
   sim_fsize_name    -       get file size of named file
   sim_fsize_ex      -       get file size as a t_offset
   sim_fsize_name_ex -       get file size as a t_offset of named file
   sim_buf_copy_swapped -    copy data swapping elements along the way
   sim_buf_swap_data -       swap data elements inplace in buffer
   sim_shmem_open            create or attach to a shared memory region
   sim_shmem_close           close a shared memory region


   sim_fopen and sim_fseek are OS-dependent.  The other routines are not.
   sim_fsize is always a 32b routine (it is used only with small capacity random
   access devices like fixed head disks and DECtapes).
*/

#include "sim_defs.h"
#include "scp.h"

t_bool sim_end;                     /* TRUE = little endian, FALSE = big endian */

#if defined(fprintf)                /* Make sure to only use the C rtl stream I/O routines */
#undef fprintf
#undef fputs
#undef fputc
#endif

#ifndef MAX
#define MAX(a,b)  (((a) >= (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b)  (((a) <= (b)) ? (a) : (b))
#endif

/* OS-independent, endian independent binary I/O package

   For consistency, all binary data read and written by the simulator
   is stored in little endian data order.  That is, in a multi-byte
   data item, the bytes are written out right to left, low order byte
   to high order byte.  On a big endian host, data is read and written
   from high byte to low byte.  Consequently, data written on a little
   endian system must be byte reversed to be usable on a big endian
   system, and vice versa.

   These routines are analogs of the standard C runtime routines
   fread and fwrite.  If the host is little endian, or the data items
   are size char, then the calls are passed directly to fread or
   fwrite.  Otherwise, these routines perform the necessary byte swaps.
   Sim_fread swaps in place, sim_fwrite uses an intermediate buffer.
*/

int32 sim_finit (void) {
	union {int32 i; char c[sizeof (int32)]; } end_test;
	end_test.i = 1;                                         /* test endian-ness */
	sim_end = (end_test.c[0] != 0);
	return sim_end;
}

void sim_buf_swap_data (void *bptr, size_t size, size_t count) {
	uint32 j;
	int32 k;
	unsigned char by, *sptr, *dptr;
	
	if (sim_end || (count == 0) || (size == sizeof (char)))
		return;
	for (j = 0, dptr = sptr = (unsigned char *) bptr;		/* loop on items */
		 j < count; j++) { 
		for (k = (int32)(size - 1); k >= (((int32) size + 1) / 2); k--) {
			by = *sptr;										/* swap end-for-end */
			*sptr++ = *(dptr + k);
			*(dptr + k) = by;
			}
		sptr = dptr = dptr + size;                          /* next item */
	}
}

FILE *sim_fopen (const char *file, const char *mode) {
	return fopen (file, mode);
}

size_t sim_fread (void *bptr, size_t size, size_t count, FILE *fptr) {
	size_t c;
	
	if ((size == 0) || (count == 0))						/* check arguments */
		return 0;
	c = fread (bptr, size, count, fptr);					/* read buffer */
	if (sim_end || (size == sizeof (char)) || (c == 0))		/* le, byte, or err? */
		return c;											/* done */
	sim_buf_swap_data(bptr, size, count);
	return c;
}

void sim_buf_copy_swapped (void *dbuf, const void *sbuf, size_t size, size_t count) {
	size_t j;
	int32 k;
	const unsigned char *sptr = (const unsigned char *)sbuf;
	unsigned char *dptr = (unsigned char *)dbuf;
	
	if (sim_end || (size == sizeof (char))) {
		memcpy (dptr, sptr, size * count);
		return;
		}
	for (j = 0; j < count; j++) {							/* loop on items */
		for (k = (int32)(size - 1); k >= 0; k--)
			*(dptr + k) = *sptr++;
		dptr = dptr + size;
		}
}

size_t sim_fwrite (const void *bptr, size_t size, size_t count, FILE *fptr) {
	size_t c, nelem, nbuf, lcnt, total;
	int32 i;
	const unsigned char *sptr;
	unsigned char *sim_flip;
	
	if ((size == 0) || (count == 0))						/* check arguments */
		return 0;
	if (sim_end || (size == sizeof (char)))					/* le or byte? */
		return fwrite (bptr, size, count, fptr);			/* done */
	sim_flip = (unsigned char *)malloc(FLIP_SIZE);
	if (!sim_flip)
		return 0;
	nelem = FLIP_SIZE / size;								/* elements in buffer */
	nbuf = count / nelem;									/* number buffers */
	lcnt = count % nelem;									/* count in last buf */
	if (lcnt) nbuf = nbuf + 1;
	else lcnt = nelem;
	total = 0;
	sptr = (const unsigned char *) bptr;					/* init input ptr */
	for (i = (int32)nbuf; i > 0; i--) {						/* loop on buffers */
		c = (i == 1)? lcnt: nelem;
		sim_buf_copy_swapped (sim_flip, sptr, size, c);
		sptr = sptr + size * count;
		c = fwrite (sim_flip, size, c, fptr);
		if (c == 0) {
			free(sim_flip);
			return total;
			}
		total = total + c;
		}
	free(sim_flip);
	return total;
}

/* Forward Declaration */

t_offset sim_ftell (FILE *st);

/* Get file size */

t_offset sim_fsize_ex (FILE *fp) {
	t_offset pos, sz;

	if (fp == NULL)
		return 0;
	pos = sim_ftell (fp);
	if (sim_fseeko (fp, 0, SEEK_END))
		return 0;
	sz = sim_ftell (fp);
	if (sim_fseeko (fp, pos, SEEK_SET))
		return 0;
	return sz;
}

t_offset sim_fsize_name_ex (const char *fname) {
	FILE *fp;
	t_offset sz;

	if ((fp = sim_fopen (fname, "rb")) == NULL)
		return 0;
	sz = sim_fsize_ex (fp);
	fclose (fp);
	return sz;
}

uint32 sim_fsize_name (const char *fname) {
	return (uint32)(sim_fsize_name_ex (fname));
}

uint32 sim_fsize (FILE *fp) {
	return (uint32)(sim_fsize_ex (fp));
}

/* OS-dependent routines */

/* Optimized file open */

int sim_fseeko (FILE *st, t_offset xpos, int origin)
{
return fseek (st, (long) xpos, origin);
}

t_offset sim_ftell (FILE *st)
{
return (t_offset)(ftell (st));
}

int sim_fseek (FILE *st, t_addr offset, int whence)
{
return sim_fseeko (st, (t_offset)offset, whence);
}

#include <unistd.h>
int sim_set_fsize (FILE *fptr, t_addr size) {
	return ftruncate(fileno(fptr), (off_t)size);
}

#include <sys/stat.h>
#include <fcntl.h>
#if HAVE_UTIME
#include <utime.h>
#endif

const char * sim_get_os_error_text (int Error) {
	return strerror (Error);
}

t_stat sim_copyfile (const char *source_file, const char *dest_file, t_bool overwrite_existing) {
	FILE *fIn = NULL, *fOut = NULL;
	t_stat st = SCPE_OK;
	char *buf = NULL;
	size_t bytes;

	fIn = sim_fopen (source_file, "rb");
	if (!fIn) {
	    st = sim_messagef (SCPE_ARG, "Can't open '%s' for input: %s\n", source_file, strerror (errno));
	    goto Cleanup_Return;
	}
	fOut = sim_fopen (dest_file, "wb");
	if (!fOut) {
	    st = sim_messagef (SCPE_ARG, "Can't open '%s' for output: %s\n", dest_file, strerror (errno));
	    goto Cleanup_Return;
	}
	buf = (char *)malloc (BUFSIZ);
	while ((bytes = fread (buf, 1, BUFSIZ, fIn)))
	    fwrite (buf, 1, bytes, fOut);
Cleanup_Return:
	free (buf);
	if (fIn) fclose (fIn);
	if (fOut) fclose (fOut);
	return st;
}

int sim_set_fifo_nonblock (FILE *fptr) {
	return -1;
}


t_stat sim_shmem_open (const char *name, size_t size, SHMEM **shmem, void **addr)
{
return SCPE_NOFNC;
}

void sim_shmem_close (SHMEM *shmem)
{
}

int32 sim_shmem_atomic_add (int32 *p, int32 v)
{
return -1;
}

t_bool sim_shmem_atomic_cas (int32 *ptr, int32 oldv, int32 newv) {
return FALSE;
}

char *sim_getcwd (char *buf, size_t buf_size) {
	return getcwd (buf, buf_size);
}

/*
 * Parsing and expansion of file names.
 *
 *    %~I%        - expands filepath value removing any surrounding quotes (" or ')
 *    %~fI%       - expands filepath value to a fully qualified path name
 *    %~pI%       - expands filepath value to a path only
 *    %~nI%       - expands filepath value to a file name only
 *    %~xI%       - expands filepath value to a file extension only
 *
 * The modifiers can be combined to get compound results:
 *
 *    %~pnI%      - expands filepath value to a path and name only
 *    %~nxI%      - expands filepath value to a file name and extension only
 *
 * In the above example above %I% can be replaced by other 
 * environment variables or numeric parameters to a DO command
 * invokation.
 */

char *sim_filepath_parts (const char *filepath, const char *parts) {
	size_t tot_len = 0, tot_size = 0;
	char *tempfilepath = NULL;
	char *fullpath = NULL, *result = NULL;
	char *c, *name, *ext;
	char chr;
	const char *p;
	char filesizebuf[32] = "";
	char filedatetimebuf[64] = "";

	if (((*filepath == '\'') || (*filepath == '"')) &&
	    (filepath[strlen (filepath) - 1] == *filepath)) {
	    size_t temp_size = 1 + strlen (filepath);
	
	    tempfilepath = (char *)malloc (temp_size);
	    if (tempfilepath == NULL)
	        return NULL;
	    strlcpy (tempfilepath, 1 + filepath, temp_size);
	    tempfilepath[strlen (tempfilepath) - 1] = '\0';
	    filepath = tempfilepath;
	    }
	if ((filepath[1] == ':')  ||
	    (filepath[0] == '/')  || 
	    (filepath[0] == '\\')){
	        tot_len = 1 + strlen (filepath);
	        fullpath = (char *)malloc (tot_len);
	        if (fullpath == NULL) {
	            free (tempfilepath);
	            return NULL;
	            }
	        strcpy (fullpath, filepath);
	    }
	else {
	    char dir[PATH_MAX+1] = "";
	    char *wd = sim_getcwd(dir, sizeof (dir));
	
	    if (wd == NULL) {
	        free (tempfilepath);
	        return NULL;
	        }
	    tot_len = 1 + strlen (filepath) + 1 + strlen (dir);
	    fullpath = (char *)malloc (tot_len);
	    if (fullpath == NULL) {
	        free (tempfilepath);
	        return NULL;
	        }
	    strlcpy (fullpath, dir, tot_len);
	    if ((dir[strlen (dir) - 1] != '/') &&       /* if missing a trailing directory separator? */
	        (dir[strlen (dir) - 1] != '\\'))
	        strlcat (fullpath, "/", tot_len);       /*  then add one */
	    strlcat (fullpath, filepath, tot_len);
	    }
	while ((c = strchr (fullpath, '\\')))           /* standardize on / directory separator */
	       *c = '/';
	if ((fullpath[1] == ':') && islower (fullpath[0]))
	    fullpath[0] = toupper (fullpath[0]);
	while ((c = strstr (fullpath + 1, "//")))       /* strip out redundant / characters (leaving the option for a leading //) */
	       memmove (c, c + 1, 1 + strlen (c + 1));
	while ((c = strstr (fullpath, "/./")))          /* strip out irrelevant /./ sequences */
	       memmove (c, c + 2, 1 + strlen (c + 2));
	while ((c = strstr (fullpath, "/../"))) {       /* process up directory climbing */
	    char *cl = c - 1;
	
	    while ((*cl != '/') && (cl > fullpath))
	        --cl;
	    if ((cl <= fullpath) ||                      /* Digest Leading /../ sequences */
	        ((fullpath[1] == ':') && (c == fullpath + 2)))
	        memmove (c, c + 3, 1 + strlen (c + 3)); /* and removing intervening elements */
	    else
	        if (*cl == '/')
	            memmove (cl, c + 3, 1 + strlen (c + 3));/* and removing intervening elements */
	        else
	            break;
	    }
	if (!strrchr (fullpath, '/'))
	    name = fullpath + strlen (fullpath);
	else
	    name = 1 + strrchr (fullpath, '/');
	ext = strrchr (name, '.');
	if (ext == NULL)
	    ext = name + strlen (name);
	tot_size = 0;
	if (*parts == '\0')             /* empty part specifier means strip only quotes */
	    tot_size = strlen (tempfilepath);
	if (strchr (parts, 't') || strchr (parts, 'z')) {
	    struct stat filestat;
	    struct tm *tm;
	
	    memset (&filestat, 0, sizeof (filestat));
	    (void)stat (fullpath, &filestat);
	    if (sizeof (filestat.st_size) == 4)
	        sprintf (filesizebuf, "%ld ", (long)filestat.st_size);
	    else
	        sprintf (filesizebuf, "%" LL_FMT "d ", (LL_TYPE)filestat.st_size);
	    tm = localtime (&filestat.st_mtime);
	    sprintf (filedatetimebuf, "%02d/%02d/%04d %02d:%02d %cM ", 1 + tm->tm_mon, tm->tm_mday, 1900 + tm->tm_year,
	                                                              tm->tm_hour % 12, tm->tm_min, (0 == (tm->tm_hour % 12)) ? 'A' : 'P');
	    }
	for (p = parts; *p; p++) {
	    switch (*p) {
	        case 'f':
	            tot_size += strlen (fullpath);
	            break;
	        case 'p':
	            tot_size += name - fullpath;
	            break;
	        case 'n':
	            tot_size += ext - name;
	            break;
	        case 'x':
	            tot_size += strlen (ext);
	            break;
	        case 't':
	            tot_size += strlen (filedatetimebuf);
	            break;
	        case 'z':
	            tot_size += strlen (filesizebuf);
	            break;
	        }
	    }
	result = (char *)malloc (1 + tot_size);
	*result = '\0';
	if (*parts == '\0')             /* empty part specifier means strip only quotes */
	    strlcat (result, filepath, 1 + tot_size);
	for (p = parts; *p; p++) {
	    switch (*p) {
	        case 'f':
	            strlcat (result, fullpath, 1 + tot_size);
	            break;
	        case 'p':
	            chr = *name;
	            *name = '\0';
	            strlcat (result, fullpath, 1 + tot_size);
	            *name = chr;
	            break;
	        case 'n':
	            chr = *ext;
	            *ext = '\0';
	            strlcat (result, name, 1 + tot_size);
	            *ext = chr;
	            break;
	        case 'x':
	            strlcat (result, ext, 1 + tot_size);
	            break;
	        case 't':
	            strlcat (result, filedatetimebuf, 1 + tot_size);
	            break;
	        case 'z':
	            strlcat (result, filesizebuf, 1 + tot_size);
	            break;
	        }
	    }
	free (fullpath);
	free (tempfilepath);
	return result;
}


#include <sys/stat.h>
#if defined (HAVE_GLOB)
#include <glob.h>
#else /* !defined (HAVE_GLOB) */
#include <dirent.h>
#if defined (HAVE_FNMATCH)
#include <fnmatch.h>
#endif
#endif /* defined (HAVE_GLOB) */

t_stat sim_dir_scan (const char *cptr, DIR_ENTRY_CALLBACK entry, void *context)
{
#if defined (HAVE_GLOB)
glob_t  paths;
#else
DIR *dir;
#endif
int found_count = 0;
struct stat filestat;
char *c;
char DirName[PATH_MAX + 1], WholeName[PATH_MAX + 1], WildName[PATH_MAX + 1];

memset (DirName, 0, sizeof(DirName));
memset (WholeName, 0, sizeof(WholeName));
strlcpy (WildName, cptr, sizeof(WildName));
cptr = WildName;
sim_trim_endspc (WildName);
c = sim_filepath_parts (cptr, "f");
strlcpy (WholeName, c, sizeof (WholeName));
free (c);
c = strrchr (WholeName, '/');
if (c) {
    memmove (DirName, WholeName, 1+c-WholeName);
    DirName[1+c-WholeName] = '\0';
    }
else
    DirName[0] = '\0';
cptr = WholeName;
#if defined (HAVE_GLOB)
memset (&paths, 0, sizeof (paths));
if (0 == glob (cptr, 0, NULL, &paths)) {
#else
dir = opendir(DirName[0] ? DirName : "/.");
if (dir) {
    struct dirent *ent;
#endif
    t_offset FileSize;
    char *FileName;
    const char *MatchName = 1 + strrchr (cptr, '/');
    char *p_name;
    struct tm *local;
#if defined (HAVE_GLOB)
    size_t i;
#endif

#if defined (HAVE_GLOB)
    for (i=0; i<paths.gl_pathc; i++) {
        FileName = (char *)malloc (1 + strlen (paths.gl_pathv[i]));
        sprintf (FileName, "%s", paths.gl_pathv[i]);
#else /* !defined (HAVE_GLOB) */
    while ((ent = readdir (dir))) {
#if defined (HAVE_FNMATCH)
        if (fnmatch(MatchName, ent->d_name, 0))
            continue;
#else /* !defined (HAVE_FNMATCH) */
        /* only match all names or exact name without fnmatch support */
        if ((strcmp(MatchName, "*") != 0) &&
            (strcmp(MatchName, ent->d_name) != 0))
            continue;
#endif /* defined (HAVE_FNMATCH) */
        FileName = (char *)malloc (1 + strlen (DirName) + strlen (ent->d_name));
        sprintf (FileName, "%s%s", DirName, ent->d_name);
#endif /* defined (HAVE_GLOB) */
        p_name = FileName + strlen (DirName);
        memset (&filestat, 0, sizeof (filestat));
        (void)stat (FileName, &filestat);
        FileSize = (t_offset)((filestat.st_mode & S_IFDIR) ? 0 : sim_fsize_name_ex (FileName));
        entry (DirName, p_name, FileSize, &filestat, context);
        free (FileName);
        ++found_count;
        }
#if defined (HAVE_GLOB)
    globfree (&paths);
#else
    closedir (dir);
#endif
    }
else
    return SCPE_ARG;
if (found_count)
    return SCPE_OK;
else
    return SCPE_ARG;
}
