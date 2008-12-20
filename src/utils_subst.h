/**
 * collectd - src/utils_subst.h
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

#ifndef UTILS_SUBST_H
#define UTILS_SUBST_H 1

#include <stddef.h>

/*
 * subst:
 *
 * Replace a substring of a string with the specified replacement text. The
 * resulting string is stored in the buffer pointed to by 'buf' of length
 * 'buflen'. Upon success, the buffer will always be null-terminated. The
 * result may be truncated if the buffer is too small.
 *
 * The substring to be replaces is identified by the two offsets 'off1' and
 * 'off2' where 'off1' specifies the offset to the beginning of the substring
 * and 'off2' specifies the offset to the first byte after the substring.
 *
 * The minimum buffer size to store the complete return value (including the
 * terminating '\0' character) thus has to be:
 * off1 + strlen(replacement) + strlen(string) - off2 + 1
 *
 * Example:
 *
 *             01234567890
 *   string = "foo_____bar"
 *                ^    ^
 *                |    |
 *              off1  off2
 *
 *   off1 = 3
 *   off2 = 8
 *
 *   replacement = " - "
 *
 *   -> "foo - bar"
 *
 * The function returns 'buf' on success, NULL else.
 */
char *subst (char *buf, size_t buflen, const char *string, int off1, int off2,
		const char *replacement);

/*
 * asubst:
 *
 * This function is very similar to subst(). It differs in that it
 * automatically allocates the memory required for the return value which the
 * user then has to free himself.
 *
 * Returns the newly allocated result string on success, NULL else.
 */
char *asubst (const char *string, int off1, int off2, const char *replacement);

/*
 * subst_string:
 *
 * Works like `subst', but instead of specifying start and end offsets you
 * specify `needle', the string that is to be replaced. If `needle' is found
 * in `string' (using strstr(3)), the offset is calculated and `subst' is
 * called with the determined parameters.
 *
 * If the substring is not found, no error will be indicated and
 * `subst_string' works mostly like `strncpy'.
 *
 * If the substring appears multiple times, all appearances will be replaced.
 * If the substring has been found `buflen' times, an endless loop is assumed
 * and the loop is broken. A warning is printed and the function returns
 * success.
 */
char *subst_string (char *buf, size_t buflen, const char *string,
		const char *needle, const char *replacement);

#endif /* UTILS_SUBST_H */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

