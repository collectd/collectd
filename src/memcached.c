/**
 * collectd - src/memcached.c
 * Copyright (C) 2007  Antony Dovgal, heavily based on hddtemp.c
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
 * Authors:
 *   Antony Dovgal <tony at daylessday dot org>
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

# include <poll.h>
# include <netdb.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <libgen.h> /* for basename */

#if HAVE_LINUX_MAJOR_H
# include <linux/major.h>
#endif

#define MEMCACHED_DEF_HOST "127.0.0.1"
#define MEMCACHED_DEF_PORT "11211"

#define MEMCACHED_RETRY_COUNT 100

static const char *config_keys[] =
{
	"Host",
	"Port",
	NULL
};
static int config_keys_num = 2;

static char *memcached_host = NULL;
static char memcached_port[16];

static int memcached_query_daemon (char *buffer, int buffer_size) /* {{{ */
{
	int fd;
	ssize_t status;
	int buffer_fill;

	const char *host;
	const char *port;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return, i = 0;

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = 0;
#ifdef AI_ADDRCONFIG
/*	ai_hints.ai_flags   |= AI_ADDRCONFIG; */
#endif
	ai_hints.ai_family   = AF_INET;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = 0;

	host = memcached_host;
	if (host == NULL) {
		host = MEMCACHED_DEF_HOST;
	}

	port = memcached_port;
	if (strlen (port) == 0) {
		port = MEMCACHED_DEF_PORT;
	}

	if ((ai_return = getaddrinfo (host, port, NULL, &ai_list)) != 0) {
		char errbuf[1024];
		ERROR ("memcached: getaddrinfo (%s, %s): %s",
				host, port,
				(ai_return == EAI_SYSTEM)
				? sstrerror (errno, errbuf, sizeof (errbuf))
				: gai_strerror (ai_return));
		return -1;
	}

	fd = -1;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
		/* create our socket descriptor */
		if ((fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol)) < 0) {
			char errbuf[1024];
			ERROR ("memcached: socket: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		/* connect to the memcached daemon */
		if (connect (fd, (struct sockaddr *) ai_ptr->ai_addr, ai_ptr->ai_addrlen)) {
			char errbuf[1024];
			shutdown(fd, SHUT_RDWR);
			close(fd);
			fd = -1;
			continue;
		}

		/* A socket could be opened and connecting succeeded. We're
		 * done. */
		break;
	}

	freeaddrinfo (ai_list);

	if (fd < 0) {
		ERROR ("memcached: Could not connect to daemon.");
		return -1;
	}

	if (send(fd, "stats\r\n", sizeof("stats\r\n") - 1, MSG_DONTWAIT) != (sizeof("stats\r\n") - 1)) {
		ERROR ("memcached: Could not send command to the memcached daemon.");
		return -1;
	}

	{
		struct pollfd p;
		int n;

		p.fd = fd;
		p.events = POLLIN|POLLERR|POLLHUP;
		p.revents = 0;

		n = poll(&p, 1, 3);

		if (n <= 0) {
			ERROR ("memcached: poll() failed or timed out");
			return -1;
		}
	}

	/* receive data from the memcached daemon */
	memset (buffer, '\0', buffer_size);

	buffer_fill = 0;
	while ((status = recv (fd, buffer + buffer_fill, buffer_size - buffer_fill, MSG_DONTWAIT)) != 0) {
		if (i > MEMCACHED_RETRY_COUNT) {
			ERROR("recv() timed out");
			break;
		}
		i++;

		if (status == -1) {
			char errbuf[1024];

			if (errno == EAGAIN) {
				continue;
			}

			ERROR ("memcached: Error reading from socket: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			shutdown(fd, SHUT_RDWR);
			close (fd);
			return -1;
		}
		buffer_fill += status;

		if (buffer_fill > 3 && buffer[buffer_fill-5] == 'E' && buffer[buffer_fill-4] == 'N' && buffer[buffer_fill-3] == 'D') {
			/* we got all the data */
			break;
		}
	}

	if (buffer_fill >= buffer_size) {
		buffer[buffer_size - 1] = '\0';
		WARNING ("memcached: Message from memcached has been truncated.");
	} else if (buffer_fill == 0) {
		WARNING ("memcached: Peer has unexpectedly shut down the socket. "
				"Buffer: `%s'", buffer);
		shutdown(fd, SHUT_RDWR);
		close(fd);
		return -1;
	}

	shutdown(fd, SHUT_RDWR);
	close(fd);
	return 0;
}
/* }}} */

static int memcached_config (const char *key, const char *value) /* {{{ */
{
	if (strcasecmp (key, "Host") == 0) {
		if (memcached_host != NULL) {
			free (memcached_host);
		}
		memcached_host = strdup (value);
	} else if (strcasecmp (key, "Port") == 0) {
		int port = (int) (atof (value));
		if ((port > 0) && (port <= 65535)) {
			snprintf (memcached_port, sizeof (memcached_port), "%i", port);
		} else {
			strncpy (memcached_port, value, sizeof (memcached_port));
		}
		memcached_port[sizeof (memcached_port) - 1] = '\0';
	} else {
		return -1;
	}

	return 0;
}
/* }}} */

#if 0
static void memcached_submit_items(double curr_items, unsigned long long total_items) /* {{{ */
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = curr_items;
	values[1].counter = total_items;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "memcached");

	plugin_dispatch_values ("memcached_items", &vl);
}
/* }}} */
#endif

static void memcached_submit_connections(double curr_connections, unsigned long long total_connections) /* {{{ */
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = curr_connections;
	values[1].counter = total_connections;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "memcached");

	plugin_dispatch_values ("memcached_connections", &vl);
}
/* }}} */

static void memcached_submit_bytes(unsigned long long bytes_read, unsigned long long bytes_written) /* {{{ */
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = bytes_read;
	values[1].counter = bytes_written;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "memcached");

	plugin_dispatch_values ("memcached_bytes", &vl);
}
/* }}} */

static void memcached_submit_cmd(unsigned long long cmd_get, unsigned long long cmd_set, unsigned long long get_hits, unsigned long long get_misses) /* {{{ */
{
	value_t values[4];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = cmd_get;
	values[1].counter = cmd_set;
	values[2].counter = get_hits;
	values[3].counter = get_misses;

	vl.values = values;
	vl.values_len = 4;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "memcached");

	plugin_dispatch_values ("memcached_cmd", &vl);
}
/* }}} */

static void memcached_submit_rusage(unsigned long long rusage_user, unsigned long long rusage_system) /* {{{ */
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = rusage_user;
	values[1].counter = rusage_system;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "memcached");

	plugin_dispatch_values ("memcached_rusage", &vl);
}
/* }}} */

static int memcached_read (void) /* {{{ */
{
	char buf[1024];
	char *lines[128];
	char *fields[3];
	char *ptr;
	char *saveptr;
	int fields_num;
	int lines_num = 0;
	int i;
	unsigned long long total_connections = 0, bytes_read = 0, bytes_written = 0, cmd_get = 0, cmd_set = 0, get_hits = 0, get_misses = 0, rusage_user = 0, rusage_system = 0;
	double curr_connections = 0;

	/* get data from daemon */
	if (memcached_query_daemon (buf, sizeof (buf)) < 0) {
		return -1;
	}

    ptr = buf;
    saveptr = NULL;
    while ((lines[lines_num] = strtok_r (ptr, "\n\r", &saveptr)) != NULL) {
        ptr = NULL;
        lines_num++;

        if (lines_num >= 127) break;
    }

#define FIELD_IS(cnst) \
	(sizeof(cnst) - 1) == name_len && memcmp(cnst, fields[1], sizeof(cnst)) == 0

	for (i = 0; i < lines_num; i++) {
		int name_len;

		fields_num = strsplit(lines[i], fields, 3);
		if (fields_num != 3) continue;

		name_len = strlen(fields[1]);
		if (name_len == 0) continue;

		if (FIELD_IS("rusage_user")) {
			rusage_user = atoll(fields[2]);
		} else if (FIELD_IS("rusage_system")) {
			rusage_system = atoll(fields[2]);
/*		} else if (FIELD_IS("curr_items")) {
			curr_items = atof(fields[2]);
		} else if (FIELD_IS("total_items")) {
			total_items = atoll(fields[2]);
		} else if (FIELD_IS("bytes")) {
			 bytes = atof(fields[2]); */
		} else if (FIELD_IS("curr_connections")) {
			curr_connections = atof(fields[2]);
		} else if (FIELD_IS("total_connections")) {
			total_connections = atoll(fields[2]);
/*		} else if (FIELD_IS("connection_structures")) {
			connection_structures = atof(fields[2]); */
		} else if (FIELD_IS("cmd_get")) {
			cmd_get = atoll(fields[2]);
		} else if (FIELD_IS("cmd_set")) {
			cmd_set = atoll(fields[2]);
		} else if (FIELD_IS("get_hits")) {
			get_hits = atoll(fields[2]);
		} else if (FIELD_IS("get_misses")) {
			get_misses = atoll(fields[2]);
/*		} else if (FIELD_IS("evictions")) {
			evictions = atoll(fields[2]); */
		} else if (FIELD_IS("bytes_read")) {
			bytes_read = atoll(fields[2]);
		} else if (FIELD_IS("bytes_written")) {
			bytes_written = atoll(fields[2]);
/*		} else if (FIELD_IS("limit_maxbytes")) {
			limit_maxbytes = atof(fields[2]);
		} else if (FIELD_IS("threads")) {
			threads = atof(fields[2]);  */
		}
	}

#if 0
	memcached_submit_items(curr_items, total_items);
#endif
	memcached_submit_connections(curr_connections, total_connections);
	memcached_submit_bytes(bytes_read, bytes_written);
	memcached_submit_cmd(cmd_get, cmd_set, get_hits, get_misses);
	memcached_submit_rusage(rusage_user, rusage_system);

	return 0;
}
/* }}} */

void module_register (void) /* {{{ */
{
	plugin_register_config ("memcached", memcached_config, config_keys, config_keys_num);
	plugin_register_read ("memcached", memcached_read);
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */

