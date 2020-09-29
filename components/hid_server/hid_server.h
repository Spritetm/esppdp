
/* Copyright (c) 2020, Peter Barrett
**
** Permission to use, copy, modify, and/or distribute this software for
** any purpose with or without fee is hereby granted, provided that the
** above copyright notice and this permission notice appear in all copies.
**
** THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
** WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR
** BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
** OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
** WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
** ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
** SOFTWARE.
*/

#ifndef hid_server_hpp
#define hid_server_hpp

#include <stdio.h>
#include <stdbool.h>
#include "hci_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// minimal hid interface
int hid_init(const char* local_name);
int hid_update();
int hid_close();
int hid_get(uint8_t* dst, int dst_len); //

void gui_msg(const char* msg);                                  // temporarily display a msg

#ifdef __cplusplus
}
#endif

#endif /* hid_server_hpp */
