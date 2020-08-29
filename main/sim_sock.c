/* sim_sock.c: OS-dependent socket routines

   Copyright (c) 2001-2010, Robert M Supnik

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

   15-Oct-12    MP      Added definitions needed to detect possible tcp 
                        connect failures
   25-Sep-12    MP      Reworked for RFC3493 interfaces supporting IPv6 and IPv4
   22-Jun-10    RMS     Fixed types in sim_accept_conn (from Mark Pizzolato)
   19-Nov-05    RMS     Added conditional for OpenBSD (from Federico G. Schwindt)
   16-Aug-05    RMS     Fixed spurious SIGPIPE signal error in Unix
   14-Apr-05    RMS     Added WSAEINPROGRESS test (from Tim Riker)
   09-Jan-04    RMS     Fixed typing problem in Alpha Unix (found by Tim Chapman)
   17-Apr-03    RMS     Fixed non-implemented version of sim_close_sock
                        (found by Mark Pizzolato)
   17-Dec-02    RMS     Added sim_connect_socket, sim_create_socket
   08-Oct-02    RMS     Revised for .NET compatibility
   22-Aug-02    RMS     Changed calling sequence for sim_accept_conn
   22-May-02    RMS     Added OS2 EMX support from Holger Veit
   06-Feb-02    RMS     Added VMS support from Robert Alan Byer
   16-Sep-01    RMS     Added Macintosh support from Peter Schorn
   02-Sep-01    RMS     Fixed UNIX bugs found by Mirian Lennox and Tom Markson
*/

#ifdef  __cplusplus
extern "C" {
#endif

#include "sim_sock.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(AF_INET6) && defined(_WIN32)
#include <ws2tcpip.h>
#endif

#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif

#ifndef WSAAPI
#define WSAAPI
#endif

#if defined(SHUT_RDWR) && !defined(SD_BOTH)
#define SD_BOTH SHUT_RDWR
#endif

#ifndef   NI_MAXHOST
#define   NI_MAXHOST 1025
#endif

/* OS dependent routines

   sim_master_sock      create master socket
   sim_connect_sock     connect a socket to a remote destination
   sim_connect_sock_ex  connect a socket to a remote destination
   sim_accept_conn      accept connection
   sim_read_sock        read from socket
   sim_write_sock       write from socket
   sim_close_sock       close socket
   sim_setnonblock      set socket non-blocking
*/

/* First, all the non-implemented versions */


void sim_init_sock (void)
{
}

void sim_cleanup_sock (void)
{
}

SOCKET sim_master_sock_ex (const char *hostport, int *parse_status, int opt_flags)
{
return INVALID_SOCKET;
}

SOCKET sim_connect_sock_ex (const char *sourcehostport, const char *hostport, const char *default_host, const char *default_port, int opt_flags)
{
return INVALID_SOCKET;
}

int sim_read_sock (SOCKET sock, char *buf, int nbytes)
{
return -1;
}

int sim_write_sock (SOCKET sock, const char *msg, int nbytes)
{
return 0;
}

void sim_close_sock (SOCKET sock)
{
return;
}

int sim_parse_addr (const char *cptr, char *host, size_t host_len, const char *default_host, char *port, size_t port_len, const char *default_port, const char *validate_addr) {
	return -1;
}

int sim_check_conn (SOCKET sock, int rd) {
	return -1;
}

SOCKET sim_accept_conn_ex (SOCKET master, char **connectaddr, int opt_flags) {
	return -1;
}

int sim_getnames_sock (SOCKET sock, char **socknamebuf, char **peernamebuf) {
	return 0;
}

#ifdef  __cplusplus
}
#endif
