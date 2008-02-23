/**
 * collectd - src/utils_tail.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
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

#include "collectd.h"
#include "common.h"
#include "utils_tail.h"

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
	int status;

	if (buflen < 1)
		return (-1);
	
	if (stat (obj->file, &stat_now) != 0)
	{
		char errbuf[1024];
		ERROR ("cu_tail_readline: stat (%s) failed: %s",
				obj->file,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	if ((stat_now.st_dev != obj->stat.st_dev) ||
		(stat_now.st_ino != obj->stat.st_ino))
	{
		/*
		 * If the file was replaced open the new file and close the
		 * old filehandle
		 */
		FILE *new_fd;

		new_fd = fopen (obj->file, "r");
		if (new_fd == NULL)
		{
			char errbuf[1024];
			ERROR ("cu_tail_readline: open (%s) failed: %s",
					obj->file,
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			return (-1);
		}
		
		/* If there was no previous file, seek to the end. We don't
		 * want to read in the entire file, usually. */
		if (obj->stat.st_ino == 0)
			fseek (new_fd, 0, SEEK_END);

		if (obj->fd != NULL)
			fclose (obj->fd);
		obj->fd = new_fd;

	}
	else if (stat_now.st_size < obj->stat.st_size)
	{
		/*
		 * Else, if the file was not replaces, but the file was
		 * truncated, seek to the beginning of the file.
		 */
		assert (obj->fd != NULL);
		rewind (obj->fd);
	}

	status = 0;
	if (fgets (buf, buflen, obj->fd) == NULL)
	{
		if (feof (obj->fd) == 0)
			buf[0] = '\0';
		else /* an error occurred */
			status = -1;
	}

	if (status == 0)
		memcpy (&obj->stat, &stat_now, sizeof (struct stat));	
	
	return (status);
} /* int cu_tail_readline */

int cu_tail_read (cu_tail_t *obj, char *buf, int buflen, tailfunc_t *callback,
		void *data)
{
	int status;

	while ((status = cu_tail_readline (obj, buf, buflen)) == 0)
	{
		/* check for EOF */
		if (buf[0] == '\0')
			break;

		status = callback (data, buf, buflen);
		if (status != 0)
			break;
	}

	return status;
} /* int cu_tail_read */
