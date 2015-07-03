/**
 * collectd - src/zookeeper.c
 * Copyright (C) 2014       Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Jeremy Katz <jeremy at katzbox.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define ZOOKEEPER_DEF_HOST "127.0.0.1"
#define ZOOKEEPER_DEF_PORT "2181"

static char *zk_host = NULL;
static char *zk_port = NULL;

static const char *config_keys[] =
{
	"Host",
	"Port"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int zookeeper_config(const char *key, const char *value)
{
	if (strncmp(key, "Host", strlen("Host")) == 0)
	{
		sfree (zk_host);
		zk_host = strdup (value);
	}
	else if (strncmp(key, "Port", strlen("Port")) == 0)
	{
		sfree (zk_port);
		zk_port = strdup (value);
	}
	else
	{
		return -1;
	}
	return 0;
}

static void zookeeper_submit_gauge (const char * type, const char * type_inst, gauge_t val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = val;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "zookeeper", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_inst != NULL)
		sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* zookeeper_submit_gauge */

static void zookeeper_submit_derive (const char * type, const char * type_inst, derive_t val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = val;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "zookeeper", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_inst != NULL)
		sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* zookeeper_submit_derive */

static int zookeeper_connect (void)
{
	int sk = -1;
	int status;
	struct addrinfo ai_hints;
	struct addrinfo *ai;
	struct addrinfo *ai_list;
	char *host;
	char *port;

	memset ((void *) &ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_family   = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;

	host = (zk_host != NULL) ? zk_host : ZOOKEEPER_DEF_HOST;
	port = (zk_port != NULL) ? zk_port : ZOOKEEPER_DEF_PORT;
	status = getaddrinfo (host, port, &ai_hints, &ai_list);
	if (status != 0)
	{
		char errbuf[1024];
		INFO ("getaddrinfo failed: %s",
			  (status == EAI_SYSTEM)
			  ? sstrerror (errno, errbuf, sizeof (errbuf))
			  : gai_strerror (status));
		return (-1);
	}

	for (ai = ai_list; ai != NULL; ai = ai->ai_next)
	{
		sk = socket (ai->ai_family, SOCK_STREAM, 0);
		if (sk < 0)
		{
			char errbuf[1024];
			WARNING ("zookeeper: socket(2) failed: %s",
					 sstrerror (errno, errbuf, sizeof(errbuf)));
			continue;
		}
		status = (int) connect (sk, ai->ai_addr, ai->ai_addrlen);
		if (status != 0)
		{
			char errbuf[1024];
			close (sk);
			sk = -1;
			WARNING ("zookeeper: connect(2) failed: %s",
					 sstrerror (errno, errbuf, sizeof(errbuf)));
			continue;
		}

		/* connected */
		break;
	}

	freeaddrinfo(ai_list);
	return (sk);
} /* int zookeeper_connect */

static int zookeeper_query (char *buffer, size_t buffer_size)
{
	int sk = -1;
	int status;
	size_t buffer_fill;

	sk = zookeeper_connect();
	if (sk < 0)
	{
		ERROR ("zookeeper: Could not connect to daemon");
		return (-1);
	}

	status = (int) swrite (sk, "mntr\r\n", strlen("mntr\r\n"));
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("zookeeper: write(2) failed: %s",
			   sstrerror (errno, errbuf, sizeof (errbuf)));
		close (sk);
		return (-1);
	}

	memset (buffer, 0, buffer_size);
	buffer_fill = 0;

	while ((status = (int) recv (sk, buffer + buffer_fill,
          buffer_size - buffer_fill, /* flags = */ 0)) != 0)
	{
		if (status < 0)
		{
			char errbuf[1024];
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			ERROR ("zookeeper: Error reading from socket: %s",
				   sstrerror (errno, errbuf, sizeof (errbuf)));
			close (sk);
			return (-1);
		}

		buffer_fill += (size_t) status;
		if (status == 0)
		{
			/* done reading from the socket */
			break;
		}
	} /* while (recv) */

	status = 0;
	if (buffer_fill == 0)
	{
		WARNING ("zookeeper: No data returned by MNTR command.");
		status = -1;
	}

	close(sk);
	return (status);
} /* int zookeeper_query */


static int zookeeper_read (void) {
	char buf[4096];
	char *ptr;
	char *save_ptr;
	char *line;
	char *fields[2];

	if (zookeeper_query (buf, sizeof (buf)) < 0)
	{
		return (-1);
	}

	ptr = buf;
	save_ptr = NULL;
	while ((line = strtok_r (ptr, "\n\r", &save_ptr)) != NULL)
	{
		ptr = NULL;
		if (strsplit(line, fields, 2) != 2)
		{
			continue;
		}
#define FIELD_CHECK(check, expected) \
	(strncmp (check, expected, strlen(expected)) == 0)

		if (FIELD_CHECK (fields[0], "zk_avg_latency"))
		{
			zookeeper_submit_gauge ("latency", "avg", atol(fields[1]));
		}
		else if (FIELD_CHECK(fields[0], "zk_min_latency"))
		{
			zookeeper_submit_gauge ("latency", "min", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_max_latency"))
		{
			zookeeper_submit_gauge ("latency", "max", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_packets_received"))
		{
			zookeeper_submit_derive ("packets", "received", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_packets_sent"))
		{
			zookeeper_submit_derive ("packets", "sent", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_num_alive_connections"))
		{
			zookeeper_submit_gauge ("current_connections", NULL, atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_outstanding_requests"))
		{
			zookeeper_submit_gauge ("requests", "outstanding", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_znode_count"))
		{
			zookeeper_submit_gauge ("gauge", "znode", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_watch_count"))
		{
			zookeeper_submit_gauge ("gauge", "watch", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_ephemerals_count"))
		{
			zookeeper_submit_gauge ("gauge", "ephemerals", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_ephemerals_count"))
		{
			zookeeper_submit_gauge ("gauge", "ephemerals", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_ephemerals_count"))
		{
			zookeeper_submit_gauge ("gauge", "ephemerals", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_approximate_data_size"))
		{
			zookeeper_submit_gauge ("bytes", "approximate_data_size", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_followers"))
		{
			zookeeper_submit_gauge ("count", "followers", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_synced_followers"))
		{
			zookeeper_submit_gauge ("count", "synced_followers", atol(fields[1]));
		}
		else if (FIELD_CHECK (fields[0], "zk_pending_syncs"))
		{
			zookeeper_submit_gauge ("count", "pending_syncs", atol(fields[1]));
		}
		else
		{
			DEBUG("Uncollected zookeeper MNTR field %s", fields[0]);
		}
	}

	return (0);
} /* zookeeper_read */

void module_register (void)
{
	plugin_register_config ("zookeeper", zookeeper_config, config_keys, config_keys_num);
	plugin_register_read ("zookeeper", zookeeper_read);
} /* void module_register */
