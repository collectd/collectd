/**
 * collectd - src/utils_debug.h
 * Copyright (C) 2005  Niki W. Waibel
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * Author:
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

/* BUG: at the moment you can use DBG_STARTFILE only ONCE in
   your program. more then one logfile is not supported. */

#if !COLLECTD_UTILS_DEBUG_H
#define COLLECTD_UTILS_DEBUG_H 1

#if COLLECT_DEBUG

#define DBG(...) cu_debug(__FILE__, __LINE__, __func__, \
	__VA_ARGS__)

#define DBG_STARTFILE(file, ...) cu_debug_startfile(__FILE__, __LINE__, \
	__func__, file, __VA_ARGS__)
#define DBG_STOPFILE(...) cu_debug_stopfile(__FILE__, __LINE__, \
	__func__, __VA_ARGS__)

#define DBG_RESETFILE(file) cu_debug_resetfile(__FILE__, __LINE__, __func__, \
	filename)

void cu_debug(const char *file, int line, const char *func,
	const char *format, ...);

int cu_debug_startfile(const char *file, int line, const char *func,
	const char *filename, const char *format, ...);
int cu_debug_stopfile(const char *file, int line, const char *func,
	const char *format, ...);

int cu_debug_resetfile(const char *file, int line, const char *func,
	const char *filename);

#else /* !COLLECT_DEBUG */

#define DBG(...)                   /* DBG(...) */
#define DBG_STARTFILE(file, ...)   /* DBG_STARTFILE(file, ...) */
#define DBG_STOPFILE(...)          /* DBG_STOPFILE(...) */
#define DBG_RESETFILE(file)        /* DBG_RESETFILE(file) */

#endif /* COLLECT_DEBUG */

#endif /* !COLLECTD_UTILS_DEBUG_H */

