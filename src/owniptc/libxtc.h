/**
 * This file was imported from the iptables sources.
 * Copyright (C) 1999-2008 Netfilter Core Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef _LIBXTC_H
#define _LIBXTC_H
/* Library which manipulates filtering rules. */

#include "ipt_kernel_headers.h"
#include <linux/netfilter/x_tables.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XT_MIN_ALIGN
/* xt_entry has pointers and u_int64_t's in it, so if you align to
   it, you'll also align to any crazy matches and targets someone
   might write */
#define XT_MIN_ALIGN (__alignof__(struct xt_entry))
#endif

#ifndef XT_ALIGN
#define XT_ALIGN(s) (((s) + ((XT_MIN_ALIGN)-1)) & ~((XT_MIN_ALIGN)-1))
#endif

typedef char xt_chainlabel[32];

#define XTC_LABEL_ACCEPT  "ACCEPT"
#define XTC_LABEL_DROP    "DROP"
#define XTC_LABEL_QUEUE   "QUEUE"
#define XTC_LABEL_RETURN  "RETURN"


#ifdef __cplusplus
}
#endif

#endif /* _LIBXTC_H */
