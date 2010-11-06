/**
 * collectd - src/utils_tail.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
 * Copyright (C) 2008  Florian Forster
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
 *   Florian Forster <octo at verplant.org>
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
	FILE  *fh;
	struct stat stat;
};

static int cu_tail_reopen (cu_tail_t *obj)
{
  int seek_end = 0;
  FILE *fh;
  struct stat stat_buf;
  int status;

  memset (&stat_buf, 0, sizeof (stat_buf));
  status = stat (obj->file, &stat_buf);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("utils_tail: stat (%s) failed: %s", obj->file,
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  /* The file is already open.. */
  if ((obj->fh != NULL) && (stat_buf.st_ino == obj->stat.st_ino))
  {
    /* Seek to the beginning if file was truncated */
    if (stat_buf.st_size < obj->stat.st_size)
    {
      INFO ("utils_tail: File `%s' was truncated.", obj->file);
      status = fseek (obj->fh, 0, SEEK_SET);
      if (status != 0)
      {
	char errbuf[1024];
	ERROR ("utils_tail: fseek (%s) failed: %s", obj->file,
	    sstrerror (errno, errbuf, sizeof (errbuf)));
	fclose (obj->fh);
	obj->fh = NULL;
	return (-1);
      }
    }
    memcpy (&obj->stat, &stat_buf, sizeof (struct stat));
    return (1);
  }

  /* Seek to the end if we re-open the same file again or the file opened
   * is the first at all or the first after an error */
  if ((obj->stat.st_ino == 0) || (obj->stat.st_ino == stat_buf.st_ino))
    seek_end = 1;

  fh = fopen (obj->file, "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("utils_tail: fopen (%s) failed: %s", obj->file,
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  if (seek_end != 0)
  {
    status = fseek (fh, 0, SEEK_END);
    if (status != 0)
    {
      char errbuf[1024];
      ERROR ("utils_tail: fseek (%s) failed: %s", obj->file,
	  sstrerror (errno, errbuf, sizeof (errbuf)));
      fclose (fh);
      return (-1);
    }
  }

  if (obj->fh != NULL)
    fclose (obj->fh);
  obj->fh = fh;
  memcpy (&obj->stat, &stat_buf, sizeof (struct stat));

  return (0);
} /* int cu_tail_reopen */

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

	obj->fh = NULL;

	return (obj);
} /* cu_tail_t *cu_tail_create */

int cu_tail_destroy (cu_tail_t *obj)
{
	if (obj->fh != NULL)
		fclose (obj->fh);
	free (obj->file);
	free (obj);

	return (0);
} /* int cu_tail_destroy */

int cu_tail_readline (cu_tail_t *obj, char *buf, int buflen)
{
  int status;

  if (buflen < 1)
  {
    ERROR ("utils_tail: cu_tail_readline: buflen too small: %i bytes.",
	buflen);
    return (-1);
  }

  if (obj->fh == NULL)
  {
    status = cu_tail_reopen (obj);
    if (status < 0)
      return (status);
  }
  assert (obj->fh != NULL);

  /* Try to read from the filehandle. If that succeeds, everything appears to
   * be fine and we can return. */
  clearerr (obj->fh);
  if (fgets (buf, buflen, obj->fh) != NULL)
  {
    buf[buflen - 1] = 0;
    return (0);
  }

  /* Check if we encountered an error */
  if (ferror (obj->fh) != 0)
  {
    /* Jupp, error. Force `cu_tail_reopen' to reopen the file.. */
    fclose (obj->fh);
    obj->fh = NULL;
  }
  /* else: eof -> check if the file was moved away and reopen the new file if
   * so.. */

  status = cu_tail_reopen (obj);
  /* error -> return with error */
  if (status < 0)
    return (status);
  /* file end reached and file not reopened -> nothing more to read */
  else if (status > 0)
  {
    buf[0] = 0;
    return (0);
  }

  /* If we get here: file was re-opened and there may be more to read.. Let's
   * try again. */
  if (fgets (buf, buflen, obj->fh) != NULL)
  {
    buf[buflen - 1] = 0;
    return (0);
  }

  if (ferror (obj->fh) != 0)
  {
    char errbuf[1024];
    WARNING ("utils_tail: fgets (%s) returned an error: %s", obj->file,
	sstrerror (errno, errbuf, sizeof (errbuf)));
    fclose (obj->fh);
    obj->fh = NULL;
    return (-1);
  }

  /* EOf, well, apparently the new file is empty.. */
  buf[0] = 0;
  return (0);
} /* int cu_tail_readline */

int cu_tail_read (cu_tail_t *obj, char *buf, int buflen, tailfunc_t *callback,
		void *data)
{
	int status;

	while (42)
	{
		size_t len;

		status = cu_tail_readline (obj, buf, buflen);
		if (status != 0)
		{
			ERROR ("utils_tail: cu_tail_read: cu_tail_readline "
					"failed.");
			break;
		}

		/* check for EOF */
		if (buf[0] == 0)
			break;

		len = strlen (buf);
		while (len > 0) {
			if (buf[len - 1] != '\n')
				break;
			buf[len - 1] = '\0';
		}

		status = callback (data, buf, buflen);
		if (status != 0)
		{
			ERROR ("utils_tail: cu_tail_read: callback returned "
					"status %i.", status);
			break;
		}
	}

	return status;
} /* int cu_tail_read */
