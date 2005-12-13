/**
 * collectd - src/ping.h
 * Copyright (C) 2005  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef COLLECTD_PING_H
#define COLLECTD_PING_H

#include "collectd.h"
#include "common.h"

#ifndef COLLECT_PING
#if defined(HAVE_NETINET_IN_H)
#define COLLECT_PING 1
#else
#define COLLECT_PING 0
#endif /* defined(HAVE_NETINET_IN_H) */
#endif /* !defined(COLLECT_PING) */

#if COLLECT_PING

#define MAX_PINGHOSTS 32

#endif /* COLLECT_PING */
#endif
