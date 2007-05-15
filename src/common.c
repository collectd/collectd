/**
 * collectd - src/common.c
 * Copyright (C) 2005,2006  Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "common.h"
#include "utils_debug.h"

#ifdef HAVE_MATH_H
#  include <math.h>
#endif

extern int operating_mode;

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif

#ifdef HAVE_LIBRRD
#if 0
static char *rra_def[] =
{
		"RRA:AVERAGE:0.0:1:1500",
		"RRA:AVERAGE:0.2:6:1500",
		"RRA:AVERAGE:0.1:180:1680",
		"RRA:AVERAGE:0.1:2160:1520",
		"RRA:MIN:0.0:1:1500",
		"RRA:MIN:0.2:6:1500",
		"RRA:MIN:0.1:180:1680",
		"RRA:MIN:0.1:2160:1520",
		"RRA:MAX:0.0:1:1500",
		"RRA:MAX:0.2:6:1500",
		"RRA:MAX:0.1:180:1680",
		"RRA:MAX:0.1:2160:1520",
		NULL
};
static int rra_num = 12;
#endif

static int rra_timespans[] =
{
	3600,
	86400,
	604800,
	2678400,
	31622400,
	0
};
static int rra_timespans_num = 5;

static char *rra_types[] =
{
	"AVERAGE",
	"MIN",
	"MAX",
	NULL
};
static int rra_types_num = 3;
#endif /* HAVE_LIBRRD */

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

static int check_create_dir (const char *file_orig)
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

/* * * * *
 * Magic *
 * * * * */
#if HAVE_LIBRRD
static int rra_get (char ***ret)
{
	static char **rra_def = NULL;
	static int rra_num = 0;

	int rra_max = rra_timespans_num * rra_types_num;

	int step;
	int rows;
	int span;

	int cdp_num;
	int cdp_len;
	int i, j;

	char buffer[64];

	if ((rra_num != 0) && (rra_def != NULL))
	{
		*ret = rra_def;
		return (rra_num);
	}

	if ((rra_def = (char **) malloc ((rra_max + 1) * sizeof (char *))) == NULL)
		return (-1);
	memset (rra_def, '\0', (rra_max + 1) * sizeof (char *));

	step = atoi (COLLECTD_STEP);
	rows = atoi (COLLECTD_ROWS);

	if ((step <= 0) || (rows <= 0))
	{
		*ret = NULL;
		return (-1);
	}

	cdp_len = 0;
	for (i = 0; i < rra_timespans_num; i++)
	{
		span = rra_timespans[i];

		if ((span / step) < rows)
			continue;

		if (cdp_len == 0)
			cdp_len = 1;
		else
			cdp_len = (int) floor (((double) span) / ((double) (rows * step)));

		cdp_num = (int) ceil (((double) span) / ((double) (cdp_len * step)));

		for (j = 0; j < rra_types_num; j++)
		{
			if (rra_num >= rra_max)
				break;

			if (snprintf (buffer, sizeof(buffer), "RRA:%s:%3.1f:%u:%u",
						rra_types[j], COLLECTD_XFF,
						cdp_len, cdp_num) >= sizeof (buffer))
			{
				syslog (LOG_ERR, "rra_get: Buffer would have been truncated.");
				continue;
			}

			rra_def[rra_num++] = sstrdup (buffer);
		}
	}

#if COLLECT_DEBUG
	DBG ("rra_num = %i", rra_num);
	for (i = 0; i < rra_num; i++)
		DBG ("  %s", rra_def[i]);
#endif

	*ret = rra_def;
	return (rra_num);
}
#endif /* HAVE_LIBRRD */

static int log_create_file (char *filename, char **ds_def, int ds_num)
{
	FILE *log;
	int i;

	if (check_create_dir (filename))
		return (-1);

	log = fopen (filename, "w");
	if (log == NULL)
	{
		syslog (LOG_WARNING, "Failed to create %s: %s", filename,
				strerror(errno));
		return (-1);
	}

	fprintf (log, "epoch");
	for (i = 0; i < ds_num; i++)
	{
		char *name;
		char *tmp;

		name = strchr (ds_def[i], ':');
		if (name == NULL)
		{
			syslog (LOG_WARNING, "Invalid DS definition '%s' for %s",
					ds_def[i], filename);
			fclose(log);
			remove(filename);
			return (-1);
		}

		name += 1;
		tmp = strchr (name, ':');
		if (tmp == NULL)
		{
			syslog (LOG_WARNING, "Invalid DS definition '%s' for %s",
					ds_def[i], filename);
			fclose(log);
			remove(filename);
			return (-1);
		}

		/* The `%.*s' is needed because there is no null-byte behind
		 * the name. */
		fprintf(log, ",%.*s", (int) (tmp - name), name);
	}
	fprintf(log, "\n");
	fclose(log);

	return 0;
}

static int log_update_file (char *host, char *file, char *values,
		char **ds_def, int ds_num)
{
	char *tmp;
	FILE *fp;
	struct stat statbuf;
	char full_file[1024];

	/* Cook the values a bit: Substitute colons with commas */
	strsubstitute (values, ':', ',');

	/* host == NULL => local mode */
	if (host != NULL)
	{
		if (snprintf (full_file, 1024, "%s/%s", host, file) >= 1024)
			return (-1);
	}
	else
	{
		if (snprintf (full_file, 1024, "%s", file) >= 1024)
			return (-1);
	}

	strncpy (full_file, file, 1024);

	tmp = full_file + strlen (full_file) - 4;
	assert ((tmp != NULL) && (tmp > full_file));

	/* Change the filename for logfiles. */
	if (strncmp (tmp, ".rrd", 4) == 0)
	{
		time_t now;
		struct tm *tm;

		/* TODO: Find a way to minimize the calls to `localtime', since
		 * they are pretty expensive.. */
		now = time (NULL);
		tm = localtime (&now);

		strftime (tmp, 1024 - (tmp - full_file), "-%Y-%m-%d", tm);

		/* `localtime(3)' returns a pointer to static data,
		 * therefore the pointer may not be free'd. */
	}
	else
		DBG ("The filename ends with `%s' which is unexpected.", tmp);

	if (stat (full_file, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (log_create_file (full_file, ds_def, ds_num))
				return (-1);
		}
		else
		{
			syslog (LOG_ERR, "stat %s: %s", full_file, strerror (errno));
			return (-1);
		}
	}
	else if (!S_ISREG (statbuf.st_mode))
	{
		syslog (LOG_ERR, "stat %s: Not a regular file!", full_file);
		return (-1);
	}


	fp = fopen (full_file, "a");
	if (fp == NULL)
	{
		syslog (LOG_WARNING, "Failed to append to %s: %s", full_file,
				strerror(errno));
		return (-1);
	}
	fprintf(fp, "%s\n", values);
	fclose(fp);

	return (0);
} /* int log_update_file */

#if HAVE_LIBRRD
static int rrd_create_file (char *filename, char **ds_def, int ds_num)
{
	char **argv;
	int argc;
	char **rra_def;
	int rra_num;
	int i, j;
	int status = 0;

	if (check_create_dir (filename))
		return (-1);

	if ((rra_num = rra_get (&rra_def)) < 1)
	{
		syslog (LOG_ERR, "rra_create failed: Could not calculate RRAs");
		return (-1);
	}

	argc = ds_num + rra_num + 4;

	if ((argv = (char **) malloc (sizeof (char *) * (argc + 1))) == NULL)
	{
		syslog (LOG_ERR, "rrd_create failed: %s", strerror (errno));
		return (-1);
	}

	argv[0] = "create";
	argv[1] = filename;
	argv[2] = "-s";
	argv[3] = COLLECTD_STEP;

	j = 4;
	for (i = 0; i < ds_num; i++)
		argv[j++] = ds_def[i];
	for (i = 0; i < rra_num; i++)
		argv[j++] = rra_def[i];
	argv[j] = NULL;

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	if (rrd_create (argc, argv) == -1)
	{
		syslog (LOG_ERR, "rrd_create failed: %s: %s", filename, rrd_get_error ());
		status = -1;
	}

	free (argv);

	return (status);
}
#endif /* HAVE_LIBRRD */

int rrd_update_file (char *host, char *file, char *values,
		char **ds_def, int ds_num)
{
#if HAVE_LIBRRD
	struct stat statbuf;
	char full_file[1024];
	char *argv[4] = { "update", full_file, values, NULL };
#endif /* HAVE_LIBRRD */

	/* I'd rather have a function `common_update_file' to make this
	 * decission, but for that we'd need to touch all plugins.. */
	if (operating_mode == MODE_LOG)
		return (log_update_file (host, file, values,
					ds_def, ds_num));

#if HAVE_LIBRRD
	/* host == NULL => local mode */
	if (host != NULL)
	{
		if (snprintf (full_file, 1024, "%s/%s", host, file) >= 1024)
			return (-1);
	}
	else
	{
		if (snprintf (full_file, 1024, "%s", file) >= 1024)
			return (-1);
	}

	if (stat (full_file, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (rrd_create_file (full_file, ds_def, ds_num))
				return (-1);
		}
		else
		{
			syslog (LOG_ERR, "stat %s: %s", full_file, strerror (errno));
			return (-1);
		}
	}
	else if (!S_ISREG (statbuf.st_mode))
	{
		syslog (LOG_ERR, "stat %s: Not a regular file!", full_file);
		return (-1);
	}

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	if (rrd_update (3, argv) == -1)
	{
		syslog (LOG_WARNING, "rrd_update failed: %s: %s", full_file, rrd_get_error ());
		return (-1);
	}
	return (0);
/* #endif HAVE_LIBRRD */

#else
	syslog (LOG_ERR, "`rrd_update_file' was called, but collectd isn't linked against librrd!");
	return (-1);
#endif
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
