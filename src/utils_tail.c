/**
 * collectd - src/utils_tail.c
 * Copyright (C) 2008  C-Ware, Inc.
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
 * Author:
 *   Luke Heberling <lukeh at c-ware.com>
 *
 * Description:
 *   Encapsulates useful code for plugins which must watch for appends to
 *   the end of a file.
 **/


#include "utils_tail.h"
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <malloc.h>

struct cu_tail_s
{
	char  *file;
	FILE  *fd;
	struct stat stat;
};

cu_tail_t *cu_tail_create (const char *file)
{
	cu_tail_t *obj;

	obj = (cu_tail_t *) malloc (sizeof (cu_tail_t));
	if (obj == NULL)
		return (NULL);
	memset (obj, '\0', sizeof (cu_tail_t));

	obj->file = strdup (file);
	if (obj->file == NULL)
	{
		free (obj);
		return (NULL);
	}

	obj->fd = NULL;

	return (obj);
} /* cu_tail_t *cu_tail_create */

int cu_tail_destroy (cu_tail_t *obj)
{
	if (obj->fd != NULL)
		fclose (obj->fd);
	free (obj->file);
	free (obj);

	return (0);
} /* int cu_tail_destroy */

int cu_tail_readline (cu_tail_t *obj, char *buf, int buflen)
{
	struct stat stat_now;
	FILE *new_fd;
	int len;

	if( buflen < 1 )
		return -1;
	
	*buf = '\0';

	if (stat (obj->file, &stat_now) != 0)
		return 0;

	if (stat_now.st_dev != obj->stat.st_dev ||
		stat_now.st_ino != obj->stat.st_ino)
	{
		new_fd = fopen (obj->file, "r");
		if (new_fd == NULL)
			return -1;
		
		if (obj->stat.st_ino == 0)
			fseek (new_fd, 0, SEEK_END);
		if (obj->fd != NULL)
			fclose (obj->fd);
		obj->fd = new_fd;

	}
	else if (stat_now.st_size < obj->stat.st_size)
	{
		rewind (obj->fd);
	}

	memcpy (&obj->stat, &stat_now, sizeof (struct stat));	
	
	if (fgets (buf, buflen, obj->fd) == NULL && feof (obj->fd) == 0)
		return -1;

	len = strlen (buf);
	if (len > 0 && *(buf + len - 1) != '\n' && feof (obj->fd))
	{
		fseek (obj->fd, -len, SEEK_CUR);
		*buf = '\0';
	}

	return 0;
} /* int cu_tail_readline */

int cu_tail_read (cu_tail_t *obj, char *buf, int buflen, tailfunc *func, void *data)
{
	int ret;

	while ((ret = cu_tail_readline (obj, buf, buflen)) == 0)
		if (*buf == '\0' || (ret = func (data, buf, buflen)))
				break;

	return ret;
} /* int cu_tail_read */

