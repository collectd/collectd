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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#ifdef HAVE_MATH_H
# include <math.h>
#endif

/* for ntohl and htonl */
#if HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif

#if !HAVE_GETPWNAM_R
static pthread_mutex_t getpwnam_r_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#if !HAVE_STRERROR_R
static pthread_mutex_t strerror_r_lock = PTHREAD_MUTEX_INITIALIZER;
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
		DEBUG ("Not enough memory.");
		exit(3);
	}

	return (r);
}

/* Even though Posix requires "strerror_r" to return an "int",
 * some systems (e.g. the GNU libc) return a "char *" _and_
 * ignore the second argument ... -tokkee */
char *sstrerror (int errnum, char *buf, size_t buflen)
{
	buf[0] = '\0';

#if !HAVE_STRERROR_R
	{
		char *temp;

		pthread_mutex_lock (&strerror_r_lock);

		temp = strerror (errnum);
		strncpy (buf, temp, buflen);

		pthread_mutex_unlock (&strerror_r_lock);
	}
/* #endif !HAVE_STRERROR_R */

#elif STRERROR_R_CHAR_P
	{
		char *temp;
		temp = strerror_r (errnum, buf, buflen);
		if (buf[0] == '\0')
		{
			if ((temp != NULL) && (temp != buf) && (temp[0] != '\0'))
				strncpy (buf, temp, buflen);
			else
				strncpy (buf, "strerror_r did not return "
						"an error message", buflen);
		}
	}
/* #endif STRERROR_R_CHAR_P */

#else
	if (strerror_r (errnum, buf, buflen) != 0)
	{
		snprintf (buf, buflen, "Error #%i; "
				"Additionally, strerror_r failed.",
				errnum);
	}
#endif /* STRERROR_R_CHAR_P */

	buf[buflen - 1] = '\0';
	return (buf);
} /* char *sstrerror */

void *smalloc (size_t size)
{
	void *r;

	if ((r = malloc (size)) == NULL)
	{
		DEBUG("Not enough memory.");
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
			DEBUG ("Received EOF from fd %i. "
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
	char *saveptr;

	i = 0;
	ptr = string;
	saveptr = NULL;
	while ((fields[i] = strtok_r (ptr, " \t", &saveptr)) != NULL)
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
} /* int strsubstitute */

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

	if (buf_len <= 1)
		return (0);

	/* Move one to the left */
	if (buf[0] == '/')
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
} /* int escape_slashes */

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
	char *saveptr;
	int   last_is_file = 1;
	int   path_is_absolute = 0;
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
	if (file_orig[0] == '/')
		path_is_absolute = 1;

	/*
	 * Create a copy for `strtok_r' to destroy
	 */
	strncpy (file_copy, file_orig, 512);
	file_copy[511] = '\0';

	/*
	 * Break into components. This will eat up several slashes in a row and
	 * remove leading and trailing slashes..
	 */
	ptr = file_copy;
	saveptr = NULL;
	fields_num = 0;
	while ((fields[fields_num] = strtok_r (ptr, "/", &saveptr)) != NULL)
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
			ERROR ("Cowardly refusing to create a directory that begins with a `.' (dot): `%s'", file_orig);
			return (-2);
		}

		/*
		 * Join the components together again
		 */
		dir[0] = '/';
		if (strjoin (dir + path_is_absolute, dir_len - path_is_absolute,
					fields, i + 1, "/") < 0)
		{
			ERROR ("strjoin failed: `%s', component #%i", file_orig, i);
			return (-1);
		}

		if (stat (dir, &statbuf) == -1)
		{
			if (errno == ENOENT)
			{
				if (mkdir (dir, 0755) == -1)
				{
					char errbuf[1024];
					ERROR ("check_create_dir: mkdir (%s): %s", dir,
							sstrerror (errno,
								errbuf, sizeof (errbuf)));
					return (-1);
				}
			}
			else
			{
				char errbuf[1024];
				ERROR ("stat (%s): %s", dir,
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}
		}
		else if (!S_ISDIR (statbuf.st_mode))
		{
			ERROR ("stat (%s): Not a directory!", dir);
			return (-1);
		}
	}

	return (0);
} /* check_create_dir */

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
			ERROR ("Cound not find kstat %s", ident);
			return (-1);
		}

		if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED)
		{
			WARNING ("kstat %s has wrong type", ident);
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
		WARNING ("kstat %s could not be read", ident);
		return (-1);
	}

	if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED)
	{
		WARNING ("kstat %s has wrong type", ident);
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
		WARNING ("get_kstat_value: Not a numeric value: %s", name);
		 
	return (retval);
}
#endif /* HAVE_LIBKSTAT */

unsigned long long ntohll (unsigned long long n)
{
#if BYTE_ORDER == BIG_ENDIAN
	return (n);
#else
	return (((unsigned long long) ntohl (n)) << 32) + ntohl (n >> 32);
#endif
} /* unsigned long long ntohll */

unsigned long long htonll (unsigned long long n)
{
#if BYTE_ORDER == BIG_ENDIAN
	return (n);
#else
	return (((unsigned long long) htonl (n)) << 32) + htonl (n >> 32);
#endif
} /* unsigned long long htonll */

#if FP_LAYOUT_NEED_NOTHING
/* Well, we need nothing.. */
/* #endif FP_LAYOUT_NEED_NOTHING */

#elif FP_LAYOUT_NEED_ENDIANFLIP || FP_LAYOUT_NEED_INTSWAP
# if FP_LAYOUT_NEED_ENDIANFLIP
#  define FP_CONVERT(A) ((((uint64_t)(A) & 0xff00000000000000LL) >> 56) | \
                         (((uint64_t)(A) & 0x00ff000000000000LL) >> 40) | \
                         (((uint64_t)(A) & 0x0000ff0000000000LL) >> 24) | \
                         (((uint64_t)(A) & 0x000000ff00000000LL) >> 8)  | \
                         (((uint64_t)(A) & 0x00000000ff000000LL) << 8)  | \
                         (((uint64_t)(A) & 0x0000000000ff0000LL) << 24) | \
                         (((uint64_t)(A) & 0x000000000000ff00LL) << 40) | \
                         (((uint64_t)(A) & 0x00000000000000ffLL) << 56))
# else
#  define FP_CONVERT(A) ((((uint64_t)(A) & 0xffffffff00000000LL) >> 32) | \
                         (((uint64_t)(A) & 0x00000000ffffffffLL) << 32))
# endif

double ntohd (double d)
{
	union
	{
		uint8_t  byte[8];
		uint64_t integer;
		double   floating;
	} ret;

	ret.floating = d;

	/* NAN in x86 byte order */
	if ((ret.byte[0] == 0x00) && (ret.byte[1] == 0x00)
			&& (ret.byte[2] == 0x00) && (ret.byte[3] == 0x00)
			&& (ret.byte[4] == 0x00) && (ret.byte[5] == 0x00)
			&& (ret.byte[6] == 0xf8) && (ret.byte[7] == 0x7f))
	{
		return (NAN);
	}
	else
	{
		uint64_t tmp;

		tmp = ret.integer;
		ret.integer = FP_CONVERT (tmp);
		return (ret.floating);
	}
} /* double ntohd */

double htond (double d)
{
	union
	{
		uint8_t  byte[8];
		uint64_t integer;
		double   floating;
	} ret;

	if (isnan (d))
	{
		ret.byte[0] = ret.byte[1] = ret.byte[2] = ret.byte[3] = 0x00;
		ret.byte[4] = ret.byte[5] = 0x00;
		ret.byte[6] = 0xf8;
		ret.byte[7] = 0x7f;
		return (ret.floating);
	}
	else
	{
		uint64_t tmp;

		ret.floating = d;
		tmp = FP_CONVERT (ret.integer);
		ret.integer = tmp;
		return (ret.floating);
	}
} /* double htond */
#endif /* FP_LAYOUT_NEED_ENDIANFLIP || FP_LAYOUT_NEED_INTSWAP */

int format_name (char *ret, int ret_len,
		const char *hostname,
		const char *plugin, const char *plugin_instance,
		const char *type, const char *type_instance)
{
	int  status;

	assert (plugin != NULL);
	assert (type != NULL);

	if ((plugin_instance == NULL) || (strlen (plugin_instance) == 0))
	{
		if ((type_instance == NULL) || (strlen (type_instance) == 0))
			status = snprintf (ret, ret_len, "%s/%s/%s",
					hostname, plugin, type);
		else
			status = snprintf (ret, ret_len, "%s/%s/%s-%s",
					hostname, plugin, type,
					type_instance);
	}
	else
	{
		if ((type_instance == NULL) || (strlen (type_instance) == 0))
			status = snprintf (ret, ret_len, "%s/%s-%s/%s",
					hostname, plugin, plugin_instance,
					type);
		else
			status = snprintf (ret, ret_len, "%s/%s-%s/%s-%s",
					hostname, plugin, plugin_instance,
					type, type_instance);
	}

	if ((status < 1) || (status >= ret_len))
		return (-1);
	return (0);
} /* int format_name */

int parse_identifier (char *str, char **ret_host,
		char **ret_plugin, char **ret_plugin_instance,
		char **ret_type, char **ret_type_instance)
{
	char *hostname = NULL;
	char *plugin = NULL;
	char *plugin_instance = NULL;
	char *type = NULL;
	char *type_instance = NULL;

	hostname = str;
	if (hostname == NULL)
		return (-1);

	plugin = strchr (hostname, '/');
	if (plugin == NULL)
		return (-1);
	*plugin = '\0'; plugin++;

	type = strchr (plugin, '/');
	if (type == NULL)
		return (-1);
	*type = '\0'; type++;

	plugin_instance = strchr (plugin, '-');
	if (plugin_instance != NULL)
	{
		*plugin_instance = '\0';
		plugin_instance++;
	}

	type_instance = strchr (type, '-');
	if (type_instance != NULL)
	{
		*type_instance = '\0';
		type_instance++;
	}

	*ret_host = hostname;
	*ret_plugin = plugin;
	*ret_plugin_instance = plugin_instance;
	*ret_type = type;
	*ret_type_instance = type_instance;
	return (0);
} /* int parse_identifier */

int parse_values (char *buffer, value_list_t *vl, const data_set_t *ds)
{
	int i;
	char *dummy;
	char *ptr;
	char *saveptr;

	i = -1;
	dummy = buffer;
	saveptr = NULL;
	while ((ptr = strtok_r (dummy, ":", &saveptr)) != NULL)
	{
		dummy = NULL;

		if (i >= vl->values_len)
			break;

		if (i == -1)
		{
			if (strcmp ("N", ptr) == 0)
				vl->time = time (NULL);
			else
				vl->time = (time_t) atoi (ptr);
		}
		else
		{
			if (strcmp ("U", ptr) == 0)
				vl->values[i].gauge = NAN;
			else if (ds->ds[i].type == DS_TYPE_COUNTER)
				vl->values[i].counter = atoll (ptr);
			else if (ds->ds[i].type == DS_TYPE_GAUGE)
				vl->values[i].gauge = atof (ptr);
		}

		i++;
	} /* while (strtok_r) */

	if ((ptr != NULL) || (i != vl->values_len))
		return (-1);
	return (0);
} /* int parse_values */

#if !HAVE_GETPWNAM_R
int getpwnam_r (const char *name, struct passwd *pwbuf, char *buf,
		size_t buflen, struct passwd **pwbufp)
{
	int status = 0;
	struct passwd *pw;

	memset (pwbuf, '\0', sizeof (struct passwd));

	pthread_mutex_lock (&getpwnam_r_lock);

	do
	{
		pw = getpwnam (name);
		if (pw == NULL)
		{
			status = (errno != 0) ? errno : ENOENT;
			break;
		}

#define GETPWNAM_COPY_MEMBER(member) \
		if (pw->member != NULL) \
		{ \
			int len = strlen (pw->member); \
			if (len >= buflen) \
			{ \
				status = ENOMEM; \
				break; \
			} \
			sstrncpy (buf, pw->member, buflen); \
			pwbuf->member = buf; \
			buf    += (len + 1); \
			buflen -= (len + 1); \
		}
		GETPWNAM_COPY_MEMBER(pw_name);
		GETPWNAM_COPY_MEMBER(pw_passwd);
		GETPWNAM_COPY_MEMBER(pw_gecos);
		GETPWNAM_COPY_MEMBER(pw_dir);
		GETPWNAM_COPY_MEMBER(pw_shell);

		pwbuf->pw_uid = pw->pw_uid;
		pwbuf->pw_gid = pw->pw_gid;

		if (pwbufp != NULL)
			*pwbufp = pwbuf;
	} while (0);

	pthread_mutex_unlock (&getpwnam_r_lock);

	return (status);
} /* int getpwnam_r */
#endif
