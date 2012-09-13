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

	sstrncpy (buf_ptr, string,
			((size_t)off1 + 1 > buflen) ? buflen : (size_t)off1 + 1);
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

char *subst_string (char *buf, size_t buflen, const char *string,
		const char *needle, const char *replacement)
{
	char *temp;
	size_t needle_len;
	size_t i;

	if ((buf == NULL) || (string == NULL)
			|| (needle == NULL) || (replacement == NULL))
		return (NULL);

	temp = (char *) malloc (buflen);
	if (temp == NULL)
	{
		ERROR ("subst_string: malloc failed.");
		return (NULL);
	}

	needle_len = strlen (needle);
	strncpy (buf, string, buflen);

	/* Limit the loop to prevent endless loops. */
	for (i = 0; i < buflen; i++)
	{
		char *begin_ptr;
		size_t begin;

		/* Find `needle' in `buf'. */
		begin_ptr = strstr (buf, needle);
		if (begin_ptr == NULL)
			break;

		/* Calculate the start offset. */
		begin = begin_ptr - buf;

		/* Substitute the region using `subst'. The result is stored in
		 * `temp'. */
		begin_ptr = subst (temp, buflen, buf,
				begin, begin + needle_len,
				replacement);
		if (begin_ptr == NULL)
		{
			WARNING ("subst_string: subst failed.");
			break;
		}

		/* Copy the new string in `temp' to `buf' for the next round. */
		strncpy (buf, temp, buflen);
	}

	if (i >= buflen)
	{
		WARNING ("subst_string: Loop exited after %zu iterations: "
				"string = %s; needle = %s; replacement = %s;",
				i, string, needle, replacement);
	}

	sfree (temp);
	return (buf);
} /* char *subst_string */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

