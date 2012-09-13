/**
 * collectd - src/utils_cmd_flush.h
 * Copyright (C) 2008  Sebastian Harl
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
 *
 * Author:
 *   Sebastian "tokkee" Harl <sh at tokkee.org>
 **/

#ifndef UTILS_CMD_FLUSH_H
#define UTILS_CMD_FLUSH_H 1

#include <stdio.h>

int handle_flush (FILE *fh, char *buffer);

#endif /* UTILS_CMD_FLUSH_H */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

