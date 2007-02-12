/**
 * collectd - src/common.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

#include "common.h"
#include "utils_debug.h"

#ifdef HAVE_MATH_H
#  include <math.h>
#endif

/* for ntohl and htonl */
#if HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif

void sstrncpy (char *d, const char *s, int len)
{
	strncpy (d, s, len);
	d[len - 1] = '\0';
}

char *sstrdup (const char *s)
{
	char *r;

	if (s == NULL)
		return (NULL);

	if((r = strdup (s)) == NULL)
	{
		DBG ("Not enough memory.");
		exit(3);
	}

	return (r);
}

void *smalloc (size_t size)
{
	void *r;

	if ((r = malloc (size)) == NULL)
	{
		DBG("Not enough memory.");
		exit(3);
	}

	return r;
}

#if 0
void sfree (void **ptr)
{
	if (ptr == NULL)
		return;

	if (*ptr != NULL)
		free (*ptr);

	*ptr = NULL;
}
#endif

ssize_t sread (int fd, void *buf, size_t count)
{
	char    *ptr;
	size_t   nleft;
	ssize_t  status;

	ptr   = (char *) buf;
	nleft = count;

	while (nleft > 0)
	{
		status = read (fd, (void *) ptr, nleft);

		if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (status < 0)
			return (status);

		if (status == 0)
		{
			DBG ("Received EOF from fd %i. "
					"Closing fd and returning error.",
					fd);
			close (fd);
			return (-1);
		}

		assert (nleft >= status);

		nleft = nleft - status;
		ptr   = ptr   + status;
	}

	return (0);
}


ssize_t swrite (int fd, const void *buf, size_t count)
{
	const char *ptr;
	size_t      nleft;
	ssize_t     status;

	ptr   = (const char *) buf;
	nleft = count;

	while (nleft > 0)
	{
		status = write (fd, (const void *) ptr, nleft);

		if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (status < 0)
			return (status);

		nleft = nleft - status;
		ptr   = ptr   + status;
	}

	return (0);
}

int strsplit (char *string, char **fields, size_t size)
{
	size_t i;
	char *ptr;

	i = 0;
	ptr = string;
	while ((fields[i] = strtok (ptr, " \t")) != NULL)
	{
		ptr = NULL;
		i++;

		if (i >= size)
			break;
	}

	return (i);
}

int strjoin (char *dst, size_t dst_len,
		char **fields, size_t fields_num,
		const char *sep)
{
	int field_len;
	int sep_len;
	int i;

	memset (dst, '\0', dst_len);

	if (fields_num <= 0)
		return (-1);

	sep_len = 0;
	if (sep != NULL)
		sep_len = strlen (sep);

	for (i = 0; i < fields_num; i++)
	{
		if ((i > 0) && (sep_len > 0))
		{
			if (dst_len <= sep_len)
				return (-1);

			strncat (dst, sep, dst_len);
			dst_len -= sep_len;
		}

		field_len = strlen (fields[i]);

		if (dst_len <= field_len)
			return (-1);

		strncat (dst, fields[i], dst_len);
		dst_len -= field_len;
	}

	return (strlen (dst));
}

int strsubstitute (char *str, char c_from, char c_to)
{
	int ret;

	if (str == NULL)
		return (-1);

	ret = 0;
	while (*str != '\0')
	{
		if (*str == c_from)
		{
			*str = c_to;
			ret++;
		}
		str++;
	}

	return (ret);
}

int escape_slashes (char *buf, int buf_len)
{
	int i;

	if (strcmp (buf, "/") == 0)
	{
		if (buf_len < 5)
			return (-1);

		strncpy (buf, "root", buf_len);
		return (0);
	}

	/* Move one to the left */
	memmove (buf, buf + 1, buf_len - 1);

	for (i = 0; i < buf_len - 1; i++)
	{
		if (buf[i] == '\0')
			break;
		else if (buf[i] == '/')
			buf[i] = '_';
	}
	buf[i] = '\0';

	return (0);
}

int timeval_sub_timespec (struct timeval *tv0, struct timeval *tv1, struct timespec *ret)
{
	if ((tv0 == NULL) || (tv1 == NULL) || (ret == NULL))
		return (-2);

	if ((tv0->tv_sec < tv1->tv_sec)
			|| ((tv0->tv_sec == tv1->tv_sec) && (tv0->tv_usec < tv1->tv_usec)))
		return (-1);

	ret->tv_sec  = tv0->tv_sec - tv1->tv_sec;
	ret->tv_nsec = 1000 * ((long) (tv0->tv_usec - tv1->tv_usec));

	if (ret->tv_nsec < 0)
	{
		assert (ret->tv_sec > 0);

		ret->tv_nsec += 1000000000;
		ret->tv_sec  -= 1;
	}

	return (0);
}

int check_create_dir (const char *file_orig)
{
	struct stat statbuf;

	char  file_copy[512];
	char  dir[512];
	int   dir_len = 512;
	char *fields[16];
	int   fields_num;
	char *ptr;
	int   last_is_file = 1;
	int   len;
	int   i;

	/*
	 * Sanity checks first
	 */
	if (file_orig == NULL)
		return (-1);

	if ((len = strlen (file_orig)) < 1)
		return (-1);
	else if (len >= 512)
		return (-1);

	/*
	 * If `file_orig' ends in a slash the last component is a directory,
	 * otherwise it's a file. Act accordingly..
	 */
	if (file_orig[len - 1] == '/')
		last_is_file = 0;

	/*
	 * Create a copy for `strtok' to destroy
	 */
	strncpy (file_copy, file_orig, 512);
	file_copy[511] = '\0';

	/*
	 * Break into components. This will eat up several slashes in a row and
	 * remove leading and trailing slashes..
	 */
	ptr = file_copy;
	fields_num = 0;
	while ((fields[fields_num] = strtok (ptr, "/")) != NULL)
	{
		ptr = NULL;
		fields_num++;

		if (fields_num >= 16)
			break;
	}

	/*
	 * For each component, do..
	 */
	for (i = 0; i < (fields_num - last_is_file); i++)
	{
		/*
		 * Do not create directories that start with a dot. This
		 * prevents `../../' attacks and other likely malicious
		 * behavior.
		 */
		if (fields[i][0] == '.')
		{
			syslog (LOG_ERR, "Cowardly refusing to create a directory that begins with a `.' (dot): `%s'", file_orig);
			return (-2);
		}

		/*
		 * Join the components together again
		 */
		if (strjoin (dir, dir_len, fields, i + 1, "/") < 0)
		{
			syslog (LOG_ERR, "strjoin failed: `%s', component #%i", file_orig, i);
			return (-1);
		}

		if (stat (dir, &statbuf) == -1)
		{
			if (errno == ENOENT)
			{
				if (mkdir (dir, 0755) == -1)
				{
					syslog (LOG_ERR, "mkdir (%s): %s", dir, strerror (errno));
					return (-1);
				}
			}
			else
			{
				syslog (LOG_ERR, "stat (%s): %s", dir, strerror (errno));
				return (-1);
			}
		}
		else if (!S_ISDIR (statbuf.st_mode))
		{
			syslog (LOG_ERR, "stat (%s): Not a directory!", dir);
			return (-1);
		}
	}

	return (0);
}

#ifdef HAVE_LIBKSTAT
int get_kstat (kstat_t **ksp_ptr, char *module, int instance, char *name)
{
	char ident[128];
	
	if (kc == NULL)
		return (-1);

	snprintf (ident, 128, "%s,%i,%s", module, instance, name);
	ident[127] = '\0';

	if (*ksp_ptr == NULL)
	{
		if ((*ksp_ptr = kstat_lookup (kc, module, instance, name)) == NULL)
		{
			syslog (LOG_ERR, "Cound not find kstat %s", ident);
			return (-1);
		}

		if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED)
		{
			syslog (LOG_WARNING, "kstat %s has wrong type", ident);
			*ksp_ptr = NULL;
			return (-1);
		}
	}

#ifdef assert
	assert (*ksp_ptr != NULL);
	assert ((*ksp_ptr)->ks_type == KSTAT_TYPE_NAMED);
#endif

	if (kstat_read (kc, *ksp_ptr, NULL) == -1)
	{
		syslog (LOG_WARNING, "kstat %s could not be read", ident);
		return (-1);
	}

	if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED)
	{
		syslog (LOG_WARNING, "kstat %s has wrong type", ident);
		return (-1);
	}

	return (0);
}

long long get_kstat_value (kstat_t *ksp, char *name)
{
	kstat_named_t *kn;
	long long retval = -1LL;

#ifdef assert
	assert (ksp != NULL);
	assert (ksp->ks_type == KSTAT_TYPE_NAMED);
#else
	if (ksp == NULL)
	{
		fprintf (stderr, "ERROR: %s:%i: ksp == NULL\n", __FILE__, __LINE__);
		return (-1LL);
	}
	else if (ksp->ks_type != KSTAT_TYPE_NAMED)
	{
		fprintf (stderr, "ERROR: %s:%i: ksp->ks_type != KSTAT_TYPE_NAMED\n", __FILE__, __LINE__);
		return (-1LL);
	}
#endif

	if ((kn = (kstat_named_t *) kstat_data_lookup (ksp, name)) == NULL)
		return (retval);

	if (kn->data_type == KSTAT_DATA_INT32)
		retval = (long long) kn->value.i32;
	else if (kn->data_type == KSTAT_DATA_UINT32)
		retval = (long long) kn->value.ui32;
	else if (kn->data_type == KSTAT_DATA_INT64)
		retval = (long long) kn->value.i64; /* According to ANSI C99 `long long' must hold at least 64 bits */
	else if (kn->data_type == KSTAT_DATA_UINT64)
		retval = (long long) kn->value.ui64; /* XXX: Might overflow! */
	else
		syslog (LOG_WARNING, "get_kstat_value: Not a numeric value: %s", name);
		 
	return (retval);
}
#endif /* HAVE_LIBKSTAT */

unsigned long long ntohll (unsigned long long n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
	return (n);
#else
	return (((unsigned long long) ntohl (n)) << 32) + ntohl (n >> 32);
#endif
}

unsigned long long htonll (unsigned long long n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
	return (n);
#else
	return (((unsigned long long) htonl (n)) << 32) + htonl (n >> 32);
#endif
}
