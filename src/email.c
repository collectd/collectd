/**
 * collectd - src/email.c
 * Copyright (C) 2006,2007  Sebastian Harl
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

#include "configfile.h"

#if HAVE_LIBPTHREAD
# include <pthread.h>
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

/* some systems (e.g. Darwin) seem to not define UNIX_PATH_MAX at all */
#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX sizeof (((struct sockaddr_un *)0)->sun_path)
#endif /* UNIX_PATH_MAX */

#if HAVE_GRP_H
#	include <grp.h>
#endif /* HAVE_GRP_H */

#define MODULE_NAME "email"

/* 256 bytes ought to be enough for anybody ;-) */
#define BUFSIZE 256

#ifndef COLLECTD_SOCKET_PREFIX
# define COLLECTD_SOCKET_PREFIX "/tmp/.collectd-"
#endif /* COLLECTD_SOCKET_PREFIX */

#define SOCK_PATH COLLECTD_SOCKET_PREFIX"email"
#define MAX_CONNS 5
#define MAX_CONNS_LIMIT 16384

#define log_err(...) syslog (LOG_ERR, MODULE_NAME": "__VA_ARGS__)
#define log_warn(...) syslog (LOG_WARNING, MODULE_NAME": "__VA_ARGS__)

/*
 * Private data structures
 */
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

/* collector thread control information */
typedef struct collector {
	pthread_t thread;

	/* socket descriptor of the current/last connection */
	int socket;
} collector_t;

/* linked list of pending connections */
typedef struct conn {
	/* socket to read data from */
	int socket;

	/* buffer to read data to */
	char *buffer;
	int  idx; /* current write position in buffer */
	int  length; /* length of the current line, i.e. index of '\0' */

	struct conn *next;
} conn_t;

typedef struct {
	conn_t *head;
	conn_t *tail;
} conn_list_t;

/*
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] =
{
	"SocketGroup",
	"SocketPerms",
	"MaxConns"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static data_source_t gauge_dsrc[1] =
{
	{"value", DS_TYPE_GAUGE, 0.0, NAN}
};

static data_set_t email_count_ds =
{
	"email_count", 1, gauge_dsrc
};

static data_set_t email_size_ds =
{
	"email_size", 1, gauge_dsrc
};

static data_set_t spam_check_ds =
{
	"spam_check", 1, gauge_dsrc
};

static data_source_t spam_score_dsrc[1] =
{
	{"score", DS_TYPE_GAUGE, NAN, NAN}
};

static data_set_t spam_score_ds =
{
	"spam_score", 1, spam_score_dsrc
};

/* socket configuration */
static char *sock_group = COLLECTD_GRP_NAME;
static int  sock_perms  = S_IRWXU | S_IRWXG;
static int  max_conns   = MAX_CONNS;

/* state of the plugin */
static int disabled = 0;

/* thread managing "client" connections */
static pthread_t connector;
static int connector_socket;

/* tell the collector threads that a new connection is available */
static pthread_cond_t conn_available = PTHREAD_COND_INITIALIZER;

/* connections that are waiting to be processed */
static pthread_mutex_t conns_mutex = PTHREAD_MUTEX_INITIALIZER;
static conn_list_t conns;

/* tell the connector thread that a collector is available */
static pthread_cond_t collector_available = PTHREAD_COND_INITIALIZER;

/* collector threads */
static collector_t **collectors;

static pthread_mutex_t available_mutex = PTHREAD_MUTEX_INITIALIZER;
static int available_collectors;

static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t count;

static pthread_mutex_t size_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t size;

static pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
static double score;
static int score_count;

static pthread_mutex_t check_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t check;

/*
 * Private functions
 */
static int email_config (const char *key, const char *value)
{
	if (0 == strcasecmp (key, "SocketGroup")) {
		sock_group = sstrdup (value);
	}
	else if (0 == strcasecmp (key, "SocketPerms")) {
		/* the user is responsible for providing reasonable values */
		sock_perms = (int)strtol (value, NULL, 8);
	}
	else if (0 == strcasecmp (key, "MaxConns")) {
		long int tmp = strtol (value, NULL, 0);

		if (tmp < 1) {
			fprintf (stderr, "email plugin: `MaxConns' was set to invalid "
					"value %li, will use default %i.\n",
					tmp, MAX_CONNS);
			max_conns = MAX_CONNS;
		}
		else if (tmp > MAX_CONNS_LIMIT) {
			fprintf (stderr, "email plugin: `MaxConns' was set to invalid "
					"value %li, will use hardcoded limit %i.\n",
					tmp, MAX_CONNS_LIMIT);
			max_conns = MAX_CONNS_LIMIT;
		}
		else {
			max_conns = (int)tmp;
		}
	}
	else {
		return -1;
	}
	return 0;
} /* static int email_config (char *, char *) */

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
static char read_char (conn_t *src)
{
	char ret = '\0';

	fd_set fdset;

	FD_ZERO (&fdset);
	FD_SET (src->socket, &fdset);

	if (-1 == select (src->socket + 1, &fdset, NULL, NULL, NULL)) {
		log_err ("select() failed: %s", strerror (errno));
		return '\0';
	}

	assert (FD_ISSET (src->socket, &fdset));

	do {
		ssize_t len = 0;

		errno = 0;
		if (0 > (len = read (src->socket, (void *)&ret, 1))) {
			if (EINTR != errno) {
				log_err ("read() failed: %s", strerror (errno));
				return '\0';
			}
		}

		if (0 == len)
			return '\0';
	} while (EINTR == errno);
	return ret;
} /* static char read_char (conn_t *) */

/* Read a single line (terminated by '\n') from the the socket.
 *
 * The return value is zero terminated and does not contain any newline
 * characters.
 *
 * If an error occurs or end-of-file is reached return NULL.
 *
 * IMPORTANT NOTE: If there is no newline character found in BUFSIZE
 * characters of the input stream, the line will will be ignored! By
 * definition we should not get any longer input lines, thus this is
 * acceptable in this case ;-) */
static char *read_line (conn_t *src)
{
	int i = 0;

	assert ((BUFSIZE >= src->idx) && (src->idx >= 0));
	assert ((src->idx > src->length) || (src->length == 0));

	if (src->length > 0) { /* remove old line */
		src->idx -= (src->length + 1);
		memmove (src->buffer, src->buffer + src->length + 1, src->idx);
		src->length = 0;
	}

	for (i = 0; i < src->idx; ++i) {
		if ('\n' == src->buffer[i])
			break;
	}

	if (i == src->idx) {
		fd_set fdset;

		ssize_t len = 0;

		FD_ZERO (&fdset);
		FD_SET (src->socket, &fdset);

		if (-1 == select (src->socket + 1, &fdset, NULL, NULL, NULL)) {
			log_err ("select() failed: %s", strerror (errno));
			return NULL;
		}

		assert (FD_ISSET (src->socket, &fdset));

		do {
			errno = 0;
			if (0 > (len = read (src->socket,
							(void *)(src->buffer + src->idx),
							BUFSIZE - src->idx))) {
				if (EINTR != errno) {
					log_err ("read() failed: %s", strerror (errno));
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

		if (i == src->idx) {
			src->length = 0;

			if (BUFSIZE == src->idx) { /* no space left in buffer */
				while ('\n' != read_char (src))
					/* ignore complete line */;

				src->idx = 0;
			}
			return read_line (src);
		}
	}

	src->buffer[i] = '\0';
	src->length    = i;

	return src->buffer;
} /* static char *read_line (conn_t *) */

static void *collect (void *arg)
{
	collector_t *this = (collector_t *)arg;

	char *buffer = (char *)smalloc (BUFSIZE);

	while (1) {
		int loop = 1;

		conn_t *connection;

		pthread_mutex_lock (&conns_mutex);

		while (NULL == conns.head) {
			pthread_cond_wait (&conn_available, &conns_mutex);
		}

		connection = conns.head;
		conns.head = conns.head->next;

		if (NULL == conns.head) {
			conns.tail = NULL;
		}

		this->socket = connection->socket;

		pthread_mutex_unlock (&conns_mutex);

		connection->buffer = buffer;
		connection->idx    = 0;
		connection->length = 0;

		{ /* put the socket in non-blocking mode */
			int flags = 0;

			errno = 0;
			if (-1 == fcntl (connection->socket, F_GETFL, &flags)) {
				log_err ("fcntl() failed: %s", strerror (errno));
				loop = 0;
			}

			errno = 0;
			if (-1 == fcntl (connection->socket, F_SETFL, flags | O_NONBLOCK)) {
				log_err ("fcntl() failed: %s", strerror (errno));
				loop = 0;
			}
		}

		while (loop) {
			char *line = read_line (connection);

			if (NULL == line) {
				loop = 0;
				break;
			}

			if (':' != line[1]) {
				log_err ("syntax error in line '%s'", line);
				continue;
			}

			if ('e' == line[0]) { /* e:<type>:<bytes> */
				char *ptr  = NULL;
				char *type = strtok_r (line + 2, ":", &ptr);
				char *tmp  = strtok_r (NULL, ":", &ptr);
				int  bytes = 0;

				if (NULL == tmp) {
					log_err ("syntax error in line '%s'", line);
					continue;
				}

				bytes = atoi (tmp);

				pthread_mutex_lock (&count_mutex);
				type_list_incr (&count, type, 1);
				pthread_mutex_unlock (&count_mutex);

				if (bytes > 0) {
					pthread_mutex_lock (&size_mutex);
					type_list_incr (&size, type, bytes);
					pthread_mutex_unlock (&size_mutex);
				}
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
				log_err ("unknown type '%c'", line[0]);
			}
		} /* while (loop) */

		close (connection->socket);

		free (connection);

		pthread_mutex_lock (&available_mutex);
		++available_collectors;
		pthread_mutex_unlock (&available_mutex);

		pthread_cond_signal (&collector_available);
	} /* while (1) */

	free (buffer);
	pthread_exit ((void *)0);
} /* static void *collect (void *) */

static void *open_connection (void *arg)
{
	struct sockaddr_un addr;

	/* create UNIX socket */
	errno = 0;
	if (-1 == (connector_socket = socket (PF_UNIX, SOCK_STREAM, 0))) {
		disabled = 1;
		log_err ("socket() failed: %s", strerror (errno));
		pthread_exit ((void *)1);
	}

	addr.sun_family = AF_UNIX;

	strncpy (addr.sun_path, SOCK_PATH, (size_t)(UNIX_PATH_MAX - 1));
	addr.sun_path[UNIX_PATH_MAX - 1] = '\0';
	unlink (addr.sun_path);

	errno = 0;
	if (-1 == bind (connector_socket, (struct sockaddr *)&addr,
				offsetof (struct sockaddr_un, sun_path)
					+ strlen(addr.sun_path))) {
		disabled = 1;
		log_err ("bind() failed: %s", strerror (errno));
		pthread_exit ((void *)1);
	}

	errno = 0;
	if (-1 == listen (connector_socket, 5)) {
		disabled = 1;
		log_err ("listen() failed: %s", strerror (errno));
		pthread_exit ((void *)1);
	}

	if ((uid_t) 0 == geteuid ())
	{
		struct group sg;
		struct group *grp;
		char grbuf[2048];
		int status;

		grp = NULL;
		status = getgrnam_r (sock_group, &sg, grbuf, sizeof (grbuf), &grp);
		if (status != 0)
		{
			log_warn ("getgrnam_r (%s) failed: %s", sock_group, strerror (status));
		}
		else if (grp == NULL)
		{
			log_warn ("No such group: `%s'", sock_group);
		}
		else
		{
			status = chown (SOCK_PATH, (uid_t) -1, grp->gr_gid);
			if (status != 0)
				log_warn ("chown (%s, -1, %i) failed: %s",
						SOCK_PATH, (int) grp->gr_gid, strerror (errno));
		}
	}
	else /* geteuid != 0 */
	{
		log_warn ("not running as root");
	}

	errno = 0;
	if (0 != chmod (SOCK_PATH, sock_perms)) {
		log_warn ("chmod() failed: %s", strerror (errno));
	}

	{ /* initialize collector threads */
		int i   = 0;
		int err = 0;

		pthread_attr_t ptattr;

		conns.head = NULL;
		conns.tail = NULL;

		pthread_attr_init (&ptattr);
		pthread_attr_setdetachstate (&ptattr, PTHREAD_CREATE_DETACHED);

		available_collectors = max_conns;

		collectors =
			(collector_t **)smalloc (max_conns * sizeof (collector_t *));

		for (i = 0; i < max_conns; ++i) {
			collectors[i] = (collector_t *)smalloc (sizeof (collector_t));
			collectors[i]->socket = 0;

			if (0 != (err = pthread_create (&collectors[i]->thread, &ptattr,
							collect, collectors[i]))) {
				log_err ("pthread_create() failed: %s", strerror (err));
			}
		}

		pthread_attr_destroy (&ptattr);
	}

	while (1) {
		int remote = 0;

		conn_t *connection;

		pthread_mutex_lock (&available_mutex);

		while (0 == available_collectors) {
			pthread_cond_wait (&collector_available, &available_mutex);
		}

		--available_collectors;

		pthread_mutex_unlock (&available_mutex);

		do {
			errno = 0;
			if (-1 == (remote = accept (connector_socket, NULL, NULL))) {
				if (EINTR != errno) {
					disabled = 1;
					log_err ("accept() failed: %s", strerror (errno));
					pthread_exit ((void *)1);
				}
			}
		} while (EINTR == errno);

		connection = (conn_t *)smalloc (sizeof (conn_t));

		connection->socket = remote;
		connection->next   = NULL;

		pthread_mutex_lock (&conns_mutex);

		if (NULL == conns.head) {
			conns.head = connection;
			conns.tail = connection;
		}
		else {
			conns.tail->next = connection;
			conns.tail = conns.tail->next;
		}

		pthread_mutex_unlock (&conns_mutex);

		pthread_cond_signal (&conn_available);
	}
	pthread_exit ((void *)0);
} /* static void *open_connection (void *) */

static int email_init (void)
{
	int err = 0;

	if (0 != (err = pthread_create (&connector, NULL,
				open_connection, NULL))) {
		disabled = 1;
		log_err ("pthread_create() failed: %s", strerror (err));
		return (-1);
	}

	return (0);
} /* int email_init */

static int email_shutdown (void)
{
	int i = 0;

	if (disabled)
		return (0);

	pthread_kill (connector, SIGTERM);
	close (connector_socket);

	/* don't allow any more connections to be processed */
	pthread_mutex_lock (&conns_mutex);

	for (i = 0; i < max_conns; ++i) {
		pthread_kill (collectors[i]->thread, SIGTERM);
		close (collectors[i]->socket);
	}

	pthread_mutex_unlock (&conns_mutex);

	unlink (SOCK_PATH);

	return (0);
} /* static void email_shutdown (void) */

static void email_submit (const char *type, const char *type_instance, gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "email");
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (type, &vl);
} /* void email_submit */

/* Copy list l1 to list l2. l2 may partly exist already, but it is assumed
 * that neither the order nor the name of any element of either list is
 * changed and no elements are deleted. The values of l1 are reset to zero
 * after they have been copied to l2. */
static void copy_type_list (type_list_t *l1, type_list_t *l2)
{
	type_t *ptr1;
	type_t *ptr2;

	type_t *last = NULL;

	for (ptr1 = l1->head, ptr2 = l2->head; NULL != ptr1;
			ptr1 = ptr1->next, last = ptr2, ptr2 = ptr2->next) {
		if (NULL == ptr2) {
			ptr2 = (type_t *)smalloc (sizeof (type_t));
			ptr2->name = NULL;
			ptr2->next = NULL;

			if (NULL == last) {
				l2->head = ptr2;
			}
			else {
				last->next = ptr2;
			}

			l2->tail = ptr2;
		}

		if (NULL == ptr2->name) {
			ptr2->name = sstrdup (ptr1->name);
		}

		ptr2->value = ptr1->value;
		ptr1->value = 0;
	}
	return;
}

static int email_read (void)
{
	type_t *ptr;

	double sc;

	static type_list_t *cnt;
	static type_list_t *sz;
	static type_list_t *chk;

	if (disabled)
		return (-1);

	if (NULL == cnt) {
		cnt = (type_list_t *)smalloc (sizeof (type_list_t));
		cnt->head = NULL;
	}

	if (NULL == sz) {
		sz = (type_list_t *)smalloc (sizeof (type_list_t));
		sz->head = NULL;
	}

	if (NULL == chk) {
		chk = (type_list_t *)smalloc (sizeof (type_list_t));
		chk->head = NULL;
	}

	/* email count */
	pthread_mutex_lock (&count_mutex);

	copy_type_list (&count, cnt);

	pthread_mutex_unlock (&count_mutex);

	for (ptr = cnt->head; NULL != ptr; ptr = ptr->next) {
		email_submit ("email_count", ptr->name, ptr->value);
	}

	/* email size */
	pthread_mutex_lock (&size_mutex);

	copy_type_list (&size, sz);

	pthread_mutex_unlock (&size_mutex);

	for (ptr = sz->head; NULL != ptr; ptr = ptr->next) {
		email_submit ("email_size", ptr->name, ptr->value);
	}

	/* spam score */
	pthread_mutex_lock (&score_mutex);

	sc = score;
	score = 0.0;
	score_count = 0;

	pthread_mutex_unlock (&score_mutex);

	email_submit ("spam_score", "", sc);

	/* spam checks */
	pthread_mutex_lock (&check_mutex);

	copy_type_list (&check, chk);

	pthread_mutex_unlock (&check_mutex);

	for (ptr = chk->head; NULL != ptr; ptr = ptr->next)
		email_submit ("spam_check", ptr->name, ptr->value);

	return (0);
} /* int email_read */

void module_register (void)
{
	plugin_register_data_set (&email_count_ds);
	plugin_register_data_set (&email_size_ds);
	plugin_register_data_set (&spam_check_ds);
	plugin_register_data_set (&spam_score_ds);

	plugin_register_config ("email", email_config, config_keys, config_keys_num);
	plugin_register_init ("email", email_init);
	plugin_register_read ("email", email_read);
	plugin_register_shutdown ("email", email_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

