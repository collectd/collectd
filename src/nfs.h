/**
 * collectd - src/nfs.h
 * Copyright (C) 2005  Jason Pepas
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
 *   Jason Pepas <cell at ices.utexas.edu>
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef NFS_H
#define NFS_H

#include "collectd.h"

#ifndef COLLECT_NFS
#if defined(KERNEL_LINUX)
#define COLLECT_NFS 1
#else
#define COLLECT_NFS 0
#endif
#endif /* !defined(COLLECT_NFS) */

#endif /* NFS_H */
