/**
 * collectd - src/email.c
 * Copyright (C) 2006-2008  Sebastian Harl
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

#define SOCK_PATH LOCALSTATEDIR"/run/"PACKAGE_NAME"-email"
#define MAX_CONNS 5
#define MAX_CONNS_LIMIT 16384

#define log_debug(...) DEBUG ("email: "__VA_ARGS__)
#define log_err(...) ERROR ("email: "__VA_ARGS__)
#define log_warn(...) WARNING ("email: "__VA_ARGS__)

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
	FILE *socket;
} collector_t;

/* linked list of pending connections */
typedef struct conn {
	/* socket to read data from */
	FILE *socket;

	/* linked list of connections */
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
static char *sock_file  = NULL;
static char *sock_group = NULL;
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
static type_list_t list_count;
static type_list_t list_count_copy;

static pthread_mutex_t size_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t list_size;
static type_list_t list_size_copy;

static pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
static double score;
static int score_count;

static pthread_mutex_t check_mutex = PTHREAD_MUTEX_INITIALIZER;
static type_list_t list_check;
static type_list_t list_check_copy;

/*
 * Private functions
 */
static int email_config (const char *key, const char *value)
{
	if (0 == strcasecmp (key, "SocketFile")) {
		if (NULL != sock_file)
			free (sock_file);
		sock_file = sstrdup (value);
	}
	else if (0 == strcasecmp (key, "SocketGroup")) {
		if (NULL != sock_group)
			free (sock_group);
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
			ERROR ("email plugin: `MaxConns' was set to invalid "
					"value %li, will use default %i.\n",
					tmp, MAX_CONNS);
			max_conns = MAX_CONNS;
		}
		else if (tmp > MAX_CONNS_LIMIT) {
			fprintf (stderr, "email plugin: `MaxConns' was set to invalid "
					"value %li, will use hardcoded limit %i.\n",
					tmp, MAX_CONNS_LIMIT);
			ERROR ("email plugin: `MaxConns' was set to invalid "
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

static void *collect (void *arg)
{
	collector_t *this = (collector_t *)arg;

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

		pthread_mutex_unlock (&conns_mutex);

		/* make the socket available to the global
		 * thread and connection management */
		this->socket = connection->socket;

		log_debug ("collect: handling connection on fd #%i",
				fileno (this->socket));

		while (loop) {
			/* 256 bytes ought to be enough for anybody ;-) */
			char line[256 + 1]; /* line + '\0' */
			int  len = 0;

			errno = 0;
			if (NULL == fgets (line, sizeof (line), this->socket)) {
				loop = 0;

				if (0 != errno) {
					char errbuf[1024];
					log_err ("collect: reading from socket (fd #%i) "
							"failed: %s", fileno (this->socket),
							sstrerror (errno, errbuf, sizeof (errbuf)));
				}
				break;
			}

			len = strlen (line);
			if (('\n' != line[len - 1]) && ('\r' != line[len - 1])) {
				log_warn ("collect: line too long (> %zu characters): "
						"'%s' (truncated)", sizeof (line) - 1, line);

				while (NULL != fgets (line, sizeof (line), this->socket))
					if (('\n' == line[len - 1]) || ('\r' == line[len - 1]))
						break;
				continue;
			}

			line[len - 1] = '\0';

			log_debug ("collect: line = '%s'", line);

			if (':' != line[1]) {
				log_err ("collect: syntax error in line '%s'", line);
				continue;
			}

			if ('e' == line[0]) { /* e:<type>:<bytes> */
				char *ptr  = NULL;
				char *type = strtok_r (line + 2, ":", &ptr);
				char *tmp  = strtok_r (NULL, ":", &ptr);
				int  bytes = 0;

				if (NULL == tmp) {
					log_err ("collect: syntax error in line '%s'", line);
					continue;
				}

				bytes = atoi (tmp);

				pthread_mutex_lock (&count_mutex);
				type_list_incr (&list_count, type, 1);
				pthread_mutex_unlock (&count_mutex);

				if (bytes > 0) {
					pthread_mutex_lock (&size_mutex);
					type_list_incr (&list_size, type, bytes);
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
					type_list_incr (&list_check, type, 1);
					pthread_mutex_unlock (&check_mutex);
				} while (NULL != (type = strtok_r (NULL, ",", &ptr)));
			}
			else {
				log_err ("collect: unknown type '%c'", line[0]);
			}
		} /* while (loop) */

		log_debug ("Shutting down connection on fd #%i",
				fileno (this->socket));

		fclose (connection->socket);
		free (connection);

		this->socket = NULL;

		pthread_mutex_lock (&available_mutex);
		++available_collectors;
		pthread_mutex_unlock (&available_mutex);

		pthread_cond_signal (&collector_available);
	} /* while (1) */

	pthread_exit ((void *)0);
} /* static void *collect (void *) */

static void *open_connection (void __attribute__((unused)) *arg)
{
	struct sockaddr_un addr;

	char *path  = (NULL == sock_file) ? SOCK_PATH : sock_file;
	char *group = (NULL == sock_group) ? COLLECTD_GRP_NAME : sock_group;

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
	sstrncpy (addr.sun_path, path, (size_t)(UNIX_PATH_MAX - 1));

	errno = 0;
	if (-1 == bind (connector_socket, (struct sockaddr *)&addr,
				offsetof (struct sockaddr_un, sun_path)
					+ strlen(addr.sun_path))) {
		char errbuf[1024];
		disabled = 1;
		close (connector_socket);
		connector_socket = -1;
		log_err ("bind() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		pthread_exit ((void *)1);
	}

	errno = 0;
	if (-1 == listen (connector_socket, 5)) {
		char errbuf[1024];
		disabled = 1;
		close (connector_socket);
		connector_socket = -1;
		log_err ("listen() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		pthread_exit ((void *)1);
	}

	{
		struct group sg;
		struct group *grp;
		char grbuf[2048];
		int status;

		grp = NULL;
		status = getgrnam_r (group, &sg, grbuf, sizeof (grbuf), &grp);
		if (status != 0)
		{
			char errbuf[1024];
			log_warn ("getgrnam_r (%s) failed: %s", group,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}
		else if (grp == NULL)
		{
			log_warn ("No such group: `%s'", group);
		}
		else
		{
			status = chown (path, (uid_t) -1, grp->gr_gid);
			if (status != 0)
			{
				char errbuf[1024];
				log_warn ("chown (%s, -1, %i) failed: %s",
						path, (int) grp->gr_gid,
						sstrerror (errno, errbuf, sizeof (errbuf)));
			}
		}
	}

	errno = 0;
	if (0 != chmod (path, sock_perms)) {
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
			collectors[i]->socket = NULL;

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
					close (connector_socket);
					connector_socket = -1;
					log_err ("accept() failed: %s",
							sstrerror (errno, errbuf, sizeof (errbuf)));
					pthread_exit ((void *)1);
				}
			}
		} while (EINTR == errno);

		connection = (conn_t *)smalloc (sizeof (conn_t));

		connection->socket = fdopen (remote, "r");
		connection->next   = NULL;

		if (NULL == connection->socket) {
			close (remote);
			continue;
		}

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
	type_t *ptr = NULL;

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

	available_collectors = 0;

	if (collectors != NULL) {
		for (i = 0; i < max_conns; ++i) {
			if (collectors[i] == NULL)
				continue;

			if (collectors[i]->thread != ((pthread_t) 0)) {
				pthread_kill (collectors[i]->thread, SIGTERM);
				collectors[i]->thread = (pthread_t) 0;
			}

			if (collectors[i]->socket != NULL) {
				fclose (collectors[i]->socket);
				collectors[i]->socket = NULL;
			}

			sfree (collectors[i]);
		}
		sfree (collectors);
	} /* if (collectors != NULL) */

	pthread_mutex_unlock (&conns_mutex);

	for (ptr = list_count.head; NULL != ptr; ptr = ptr->next) {
		free (ptr->name);
		free (ptr);
	}

	for (ptr = list_count_copy.head; NULL != ptr; ptr = ptr->next) {
		free (ptr->name);
		free (ptr);
	}

	for (ptr = list_size.head; NULL != ptr; ptr = ptr->next) {
		free (ptr->name);
		free (ptr);
	}

	for (ptr = list_size_copy.head; NULL != ptr; ptr = ptr->next) {
		free (ptr->name);
		free (ptr);
	}

	for (ptr = list_check.head; NULL != ptr; ptr = ptr->next) {
		free (ptr->name);
		free (ptr);
	}

	for (ptr = list_check_copy.head; NULL != ptr; ptr = ptr->next) {
		free (ptr->name);
		free (ptr);
	}

	unlink ((NULL == sock_file) ? SOCK_PATH : sock_file);

	sfree (sock_file);
	sfree (sock_group);
	return (0);
} /* static void email_shutdown (void) */

static void email_submit (const char *type, const char *type_instance, gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "email", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

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

	if (disabled)
		return (-1);

	/* email count */
	pthread_mutex_lock (&count_mutex);

	copy_type_list (&list_count, &list_count_copy);

	pthread_mutex_unlock (&count_mutex);

	for (ptr = list_count_copy.head; NULL != ptr; ptr = ptr->next) {
		email_submit ("email_count", ptr->name, ptr->value);
	}

	/* email size */
	pthread_mutex_lock (&size_mutex);

	copy_type_list (&list_size, &list_size_copy);

	pthread_mutex_unlock (&size_mutex);

	for (ptr = list_size_copy.head; NULL != ptr; ptr = ptr->next) {
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

	copy_type_list (&list_check, &list_check_copy);

	pthread_mutex_unlock (&check_mutex);

	for (ptr = list_check_copy.head; NULL != ptr; ptr = ptr->next)
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
