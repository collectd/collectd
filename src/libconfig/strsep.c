/*
 * Copyright (C) 2001, 2002, and 2003  Roy Keene
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *      email: libconfig@rkeene.org
 */

/* Compliments of Jay Freeman <saurik@saurik.com> */

#include <string.h>
#include "strsep.h"

char *strsep(char **stringp, const char *delim) {
	char *ret = *stringp;
	if (ret == NULL) return(NULL); /* grrr */
	if ((*stringp = strpbrk(*stringp, delim)) != NULL) {
		*((*stringp)++) = '\0';
	}
	return(ret);
}

