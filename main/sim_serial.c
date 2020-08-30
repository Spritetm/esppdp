/* sim_serial.c: OS-dependent serial port routines

   Copyright (c) 2008, J. David Bryan, Mark Pizzolato

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   The author gratefully acknowledges the assistance of Holger Veit with the
   UNIX-specific code and testing.

   07-Oct-08    JDB     [serial] Created file
   22-Apr-12    MP      Adapted from code originally written by J. David Bryan


   This module provides OS-dependent routines to access serial ports on the host
   machine.  The terminal multiplexer library uses these routines to provide
   serial connections to simulated terminal interfaces.

   Currently, the module supports Windows and UNIX.  Use on other systems
   returns error codes indicating that the functions failed, inhibiting serial
   port support in SIMH.

   The following routines are provided:

     sim_open_serial        open a serial port
     sim_config_serial      change baud rate and character framing configuration
     sim_control_serial     manipulate and/or return the modem bits on a serial port
     sim_read_serial        read from a serial port
     sim_write_serial       write to a serial port
     sim_close_serial       close a serial port
     sim_show_serial        shows the available host serial ports


   The calling sequences are as follows:


   SERHANDLE sim_open_serial (char *name)
   --------------------------------------

   The serial port referenced by the OS-dependent "name" is opened.  If the open
   is successful, and "name" refers to a serial port on the host system, then a
   handle to the port is returned.  If not, then the value INVALID_HANDLE is
   returned.


   t_stat sim_config_serial (SERHANDLE port, const char *config)
   -------------------------------------------------------------

   The baud rate and framing parameters (character size, parity, and number of
   stop bits) of the serial port associated with "port" are set.  If any
   "config" field value is unsupported by the host system, or if the combination
   of values (e.g., baud rate and number of stop bits) is unsupported, SCPE_ARG
   is returned.  If the configuration is successful, SCPE_OK is returned.


   sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
   -------------------------------------------------------------------------------------------------

   The DTR and RTS line of the serial port is set or cleared as indicated in 
   the respective bits_to_set or bits_to_clear parameters.  If the 
   incoming_bits parameter is not NULL, then the modem status bits DCD, RNG, 
   DSR and CTS are returned.

   If unreasonable or nonsense bits_to_set or bits_to_clear bits are 
   specified, then the return status is SCPE_ARG;
   If an error occurs, SCPE_IOERR is returned.


   int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
   ----------------------------------------------------------------------------

   A non-blocking read is issued for the serial port indicated by "port" to get
   at most "count" bytes into the string "buffer".  If a serial line break was
   detected during the read, the variable pointed to by "brk" is set to 1.  If
   the read is successful, the actual number of characters read is returned.  If
   no characters were available, then the value 0 is returned.  If an error
   occurs, then the value -1 is returned.


   int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
   ------------------------------------------------------------------

   A write is issued to the serial port indicated by "port" to put "count"
   characters from "buffer".  If the write is successful, the actual number of
   characters written is returned.  If an error occurs, then the value -1 is
   returned.


   void sim_close_serial (SERHANDLE port)
   --------------------------------------

   The serial port indicated by "port" is closed.


   int sim_serial_devices (int max, SERIAL_LIST* list)
   ---------------------------------------------------

   enumerates the available host serial ports


   t_stat sim_show_serial (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, const void* desc)
   ---------------------------------

   displays the available host serial ports

*/


#include "sim_defs.h"
#include "sim_serial.h"

#include <ctype.h>

#define SER_DEV_NAME_MAX     256                        /* maximum device name size */
#define SER_DEV_DESC_MAX     256                        /* maximum device description size */
#define SER_DEV_CONFIG_MAX    64                        /* maximum device config size */
#define SER_MAX_DEVICE        64                        /* maximum serial devices */

typedef struct serial_list {
    char    name[SER_DEV_NAME_MAX];
    char    desc[SER_DEV_DESC_MAX];
    } SERIAL_LIST;

typedef struct serial_config {                          /* serial port configuration */
    uint32 baudrate;                                    /* baud rate */
    uint32 charsize;                                    /* character size in bits */
    char   parity;                                      /* parity (N/O/E/M/S) */
    uint32 stopbits;                                    /* 0/1/2 stop bits (0 implies 1.5) */
    } SERCONFIG;

static int       sim_serial_os_devices (int max, SERIAL_LIST* list);
static SERHANDLE sim_open_os_serial    (char *name);
static void      sim_close_os_serial   (SERHANDLE port);
static t_stat    sim_config_os_serial  (SERHANDLE port, SERCONFIG config);


static struct open_serial_device {
    SERHANDLE port;
    TMLN *line;
    char name[SER_DEV_NAME_MAX];
    char config[SER_DEV_CONFIG_MAX];
    } *serial_open_devices = NULL;
static int serial_open_device_count = 0;

static struct open_serial_device *_get_open_device (SERHANDLE port)
{
int i;

for (i=0; i<serial_open_device_count; ++i)
    if (serial_open_devices[i].port == port)
        return &serial_open_devices[i];
return NULL;
}

static struct open_serial_device *_get_open_device_byname (const char *name)
{
int i;

for (i=0; i<serial_open_device_count; ++i)
    if (0 == strcmp(name, serial_open_devices[i].name))
        return &serial_open_devices[i];
return NULL;
}

static struct open_serial_device *_serial_add_to_open_list (SERHANDLE port, TMLN *line, const char *name, const char *config)
{
serial_open_devices = (struct open_serial_device *)realloc(serial_open_devices, (++serial_open_device_count)*sizeof(*serial_open_devices));
memset(&serial_open_devices[serial_open_device_count-1], 0, sizeof(serial_open_devices[serial_open_device_count-1]));
serial_open_devices[serial_open_device_count-1].port = port;
serial_open_devices[serial_open_device_count-1].line = line;
strlcpy(serial_open_devices[serial_open_device_count-1].name, name, sizeof(serial_open_devices[serial_open_device_count-1].name));
if (config)
    strlcpy(serial_open_devices[serial_open_device_count-1].config, config, sizeof(serial_open_devices[serial_open_device_count-1].config));
return &serial_open_devices[serial_open_device_count-1];
}

static void _serial_remove_from_open_list (SERHANDLE port)
{
int i, j;

for (i=0; i<serial_open_device_count; ++i)
    if (serial_open_devices[i].port == port) {
        for (j=i+1; j<serial_open_device_count; ++j)
            serial_open_devices[j-1] = serial_open_devices[j];
        --serial_open_device_count;
        break;
        }
}

/* Generic error message handler.

   This routine should be called for unexpected errors.  Some error returns may
   be expected, e.g., a "file not found" error from an "open" routine.  These
   should return appropriate status codes to the caller, allowing SCP to print
   an error message if desired, rather than printing this generic error message.
*/

static void sim_error_serial (const char *routine, int error)
{
sim_printf ("Serial: %s fails with error %d\n", routine, error);
return;
}

/* Used when sorting a list of serial port names */
static int _serial_name_compare (const void *pa, const void *pb)
{
const SERIAL_LIST *a = (const SERIAL_LIST *)pa;
const SERIAL_LIST *b = (const SERIAL_LIST *)pb;

return strcmp(a->name, b->name);
}

static int sim_serial_devices (int max, SERIAL_LIST *list)
{
int i, j, ports = sim_serial_os_devices(max, list);

/* Open ports may not show up in the list returned by sim_serial_os_devices 
   so we add the open ports to the list removing duplicates before sorting 
   the resulting list */

for (i=0; i<serial_open_device_count; ++i) {
    for (j=0; j<ports; ++j)
        if (0 == strcmp(serial_open_devices[i].name, list[j].name))
            break;
    if (j<ports)
        continue;
    if (ports >= max)
        break;
    strcpy(list[ports].name, serial_open_devices[i].name);
    strcpy(list[ports].desc, serial_open_devices[i].config);
    ++ports;
    }
if (ports) /* Order the list returned alphabetically by the port name */
    qsort (list, ports, sizeof(list[0]), _serial_name_compare);
return ports;
}

static char* sim_serial_getname (int number, char* name)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);

if (count <= number)
    return NULL;
strcpy(name, list[number].name);
return name;
}

static char* sim_serial_getname_bydesc (char* desc, char* name)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);
int i;
size_t j=strlen(desc);

for (i=0; i<count; i++) {
    int found = 1;
    size_t k = strlen(list[i].desc);

    if (j != k)
        continue;
    for (k=0; k<j; k++)
        if (tolower(list[i].desc[k]) != tolower(desc[k]))
            found = 0;
    if (found == 0)
        continue;

    /* found a case-insensitive description match */
    strcpy(name, list[i].name);
    return name;
    }
/* not found */
return NULL;
}

static char* sim_serial_getname_byname (char* name, char* temp)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);
size_t n;
int i, found;

found = 0;
n = strlen(name);
for (i=0; i<count && !found; i++) {
    if ((n == strlen(list[i].name)) &&
        (strncasecmp(name, list[i].name, n) == 0)) {
        found = 1;
        strcpy(temp, list[i].name); /* only case might be different */
        }
    }
return (found ? temp : NULL);
}

char* sim_serial_getdesc_byname (char* name, char* temp)
{
SERIAL_LIST  list[SER_MAX_DEVICE];
int count = sim_serial_devices(SER_MAX_DEVICE, list);
size_t n;
int i, found;

found = 0;
n = strlen(name);
for (i=0; i<count && !found; i++) {
    if ((n == strlen(list[i].name)) &&
        (strncasecmp(name, list[i].name, n) == 0)) {
        found = 1;
        strcpy(temp, list[i].desc);
        }
    }
  return (found ? temp : NULL);
}

t_stat sim_show_serial (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char* desc)
{
return SCPE_OK;
}

SERHANDLE sim_open_serial (char *name, TMLN *lp, t_stat *stat) {
	printf("Stub: sim_open_serial %s\n", name);
	return INVALID_HANDLE;
}

void sim_close_serial (SERHANDLE port) {
}

t_stat sim_config_serial  (SERHANDLE port, CONST char *sconfig) {
	printf("Stub: sim_config_serial\n");
	return SCPE_OK;
}


/* Non-implemented stubs */

/* Enumerate the available serial ports. */

static int sim_serial_os_devices (int max, SERIAL_LIST* list)
{
return 0;
}

/* Open a serial port */

static SERHANDLE sim_open_os_serial (char *name)
{
return INVALID_HANDLE;
}


/* Configure a serial port */

static t_stat sim_config_os_serial (SERHANDLE port, SERCONFIG config)
{
return SCPE_IERR;
}


/* Control a serial port */

t_stat sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits)
{
return SCPE_NOFNC;
}


/* Read from a serial port */

int32 sim_read_serial (SERHANDLE port, char *buffer, int32 count, char *brk)
{
return -1;
}


/* Write to a serial port */

int32 sim_write_serial (SERHANDLE port, char *buffer, int32 count)
{
return -1;
}


/* Close a serial port */

static void sim_close_os_serial (SERHANDLE port)
{
}


