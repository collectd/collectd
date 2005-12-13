/**
 * collectd - src/cpufreq.c
 * Copyright (C) 2005  Peter Holik
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
 *   Peter Holik <peter at holik.at>
 **/

#ifndef CPUFREQ_H
#define CPUFREQ_H

#include "collectd.h"
#include "common.h"

#ifndef COLLECT_CPUFREQ
#if defined(KERNEL_LINUX)
#define COLLECT_CPUFREQ 1
#else
#define COLLECT_CPUFREQ 0
#endif
#endif /* !defined(COLLECT_CPUFREQ) */

#endif /* CPUFREQ_H */
