/**
 * collectd - src/utils_subst.c
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
 * Authors:
 *   Sebastian "tokkee" Harl <sh at tokkee.org>
 **/

/*
 * This module provides functions for string substitution.
 */

#include "collectd.h"
#include "common.h"

char *subst (char *buf, size_t buflen, const char *string, int off1, int off2,
		const char *replacement)
{
	char  *buf_ptr = buf;
	size_t len     = buflen;

	if ((NULL == buf) || (0 >= buflen) || (NULL == string)
			|| (0 > off1) || (0 > off2) || (off1 > off2)
			|| (NULL == replacement))
		return NULL;

	sstrncpy (buf_ptr, string, (off1 + 1 > buflen) ? buflen : off1 + 1);
	buf_ptr += off1;
	len     -= off1;

	if (0 >= len)
		return buf;

	sstrncpy (buf_ptr, replacement, len);
	buf_ptr += strlen (replacement);
	len     -= strlen (replacement);

	if (0 >= len)
		return buf;

	sstrncpy (buf_ptr, string + off2, len);
	return buf;
} /* subst */

char *asubst (const char *string, int off1, int off2, const char *replacement)
{
	char *buf;
	int   len;

	char *ret;

	if ((NULL == string) || (0 > off1) || (0 > off2) || (off1 > off2)
			|| (NULL ==replacement))
		return NULL;

	len = off1 + strlen (replacement) + strlen (string) - off2 + 1;

	buf = (char *)malloc (len);
	if (NULL == buf)
		return NULL;

	ret = subst (buf, len, string, off1, off2, replacement);
	if (NULL == ret)
		free (buf);
	return ret;
} /* asubst */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

