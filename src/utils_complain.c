/**
 * collectd - src/utils_complain.c
 * Copyright (C) 2006-2013  Florian octo Forster
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

