/**
 * collectd - src/tape.h
 * Copyright (C) 2005  Scott Garrett
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
 *   Scott Garrett <sgarrett at technomancer.com>
 **/

#ifndef TAPESTATS_H
#define TAPESTATS_H

#include "collectd.h"
#include "common.h"

#ifndef COLLECT_TAPE
#if defined(KERNEL_LINUX) || defined(HAVE_LIBKSTAT)
#define COLLECT_TAPE 1
#else
#define COLLECT_TAPE 0
#endif
#endif /* !defined(COLLECT_TAPE) */

#endif /* TAPESTATS_H */
