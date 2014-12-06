/**
 * collectd - src/utils_complain.h
 * Copyright (C) 2006-2013  Florian octo Forster
 * Copyright (C) 2008       Sebastian tokkee Harl
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#ifndef UTILS_COMPLAIN_H
#define UTILS_COMPLAIN_H 1

#include "utils_time.h"

typedef struct
{
	/* time of the last report */
	cdtime_t last;

	/* How long to wait until reporting again.
	 * 0 indicates that the complaint is no longer valid. */
	cdtime_t interval;

	_Bool complained_once;
} c_complain_t;

#define C_COMPLAIN_INIT_STATIC { 0, 0, 0 }
#define C_COMPLAIN_INIT(c) do { \
	(c)->last = 0; \
	(c)->interval = 0; \
	(c)->complained_once = 0; \
} while (0)

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

