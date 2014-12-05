/**
 * collectd - src/utils_complain.c
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

#include "collectd.h"
#include "utils_complain.h"
#include "plugin.h"

/* vcomplain returns 0 if it did not report, 1 else */
static int vcomplain (int level, c_complain_t *c,
		const char *format, va_list ap)
{
	cdtime_t now;
	char   message[512];

	now = cdtime ();

	if (c->last + c->interval > now)
		return 0;

	c->last = now;

	if (c->interval < plugin_get_interval ())
		c->interval = plugin_get_interval ();
	else
		c->interval *= 2;

	if (c->interval > TIME_T_TO_CDTIME_T (86400))
		c->interval = TIME_T_TO_CDTIME_T (86400);

	vsnprintf (message, sizeof (message), format, ap);
	message[sizeof (message) - 1] = '\0';

	plugin_log (level, "%s", message);
	return 1;
} /* vcomplain */

void c_complain (int level, c_complain_t *c, const char *format, ...)
{
	va_list ap;

	va_start (ap, format);
	if (vcomplain (level, c, format, ap))
		c->complained_once = 1;
	va_end (ap);
} /* c_complain */

void c_complain_once (int level, c_complain_t *c, const char *format, ...)
{
	va_list ap;

	if (c->complained_once)
		return;

	va_start (ap, format);
	if (vcomplain (level, c, format, ap))
		c->complained_once = 1;
	va_end (ap);
} /* c_complain_once */

void c_do_release (int level, c_complain_t *c, const char *format, ...)
{
	char message[512];
	va_list ap;

	if (c->interval == 0)
		return;

	c->interval = 0;
	c->complained_once = 0;

	va_start (ap, format);
	vsnprintf (message, sizeof (message), format, ap);
	message[sizeof (message) - 1] = '\0';
	va_end (ap);

	plugin_log (level, "%s", message);
} /* c_release */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

