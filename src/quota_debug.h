/**
 * collectd - src/quota_debug.h
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

#if !COLLECTD_QUOTA_DEBUG_H
#define COLLECTD_QUOTA_DEBUG_H 1

#include "common.h"

#define QUOTA_PLUGIN_DEBUG 1
#define QUOTA_PLUGIN_DEBUG_FILE "collectd_quota.log"

#if QUOTA_PLUGIN_DEBUG
	#include <stdio.h>
	extern FILE *QUOTA_DBG_FILE;
	#define DBG(...) \
	{ \
		if(QUOTA_DBG_FILE != NULL) { \
			fprintf(QUOTA_DBG_FILE, "%s:%d:%s(): ", \
				__FILE__, __LINE__, __func__); \
			fprintf(QUOTA_DBG_FILE, __VA_ARGS__); \
			fprintf(QUOTA_DBG_FILE, "\n"); \
			fflush(QUOTA_DBG_FILE); \
		} \
	}
	#define DBG_INIT(...) \
	{ \
		QUOTA_DBG_FILE = fopen(QUOTA_PLUGIN_DEBUG_FILE, "a"); \
		if(QUOTA_DBG_FILE == NULL) { \
			/* stderr is redirected to /dev/null, so you \
			   will not see anything */ \
			fprintf(stderr, "Cannot open quota debug file.\n"); \
		} else { \
			DBG(__VA_ARGS__); \
		} \
	}
#else /* !QUOTA_PLUGIN_DEBUG */
	#define DBG(...) /**/
	#define DBG_INIT(...) /**/
#endif /* QUOTA_PLUGIN_DEBUG */

#endif /* !COLLECTD_QUOTA_DEBUG_H */

