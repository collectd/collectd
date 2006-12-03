/**
 * collectd - src/email.c
 * Copyright (C) 2006  Sebastian Harl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

/*
 * This plugin communicates with a spam filter, a virus scanner or similar
 * software using a UNIX socket and a very simple protocol:
 *
 * e-mail type (e.g. ham, spam, virus, ...) and size
 * e:<type>:<bytes>
 *
 * spam score
 * s:<value>
 *
 * successful spam checks
 * c:<type1>[,<type2>,...]
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_LIBPTHREAD
# include <pthread.h>
# define EMAIL_HAVE_READ 1
#else
# define EMAIL_HAVE_READ 0
#endif

#if HAVE_SYS_SELECT_H
#	include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */

#if HAVE_SYS_SOCKET_H
#	include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */

/* *sigh* glibc does not define UNIX_PATH_MAX in sys/un.h ... */
#if HAVE_LINUX_UN_H
#	include <linux/un.h>
#elif HAVE_SYS_UN_H
#	include <sys/un.h>
#endif /* HAVE_LINUX_UN_H | HAVE_SYS_UN_H */

#if HAVE_GRP_H
#	include <grp.h>
#endif /* HAVE_GRP_H */

#define MODULE_NAME "email"

/* 256 bytes ought to be enough for anybody ;-) */
#define BUFSIZE 256

#define SOCK_PATH "/tmp/.collectd-email"
#define MAX_CONNS 5

/*
 * Private data structures
 */
#if EMAIL_HAVE_READ
/* linked list of email and check types */
typedef struct type {
	char        *name;
	int         value;
	struct type *next;
} type_t;

typedef struct {
	type_t *head;
	type_t *tail;
} type_list_t;

/* linked list of collector thread control information */
typedef struct collector {
	pthread_t thread;

	/* socket to read data from */
	int socket;

	/* buffer to read data to */
	char buffer[BUFSIZE];
	int  idx; /* current position in buffer */

	struct collector *next;
} collector_t;

typedef struct {
	collector_t *head;
	collector_t *tail;
} collector_list_t;
#endif /* EMAIL_HAVE_READ */

/*
 * Private variables
 */
#if EMAIL_HAVE_READ
/* state of the plugin */
static int disabled = 0;

/* thread managing "client" connections */
static pthread_t connector;

/* tell the connector thread that a collector is available */
static pthread_cond_t collector_available = PTHREAD_COND_INITIALIZER;

/* collector threads that are in use */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static collector_list_t active;

/* collector threads that are available for use */
static pthread_mutex_t available_mutex = PTHREAD_MUTEX_INITIALIZER;
static collector_list_t available;

static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t count;

static pthread_mutex_t size_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t size;

static pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
static double score;
static int score_count;

static pthread_mutex_t check_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t check;
#endif /* EMAIL_HAVE_READ */

#define COUNT_FILE "email/email-%s.rrd"
static char *count_ds_def[] =
{
	"DS:count:GAUGE:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int count_ds_num = 1;

#define SIZE_FILE  "email/email_size-%s.rrd"
static char *size_ds_def[] =
{
	"DS:size:GAUGE:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int size_ds_num = 1;

#define SCORE_FILE "email/spam_score.rrd"
static char *score_ds_def[] =
{
	"DS:score:GAUGE:"COLLECTD_HEARTBEAT":U:U",
	NULL
};
static int score_ds_num = 1;

#define CHECK_FILE "email/spam_check-%s.rrd"
static char *check_ds_def[] =
{
	"DS:hits:GAUGE:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int check_ds_num = 1;

#if EMAIL_HAVE_READ
/* Increment the value of the given name in the given list by incr. */
static void type_list_incr (type_list_t *list, char *name, int incr)
{
	if (NULL == list->head) {
		list->head = (type_t *)smalloc (sizeof (type_t));

		list->head->name  = sstrdup (name);
		list->head->value = incr;
		list->head->next  = NULL;

		list->tail = list->head;
	}
	else {
		type_t *ptr;

		for (ptr = list->head; NULL != ptr; ptr = ptr->next) {
			if (0 == strcmp (name, ptr->name))
				break;
		}

		if (NULL == ptr) {
			list->tail->next = (type_t *)smalloc (sizeof (type_t));
			list->tail = list->tail->next;

			list->tail->name  = sstrdup (name);
			list->tail->value = incr;
			list->tail->next  = NULL;
		}
		else {
			ptr->value += incr;
		}
	}
	return;
} /* static void type_list_incr (type_list_t *, char *) */

/* Read a single character from the socket. If an error occurs or end-of-file
 * is reached return '\0'. */
char read_char (collector_t *src)
{
	char ret = '\0';

	fd_set fdset;

	FD_ZERO (&fdset);
	FD_SET (src->socket, &fdset);

	if (-1 == select (src->socket + 1, &fdset, NULL, NULL, NULL)) {
		syslog (LOG_ERR, "select() failed: %s", strerror (errno));
		return '\0';
	}

	assert (FD_ISSET (src->socket, &fdset));

	do {
		ssize_t len = 0;

		errno = 0;
		if (0 > (len = read (src->socket, (void *)&ret, 1))) {
			if (EINTR != errno) {
				syslog (LOG_ERR, "read() failed: %s", strerror (errno));
				return '\0';
			}
		}

		if (0 == len)
			return '\0';
	} while (EINTR == errno);
	return ret;
} /* char read_char (collector_t *) */

/* Read a single line (terminated by '\n') from the the socket.
 *
 * The return value is zero terminated and does not contain any newline
 * characters. In case that no complete line is available (non-blocking mode
 * should be enabled) an empty string is returned.
 *
 * If an error occurs or end-of-file is reached return NULL.
 *
 * IMPORTANT NOTE: If there is no newline character found in BUFSIZE
 * characters of the input stream, the line will will be ignored! By
 * definition we should not get any longer input lines, thus this is
 * acceptable in this case ;-) */
char *read_line (collector_t *src)
{
	int  i = 0;
	char *ret;

	assert (BUFSIZE > src->idx);

	for (i = 0; i < src->idx; ++i) {
		if ('\n' == src->buffer[i])
			break;
	}

	if ('\n' != src->buffer[i]) {
		fd_set fdset;
	
		ssize_t len = 0;

		FD_ZERO (&fdset);
		FD_SET (src->socket, &fdset);

		if (-1 == select (src->socket + 1, &fdset, NULL, NULL, NULL)) {
			syslog (LOG_ERR, "select() failed: %s", strerror (errno));
			return NULL;
		}

		assert (FD_ISSET (src->socket, &fdset));

		do {
			errno = 0;
			if (0 > (len = read (src->socket,
							(void *)(&(src->buffer[0]) + src->idx),
							BUFSIZE - src->idx))) {
				if (EINTR != errno) {
					syslog (LOG_ERR, "read() failed: %s", strerror (errno));
					return NULL;
				}
			}

			if (0 == len)
				return NULL;
		} while (EINTR == errno);

		src->idx += len;

		for (i = src->idx - len; i < src->idx; ++i) {
			if ('\n' == src->buffer[i])
				break;
		}

		if ('\n' != src->buffer[i]) {
			ret = (char *)smalloc (1);

			ret[0] = '\0';

			if (BUFSIZE == src->idx) { /* no space left in buffer */
				while ('\n' != read_char (src))
					/* ignore complete line */;

				src->idx = 0;
			}
			return ret;
		}
	}

	ret = (char *)smalloc (i + 1);
	memcpy (ret, &(src->buffer[0]), i + 1);
	ret[i] = '\0';

	src->idx -= (i + 1);

	if (0 == src->idx)
		src->buffer[0] = '\0';
	else
		memmove (&(src->buffer[0]), &(src->buffer[i + 1]), src->idx);
	return ret;
} /* char *read_line (collector_t *) */

static void *collect (void *arg)
{
	collector_t *this = (collector_t *)arg;
	
	int loop = 1;

	{ /* put the socket in non-blocking mode */
		int flags = 0;

		errno = 0;
		if (-1 == fcntl (this->socket, F_GETFL, &flags)) {
			syslog (LOG_ERR, "fcntl() failed: %s", strerror (errno));
			loop = 0;
		}

		errno = 0;
		if (-1 == fcntl (this->socket, F_SETFL, flags | O_NONBLOCK)) {
			syslog (LOG_ERR, "fcntl() failed: %s", strerror (errno));
			loop = 0;
		}
	}

	while (loop) {
		char *line = read_line (this);

		if (NULL == line) {
			loop = 0;
			break;
		}

		if ('\0' == line[0]) {
			free (line);
			continue;
		}

		if (':' != line[1]) {
			syslog (LOG_ERR, "email: syntax error in line '%s'", line);
			free (line);
			continue;
		}

		if ('e' == line[0]) { /* e:<type>:<bytes> */
			char *ptr  = NULL;
			char *type = strtok_r (line + 2, ":", &ptr);
			char *tmp  = strtok_r (NULL, ":", &ptr);
			int  bytes = 0;

			if (NULL == tmp) {
				syslog (LOG_ERR, "email: syntax error in line '%s'", line);
				free (line);
				continue;
			}

			bytes = atoi (tmp);

			pthread_mutex_lock (&count_mutex);
			type_list_incr (&count, type, 1);
			pthread_mutex_unlock (&count_mutex);

			pthread_mutex_lock (&size_mutex);
			type_list_incr (&size, type, bytes);
			pthread_mutex_unlock (&size_mutex);
		}
		else if ('s' == line[0]) { /* s:<value> */
			pthread_mutex_lock (&score_mutex);
			score = (score * (double)score_count + atof (line + 2))
					/ (double)(score_count + 1);
			++score_count;
			pthread_mutex_unlock (&score_mutex);
		}
		else if ('c' == line[0]) { /* c:<type1>[,<type2>,...] */
			char *ptr  = NULL;
			char *type = strtok_r (line + 2, ",", &ptr);

			do {
				pthread_mutex_lock (&check_mutex);
				type_list_incr (&check, type, 1);
				pthread_mutex_unlock (&check_mutex);
			} while (NULL != (type = strtok_r (NULL, ",", &ptr)));
		}
		else {
			syslog (LOG_ERR, "email: unknown type '%c'", line[0]);
		}

		free (line);
	}

	/* put this thread back into the available list */
	pthread_mutex_lock (&active_mutex);
	{
		collector_t *last;
		collector_t *ptr;

		last = NULL;

		for (ptr = active.head; NULL != ptr; last = ptr, ptr = ptr->next) {
			if (0 != pthread_equal (ptr->thread, this->thread))
				break;
		}

		/* the current thread _has_ to be in the active list */
		assert (NULL != ptr);

		if (NULL == last) {
			active.head = ptr->next;
		}
		else {
			last->next = ptr->next;

			if (NULL == last->next) {
				active.tail = last;
			}
		}
	}
	pthread_mutex_unlock (&active_mutex);

	this->next = NULL;

	pthread_mutex_lock (&available_mutex);

	if (NULL == available.head) {
		available.head = this;
		available.tail = this;
	}
	else {
		available.tail->next = this;
		available.tail = this;
	}

	pthread_mutex_unlock (&available_mutex);

	pthread_cond_signal (&collector_available);
	pthread_exit ((void *)0);
} /* void *collect (void *) */

static void *open_connection (void *arg)
{
	int local = 0;

	struct sockaddr_un addr;

	/* create UNIX socket */
	errno = 0;
	if (-1 == (local = socket (PF_UNIX, SOCK_STREAM, 0))) {
		disabled = 1;
		syslog (LOG_ERR, "socket() failed: %s", strerror (errno));
		pthread_exit ((void *)1);
	}

	addr.sun_family = AF_UNIX;

	strncpy (addr.sun_path, SOCK_PATH, (size_t)(UNIX_PATH_MAX - 1));
	addr.sun_path[UNIX_PATH_MAX - 1] = '\0';
	unlink (addr.sun_path);

	errno = 0;
	if (-1 == bind (local, (struct sockaddr *)&addr,
				offsetof (struct sockaddr_un, sun_path)
					+ strlen(addr.sun_path))) {
		disabled = 1;
		syslog (LOG_ERR, "bind() failed: %s", strerror (errno));
		pthread_exit ((void *)1);
	}

	errno = 0;
	if (-1 == listen (local, 5)) {
		disabled = 1;
		syslog (LOG_ERR, "listen() failed: %s", strerror (errno));
		pthread_exit ((void *)1);
	}

	if ((uid_t)0 == geteuid ()) {
		struct group *grp;

		errno = 0;
		if (NULL != (grp = getgrnam (COLLECTD_GRP_NAME))) {
			errno = 0;
			if (0 != chown (SOCK_PATH, (uid_t)-1, grp->gr_gid)) {
				syslog (LOG_WARNING, "chown() failed: %s", strerror (errno));
			}
		}
		else {
			syslog (LOG_WARNING, "getgrnam() failed: %s", strerror (errno));
		}
	}
	else {
		syslog (LOG_WARNING, "not running as root");
	}

	errno = 0;
	if (0 != chmod (SOCK_PATH, S_IRWXU | S_IRWXG)) {
		syslog (LOG_WARNING, "chmod() failed: %s", strerror (errno));
	}

	{ /* initialize queue of available threads */
		int i = 0;

		collector_t *last;

		active.head = NULL;
		active.tail = NULL;

		available.head = (collector_t *)smalloc (sizeof (collector_t));
		available.tail = available.head;
		available.tail->next = NULL;

		last = available.head;

		for (i = 1; i < MAX_CONNS; ++i) {
			last->next = (collector_t *)smalloc (sizeof (collector_t));
			last = last->next;
			available.tail = last;
			available.tail->next = NULL;
		}
	}

	while (1) {
		int remote = 0;
		int err    = 0;

		collector_t *collector;

		pthread_attr_t ptattr;

		pthread_mutex_lock (&available_mutex);
		while (NULL == available.head) {
			pthread_cond_wait (&collector_available, &available_mutex);
		}
		pthread_mutex_unlock (&available_mutex);

		do {
			errno = 0;
			if (-1 == (remote = accept (local, NULL, NULL))) {
				if (EINTR != errno) {
					disabled = 1;
					syslog (LOG_ERR, "accept() failed: %s", strerror (errno));
					pthread_exit ((void *)1);
				}
			}
		} while (EINTR == errno);

		/* assign connection to next available thread */
		pthread_mutex_lock (&available_mutex);

		collector = available.head;
		collector->socket = remote;

		if (available.head == available.tail) {
			available.head = NULL;
			available.tail = NULL;
		}
		else {
			available.head = available.head->next;
		}

		pthread_mutex_unlock (&available_mutex);

		collector->idx  = 0;
		collector->next = NULL;

		pthread_attr_init (&ptattr);
		pthread_attr_setdetachstate (&ptattr, PTHREAD_CREATE_DETACHED);

		if (0 == (err = pthread_create (&collector->thread, &ptattr, collect,
				(void *)collector))) {
			pthread_mutex_lock (&active_mutex);

			if (NULL == active.head) {
				active.head = collector;
				active.tail = collector;
			}
			else {
				active.tail->next = collector;
				active.tail = collector;
			}

			pthread_mutex_unlock (&active_mutex);
		}
		else {
			pthread_mutex_lock (&available_mutex);

			if (NULL == available.head) {
				available.head = collector;
				available.tail = collector;
			}
			else {
				available.tail->next = collector;
				available.tail = collector;
			}

			pthread_mutex_unlock (&available_mutex);

			close (remote);
			syslog (LOG_ERR, "pthread_create() failed: %s", strerror (err));
		}

		pthread_attr_destroy (&ptattr);
	}
	pthread_exit ((void *)0);
} /* void *open_connection (void *) */
#endif /* EMAIL_HAVE_READ */

static void email_init (void)
{
#if EMAIL_HAVE_READ
	int err = 0;

	if (0 != (err = pthread_create (&connector, NULL,
				open_connection, NULL))) {
		disabled = 1;
		syslog (LOG_ERR, "pthread_create() failed: %s", strerror (err));
		return;
	}
#endif /* EMAIL_HAVE_READ */
	return;
} /* static void email_init (void) */

static void count_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE] = "";
	int  len           = 0;

	len = snprintf (file, BUFSIZE, COUNT_FILE, inst);
	if ((len < 0) || (len >= BUFSIZE))
		return;

	rrd_update_file (host, file, val, count_ds_def, count_ds_num);
	return;
} /* static void email_write (char *host, char *inst, char *val) */

static void size_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE] = "";
	int  len           = 0;

	len = snprintf (file, BUFSIZE, SIZE_FILE, inst);
	if ((len < 0) || (len >= BUFSIZE))
		return;

	rrd_update_file (host, file, val, size_ds_def, size_ds_num);
	return;
} /* static void size_write (char *host, char *inst, char *val) */

static void score_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, SCORE_FILE, val, score_ds_def, score_ds_num);
	return;
} /* static void score_write (char *host, char *inst, char *val) */

static void check_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE] = "";
	int  len           = 0;

	len = snprintf (file, BUFSIZE, CHECK_FILE, inst);
	if ((len < 0) || (len >= BUFSIZE))
		return;

	rrd_update_file (host, file, val, check_ds_def, check_ds_num);
	return;
} /* static void check_write (char *host, char *inst, char *val) */

#if EMAIL_HAVE_READ
static void type_submit (char *plugin, char *inst, int value)
{
	char buf[BUFSIZE] = "";
	int  len          = 0;

	if (0 == value)
		return;

	len = snprintf (buf, BUFSIZE, "%u:%i", (unsigned int)curtime, value);
	if ((len < 0) || (len >= BUFSIZE))
		return;

	plugin_submit (plugin, inst, buf);
	return;
} /* static void type_submit (char *, char *, int) */

static void score_submit (double value)
{
	char buf[BUFSIZE] = "";
	int  len          = 0;

	if (0.0 == value)
		return;

	len = snprintf (buf, BUFSIZE, "%u:%.2f", (unsigned int)curtime, value);
	if ((len < 0) || (len >= BUFSIZE))
		return;

	plugin_submit ("email_spam_score", NULL, buf);
	return;
}

static void email_read (void)
{
	type_t *ptr;

	if (disabled)
		return;

	pthread_mutex_lock (&count_mutex);

	for (ptr = count.head; NULL != ptr; ptr = ptr->next) {
		type_submit ("email_count", ptr->name, ptr->value);
		ptr->value = 0;
	}

	pthread_mutex_unlock (&count_mutex);

	pthread_mutex_lock (&size_mutex);

	for (ptr = size.head; NULL != ptr; ptr = ptr->next) {
		type_submit ("email_size", ptr->name, ptr->value);
		ptr->value = 0;
	}

	pthread_mutex_unlock (&size_mutex);

	pthread_mutex_lock (&score_mutex);

	score_submit (score);
	score = 0.0;
	score_count = 0;

	pthread_mutex_unlock (&score_mutex);

	pthread_mutex_lock (&check_mutex);

	for (ptr = check.head; NULL != ptr; ptr = ptr->next) {
		type_submit ("email_spam_check", ptr->name, ptr->value);
		ptr->value = 0;
	}

	pthread_mutex_unlock (&check_mutex);
	return;
} /* static void read (void) */
#else /* if !EMAIL_HAVE_READ */
# define email_read NULL
#endif

void module_register (void)
{
	plugin_register (MODULE_NAME, email_init, email_read, NULL);
	plugin_register ("email_count", NULL, NULL, count_write);
	plugin_register ("email_size", NULL, NULL, size_write);
	plugin_register ("email_spam_score", NULL, NULL, score_write);
	plugin_register ("email_spam_check", NULL, NULL, check_write);
	return;
} /* void module_register (void) */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

