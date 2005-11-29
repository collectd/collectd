/**
 * collectd - src/quota_fs.c
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

#include "common.h"
#include "quota_debug.h"
#include "quota_fs.h"

int
quota_fs_issupported(const char *fsname)
{
	if(!strcmp(fsname, "ext2")
	|| !strcmp(fsname, "ext3")
	|| !strcmp(fsname, "ufs")
	|| !strcmp(fsname, "vxfs")
	|| !strcmp(fsname, "zfs"))
	{
		return EXIT_SUCCESS;
	} else {
#if 0
		DBG("%s filesystem not supported", fsname);
#endif
		return EXIT_FAILURE;
	}
}

int
quota_fs_isnfs(const char *fsname)
{
	if(!strcmp(fsname, "nfs") || !strcmp(fsname, "nfs4")) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}
