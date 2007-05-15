/**
 * collectd - src/utils_debug.c
 * Copyright (C) 2005,2006  Niki W. Waibel
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
 *   Niki W. Waibel <niki.waibel at gmx.net>
 **/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "common.h"
#include "utils_debug.h"

#if COLLECT_DEBUG

static void cu_vdebug(const char *file, int line, const char *func,
	const char *format, va_list ap);

/* if preemptive threads are used, these vars need some sort of locking! */
/* pth is non-preemptive, so no locking is necessary */
static FILE *cu_debug_file = NULL;
static char *cu_debug_filename = NULL;

static void
cu_vdebug(const char *file, int line, const char *func,
	const char *format, va_list ap)
{ 
	FILE *f;
	if(cu_debug_file != NULL) {
		f = cu_debug_file;
	} else {
		/* stderr might be redirected to /dev/null. in that case */
		/* you'll not see anything... */
		f = stderr;
	}

	fprintf(f, "%s:%d:%s(): ",
		file, line, func);
	vfprintf(f, format, ap);
	fprintf(f, "\n");
	fflush(f);
} /* static void cu_vdebug(const char *file, int line, const char *func,
	const char *format, va_list ap) */

void
cu_debug(const char *file, int line, const char *func,
	const char *format, ...)
{ 
	va_list ap;

	va_start(ap, format);
	cu_vdebug(file, line, func, format, ap);
	va_end(ap);
} /* void cu_debug(const char *file, int line, const char *func,
	const char *format, ...) */

int
cu_debug_startfile(const char *file, int line, const char *func,
	const char *filename, const char *format, ...)
{
	va_list ap;

	if(cu_debug_file != NULL) {
		DBG("Don't call this function more then once without"
			" calling cu_debug_stopfile().");
		return EXIT_FAILURE;
	}

	if(cu_debug_filename == NULL) {
		cu_debug_filename = sstrdup(filename);
	}

	cu_debug_file = fopen(cu_debug_filename, "a");
	if(cu_debug_file == NULL) {
		DBG("Cannot open debug file %s: %s.\n",
			cu_debug_filename, strerror(errno));
		return EXIT_FAILURE;
	}

	va_start(ap, format);
	cu_vdebug(file, line, func, format, ap);
	va_end(ap);

	return EXIT_SUCCESS;
} /* int cu_debug_start(const char *file, int line, const char *func,
	const char *format, ...) */

int
cu_debug_stopfile(const char *file, int line, const char *func,
	const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	cu_vdebug(file, line, func, format, ap);
	va_end(ap);

	if(cu_debug_file == NULL) {
		DBG("Don't call this function more then once or without"
			" calling cu_debug_startfile().");
		return EXIT_FAILURE;
	}

	if(fclose(cu_debug_file) != 0) {
		DBG("Cannot close debug file %s: %s.\n",
			cu_debug_filename, strerror(errno));
		return EXIT_FAILURE;
	}
	cu_debug_file = NULL;

	sfree(cu_debug_filename);

	return EXIT_SUCCESS;
} /* int cu_debug_stop(const char *file, int line, const char *func,
	const char *format, ...) */

int
cu_debug_resetfile(const char *file, int line, const char *func,
	const char *filename)
{
	if(filename == NULL) {
		DBG("You have to set filename when calling this function!\n");
		return EXIT_FAILURE;
	}
	if(cu_debug_file != NULL) {
		char *save_filename = NULL;

		/* DBG_STARTFILE was called already */
		/* reopen file */

		DBG_STOPFILE("Closing %s and reopening %s.",
			cu_debug_filename, filename);
		save_filename = smalloc(strlen(cu_debug_filename)+1);
		sstrncpy(save_filename, cu_debug_filename,
			strlen(cu_debug_filename)+1);
		cu_debug_filename = smalloc(strlen(filename)+1);
		sstrncpy(cu_debug_filename, filename, strlen(filename)+1);
		DBG_STARTFILE("Reopening %s after closing %s.",
			filename, save_filename);
		sfree(save_filename);
		return EXIT_SUCCESS;
	}

	/* DBG_STARTFILE was NOT called already */
	/* setting filename only */

	if(cu_debug_filename != NULL) {
		sfree(cu_debug_filename);
	}
	cu_debug_filename = smalloc(strlen(filename)+1);
	sstrncpy(cu_debug_filename, filename, strlen(filename)+1);

	return EXIT_SUCCESS;
} /* int cu_debug_resetfile(const char *file, int line, const char *func,
        const char *filename) */

#endif /* COLLECT_DEBUG */

