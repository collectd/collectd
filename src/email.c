/**
 * collectd - src/email.c
 * Copyright (C) 2006,2007  Sebastian Harl
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

#include <stddef.h>

#if HAVE_LIBPTHREAD
# include <pthread.h>
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

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

#define SOCK_PATH LOCALSTATEDIR"/run/"PACKAGE_NAME"-email"
#define MAX_CONNS 5
#define MAX_CONNS_LIMIT 16384

#define log_err(...) ERROR (MODULE_NAME": "__VA_ARGS__)
#define log_warn(...) WARNING (MODULE_NAME": "__VA_ARGS__)

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
	"SocketFile",
	"SocketGroup",
	"SocketPerms",
	"MaxConns"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/* socket configuration */
static char *sock_file  = SOCK_PATH;
static char *sock_group = COLLECTD_GRP_NAME;
static int  sock_perms  = S_IRWXU | S_IRWXG;
static int  max_conns   = MAX_CONNS;

/* state of the plugin */
static int disabled = 0;

/* thread managing "client" connections */
static pthread_t connector = (pthread_t) 0;
static int connector_socket = -1;

/* tell the collector threads that a new connection is available */
static pthread_cond_t conn_available = PTHREAD_COND_INITIALIZER;

/* connections that are waiting to be processed */
static pthread_mutex_t conns_mutex = PTHREAD_MUTEX_INITIALIZER;
static conn_list_t conns;

/* tell the connector thread that a collector is available */
static pthread_cond_t collector_available = PTHREAD_COND_INITIALIZER;

/* collector threads */
static collector_t **collectors = NULL;

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
	if (0 == strcasecmp (key, "SocketFile")) {
		sock_file = sstrdup (value);
	}
	else if (0 == strcasecmp (key, "SocketGroup")) {
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
		char errbuf[1024];
		log_err ("select() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return '\0';
	}

	assert (FD_ISSET (src->socket, &fdset));

	do {
		ssize_t len = 0;

		errno = 0;
		if (0 > (len = read (src->socket, (void *)&ret, 1))) {
			if (EINTR != errno) {
				char errbuf[1024];
				log_err ("read() failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
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
			char errbuf[1024];
			log_err ("select() failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return NULL;
		}

		assert (FD_ISSET (src->socket, &fdset));

		do {
			errno = 0;
			if (0 > (len = read (src->socket,
							(void *)(src->buffer + src->idx),
							BUFSIZE - src->idx))) {
				if (EINTR != errno) {
					char errbuf[1024];
					log_err ("read() failed: %s",
							sstrerror (errno, errbuf, sizeof (errbuf)));
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
				char errbuf[1024];
				log_err ("fcntl() failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				loop = 0;
			}

			errno = 0;
			if (-1 == fcntl (connection->socket, F_SETFL, flags | O_NONBLOCK)) {
				char errbuf[1024];
				log_err ("fcntl() failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
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

		this->socket = -1;

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
		char errbuf[1024];
		disabled = 1;
		log_err ("socket() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		pthread_exit ((void *)1);
	}

	addr.sun_family = AF_UNIX;

	strncpy (addr.sun_path, sock_file, (size_t)(UNIX_PATH_MAX - 1));
	addr.sun_path[UNIX_PATH_MAX - 1] = '\0';
	unlink (addr.sun_path);

	errno = 0;
	if (-1 == bind (connector_socket, (struct sockaddr *)&addr,
				offsetof (struct sockaddr_un, sun_path)
					+ strlen(addr.sun_path))) {
		char errbuf[1024];
		disabled = 1;
		connector_socket = -1; /* TODO: close? */
		log_err ("bind() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		pthread_exit ((void *)1);
	}

	errno = 0;
	if (-1 == listen (connector_socket, 5)) {
		char errbuf[1024];
		disabled = 1;
		connector_socket = -1; /* TODO: close? */
		log_err ("listen() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
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
			char errbuf[1024];
			log_warn ("getgrnam_r (%s) failed: %s", sock_group,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}
		else if (grp == NULL)
		{
			log_warn ("No such group: `%s'", sock_group);
		}
		else
		{
			status = chown (sock_file, (uid_t) -1, grp->gr_gid);
			if (status != 0)
			{
				char errbuf[1024];
				log_warn ("chown (%s, -1, %i) failed: %s",
						sock_file, (int) grp->gr_gid,
						sstrerror (errno, errbuf, sizeof (errbuf)));
			}
		}
	}
	else /* geteuid != 0 */
	{
		log_warn ("not running as root");
	}

	errno = 0;
	if (0 != chmod (sock_file, sock_perms)) {
		char errbuf[1024];
		log_warn ("chmod() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
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
			collectors[i]->socket = -1;

			if (0 != (err = pthread_create (&collectors[i]->thread, &ptattr,
							collect, collectors[i]))) {
				char errbuf[1024];
				log_err ("pthread_create() failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				collectors[i]->thread = (pthread_t) 0;
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
					char errbuf[1024];
					disabled = 1;
					connector_socket = -1; /* TODO: close? */
					log_err ("accept() failed: %s",
							sstrerror (errno, errbuf, sizeof (errbuf)));
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
		char errbuf[1024];
		disabled = 1;
		log_err ("pthread_create() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	return (0);
} /* int email_init */

static int email_shutdown (void)
{
	int i = 0;

	if (connector != ((pthread_t) 0)) {
		pthread_kill (connector, SIGTERM);
		connector = (pthread_t) 0;
	}

	if (connector_socket >= 0) {
		close (connector_socket);
		connector_socket = -1;
	}

	/* don't allow any more connections to be processed */
	pthread_mutex_lock (&conns_mutex);

	if (collectors != NULL) {
		for (i = 0; i < max_conns; ++i) {
			if (collectors[i] == NULL)
				continue;

			if (collectors[i]->thread != ((pthread_t) 0)) {
				pthread_kill (collectors[i]->thread, SIGTERM);
				collectors[i]->thread = (pthread_t) 0;
			}

			if (collectors[i]->socket >= 0) {
				close (collectors[i]->socket);
				collectors[i]->socket = -1;
			}
		}
	} /* if (collectors != NULL) */

	pthread_mutex_unlock (&conns_mutex);

	unlink (sock_file);
	errno = 0;

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
	strncpy (vl.type, type, sizeof (vl.type));
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
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

	double score_old;
	int score_count_old;

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

	score_old = score;
	score_count_old = score_count;
	score = 0.0;
	score_count = 0;

	pthread_mutex_unlock (&score_mutex);

	if (score_count_old > 0)
		email_submit ("spam_score", "", score_old);

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
	plugin_register_config ("email", email_config, config_keys, config_keys_num);
	plugin_register_init ("email", email_init);
	plugin_register_read ("email", email_read);
	plugin_register_shutdown ("email", email_shutdown);
} /* void module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
