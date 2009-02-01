/**
 * collectd - src/utils_complain.h
 * Copyright (C) 2006-2007  Florian octo Forster
 * Copyright (C) 2008  Sebastian tokkee Harl
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#ifndef UTILS_COMPLAIN_H
#define UTILS_COMPLAIN_H 1

#include <time.h>

typedef struct
{
	/* time of the last report */
	time_t last;

	/* how long to wait until reporting again
	 *   0 indicates that the complaint is no longer valid
	 * < 0 indicates that the complaint has been reported once
	 *     => c_complain_once will not report again
	 *     => c_complain uses the absolute value to reset the old value */
	int interval;
} c_complain_t;

#define C_COMPLAIN_INIT_STATIC { 0, 0 }
#define C_COMPLAIN_INIT(c) do { (c)->last = 0; (c)->interval = 0; } while (0)

/*
 * NAME
 *   c_complain
 *
 * DESCRIPTION
 *   Complain about something. This function will report a message (usually
 *   indicating some error condition) using the collectd logging mechanism.
 *   When this function is called again, reporting the message again will be
 *   deferred by an increasing interval (up to one day) to prevent flooding
 *   the logs. A call to `c_release' resets the counter.
 *
 * PARAMETERS
 *   `level'  The log level passed to `plugin_log'.
 *   `c'      Identifier for the complaint.
 *   `format' Message format - see the documentation of printf(3).
 */
void c_complain (int level, c_complain_t *c, const char *format, ...);

/*
 * NAME
 *   c_complain_once
 *
 * DESCRIPTION
 *   Complain about something once. This function will not report anything
 *   again, unless `c_release' has been called in between. If used after some
 *   calls to `c_complain', it will report again on the next interval and stop
 *   after that.
 *
 *   See `c_complain' for further details and a description of the parameters.
 */
void c_complain_once (int level, c_complain_t *c, const char *format, ...);

/*
 * NAME
 *   c_would_release
 *
 * DESCRIPTION
 *   Returns true if the specified complaint would be released, false else.
 */
#define c_would_release(c) ((c)->interval != 0)

/*
 * NAME
 *   c_release
 *
 * DESCRIPTION
 *   Release a complaint. This will report a message once, marking the
 *   complaint as released.
 *
 *   See `c_complain' for a description of the parameters.
 */
void c_do_release (int level, c_complain_t *c, const char *format, ...);
#define c_release(level, c, ...) \
	do { \
		if (c_would_release (c)) \
			c_do_release(level, c, __VA_ARGS__); \
	} while (0)

#endif /* UTILS_COMPLAIN_H */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

