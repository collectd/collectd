/**
 * collectd - src/dns.c
 * Copyright (C) 2006  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_debug.h"

#if HAVE_SYS_POLL_H
# include <sys/poll.h>
#endif

#define MODULE_NAME "dns"

#if HAVE_LIBPCAP
# define NAMED_HAVE_CONFIG 1
#else
# define NAMED_HAVE_CONFIG 0
#endif

#if HAVE_LIBPCAP
# include "utils_dns.h"
# define NAMED_HAVE_READ 1
#else
# define NAMED_HAVE_READ 0
#endif

struct counter_list_s
{
	unsigned int key;
	unsigned int value;
	struct counter_list_s *next;
};
typedef struct counter_list_s counter_list_t;

static char *traffic_file   = "dns/dns_traffic.rrd";
static char *qtype_file   = "dns/qtype-%s.rrd";
static char *opcode_file  = "dns/opcode-%s.rrd";
static char *rcode_file   = "dns/rcode-%s.rrd";

static char *traffic_ds_def[] =
{
	/* Limit to 1GBit/s */
	"DS:queries:COUNTER:"COLLECTD_HEARTBEAT":0:125000000",
	"DS:responses:COUNTER:"COLLECTD_HEARTBEAT":0:125000000",
	NULL
};
static int traffic_ds_num = 2;

static char *qtype_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int qtype_ds_num = 1;

static char *opcode_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int opcode_ds_num = 1;

static char *rcode_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int rcode_ds_num = 1;

#if NAMED_HAVE_CONFIG
#if HAVE_LIBPCAP
static char *config_keys[] =
{
	"Interface",
	"IgnoreSource",
	NULL
};
static int config_keys_num = 2;
#endif /* HAVE_LIBPCAP */
#endif /* NAMED_HAVE_CONFIG */

#if HAVE_LIBPCAP
#define PCAP_SNAPLEN 1460
static char   *pcap_device = NULL;
static int     pipe_fd = -1;

static unsigned int    tr_queries;
static unsigned int    tr_responses;
static counter_list_t *qtype_list;
static counter_list_t *opcode_list;
static counter_list_t *rcode_list;
#endif

static counter_list_t *counter_list_search (counter_list_t **list, unsigned int key)
{
	counter_list_t *entry;

	DBG ("counter_list_search (list = %p, key = %u)",
			(void *) *list, key);

	for (entry = *list; entry != NULL; entry = entry->next)
		if (entry->key == key)
			break;

	DBG ("return (%p)", (void *) entry);
	return (entry);
}

static counter_list_t *counter_list_create (counter_list_t **list,
		unsigned int key, unsigned int value)
{
	counter_list_t *entry;

	DBG ("counter_list_create (list = %p, key = %u, value = %u)",
			(void *) *list, key, value);

	entry = (counter_list_t *) malloc (sizeof (counter_list_t));
	if (entry == NULL)
		return (NULL);

	memset (entry, 0, sizeof (counter_list_t));
	entry->key = key;
	entry->value = value;

	if (*list == NULL)
	{
		*list = entry;
	}
	else
	{
		counter_list_t *last;

		last = *list;
		while (last->next != NULL)
			last = last->next;

		last->next = entry;
	}

	DBG ("return (%p)", (void *) entry);
	return (entry);
}

static void counter_list_add (counter_list_t **list,
		unsigned int key, unsigned int increment)
{
	counter_list_t *entry;

	DBG ("counter_list_add (list = %p, key = %u, increment = %u)",
			(void *) *list, key, increment);

	entry = counter_list_search (list, key);

	if (entry != NULL)
	{
		entry->value += increment;
	}
	else
	{
		counter_list_create (list, key, increment);
	}
	DBG ("return ()");
}

static int counter_list_send (counter_list_t *list, int fd)
{
	counter_list_t *cl;
	unsigned int values[2 * T_MAX];
	unsigned int values_num;

	if (fd < 0)
		return (-1);

	values_num = 0;

	for (cl = list;
			(cl != NULL) && (values_num < T_MAX);
			cl = cl->next)
	{
		values[2 * values_num] = cl->key;
		values[(2 * values_num) + 1] = cl->value;
		values_num++;
	}

	DBG ("swrite (fd = %i, values_num = %i)", fd, values_num);
	if (swrite (fd, (const void *) &values_num, sizeof (values_num)) != 0)
	{
		DBG ("Writing to fd failed: %s", strerror (errno));
		syslog (LOG_ERR, "dns plugin: Writing to fd failed: %s",
				strerror (errno));
		return (-1);
	}

	if (values_num == 0)
		return (0);

	DBG ("swrite (fd = %i, values = %p, size = %i)",
			fd, (void *) values, (int) (sizeof (int) * values_num));
	if (swrite (fd, (const void *) values, 2 * sizeof (int) * values_num) != 0)
	{
		DBG ("Writing to pipe failed: %s", strerror (errno));
		syslog (LOG_ERR, "dns plugin: Writing to pipe failed: %s",
				strerror (errno));
		return (-1);
	}

	return (values_num);
}
#if NAMED_HAVE_CONFIG
static int dns_config (char *key, char *value)
{
#if HAVE_LIBPCAP
	if (strcasecmp (key, "Interface") == 0)
	{
		if (pcap_device != NULL)
			free (pcap_device);
		if ((pcap_device = strdup (value)) == NULL)
			return (1);
	}
	else if (strcasecmp (key, "IgnoreSource") == 0)
	{
		if (value != NULL)
			ignore_list_add_name (value);
	}
	else
	{
		return (-1);
	}

	return (0);
#endif /* HAVE_LIBPCAP */
}
#endif /* NAMED_HAVE_CONFIG */

static void dns_child_callback (const rfc1035_header_t *dns)
{
	if (dns->qr == 0)
	{
		/* This is a query */
		tr_queries += dns->length;
		counter_list_add (&qtype_list,  dns->qtype,  1);
	}
	else
	{
		/* This is a reply */
		tr_responses += dns->length;
		counter_list_add (&rcode_list,  dns->rcode,  1);
	}

	/* FIXME: Are queries, replies or both interesting? */
	counter_list_add (&opcode_list, dns->opcode, 1);
}

static void dns_child_loop (void)
{
	pcap_t *pcap_obj;
	char    pcap_error[PCAP_ERRBUF_SIZE];
	struct  bpf_program fp;

	struct pollfd poll_fds[2];
	int status;

	/* Don't catch these signals */
	signal (SIGINT, SIG_DFL);
	signal (SIGTERM, SIG_DFL);

	/* Passing `pcap_device == NULL' is okay and the same as passign "any" */
	DBG ("Creating PCAP object..");
	pcap_obj = pcap_open_live (pcap_device,
			PCAP_SNAPLEN,
			0 /* Not promiscuous */,
			0 /* no read timeout */,
			pcap_error);
	if (pcap_obj == NULL)
	{
		syslog (LOG_ERR, "dns plugin: Opening interface `%s' failed: %s",
				(pcap_device != NULL) ? pcap_device : "any",
				pcap_error);
		close (pipe_fd);
		pipe_fd = -1;
		return;
	}

	memset (&fp, 0, sizeof (fp));
	if (pcap_compile (pcap_obj, &fp, "udp port 53", 1, 0) < 0)
	{
		DBG ("pcap_compile failed");
		syslog (LOG_ERR, "dns plugin: pcap_compile failed");
		close (pipe_fd);
		pipe_fd = -1;
		return;
	}
	if (pcap_setfilter (pcap_obj, &fp) < 0)
	{
		DBG ("pcap_setfilter failed");
		syslog (LOG_ERR, "dns plugin: pcap_setfilter failed");
		close (pipe_fd);
		pipe_fd = -1;
		return;
	}

	DBG ("PCAP object created.");

	dnstop_set_pcap_obj (pcap_obj);
	dnstop_set_callback (dns_child_callback);

	/* Set up pipe end */
	poll_fds[0].fd = pipe_fd;
	poll_fds[0].events = POLLOUT;

	/* Set up pcap device */
	poll_fds[1].fd = pcap_fileno (pcap_obj);
	poll_fds[1].events = POLLIN | POLLPRI;

	while (pipe_fd > 0)
	{
		DBG ("poll (...)");
		status = poll (poll_fds, 2, -1 /* wait forever for a change */);

		/* Signals are not caught, but this is very handy when
		 * attaching to the process with a debugger. -octo */
		if ((status < 0) && (errno == EINTR))
		{
			errno = 0;
			continue;
		}

		if (status < 0)
		{
			syslog (LOG_ERR, "dns plugin: poll(2) failed: %s",
					strerror (errno));
			break;
		}

		if (poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			DBG ("Pipe closed. Exiting.");
			syslog (LOG_NOTICE, "dns plugin: Pipe closed. Exiting.");
			break;
		}
		else if (poll_fds[0].revents & POLLOUT)
		{
			DBG ("Sending data..");

			DBG ("swrite (pipe_fd = %i, tr_queries = %i)", pipe_fd, tr_queries);
			if (swrite (pipe_fd, (const void *) &tr_queries, sizeof (tr_queries)) != 0)
			{
				DBG ("Writing to pipe_fd failed: %s", strerror (errno));
				syslog (LOG_ERR, "dns plugin: Writing to pipe_fd failed: %s",
						strerror (errno));
				return;
			}

			DBG ("swrite (pipe_fd = %i, tr_responses = %i)", pipe_fd, tr_responses);
			if (swrite (pipe_fd, (const void *) &tr_responses, sizeof (tr_responses)) != 0)
			{
				DBG ("Writing to pipe_fd failed: %s", strerror (errno));
				syslog (LOG_ERR, "dns plugin: Writing to pipe_fd failed: %s",
						strerror (errno));
				return;
			}

			counter_list_send (qtype_list, pipe_fd);
			counter_list_send (opcode_list, pipe_fd);
			counter_list_send (rcode_list, pipe_fd);
		}

		if (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			DBG ("pcap-device closed. Exiting.");
			syslog (LOG_ERR, "dns plugin: pcap-device closed. Exiting.");
			break;
		}
		else if (poll_fds[1].revents & (POLLIN | POLLPRI))
		{
			status = pcap_dispatch (pcap_obj,
					10 /* Only handle 10 packets at a time */,
					handle_pcap /* callback */,
					NULL /* Whatever this means.. */);
			if (status < 0)
			{
				DBG ("pcap_dispatch failed: %s", pcap_geterr (pcap_obj));
				syslog (LOG_ERR, "dns plugin: pcap_dispatch failed: %s",
						pcap_geterr (pcap_obj));
				break;
			}
		}
	} /* while (42) */

	DBG ("child is exiting");

	close (pipe_fd);
	pipe_fd = -1;
	pcap_close (pcap_obj);
} /* static void dns_child_loop (void) */

static void dns_init (void)
{
#if HAVE_LIBPCAP
	int pipe_fds[2];
	pid_t pid_child;

	tr_queries   = 0;
	tr_responses = 0;

	if (pipe (pipe_fds) != 0)
	{
		syslog (LOG_ERR, "dns plugin: pipe(2) failed: %s",
				strerror (errno));
		return;
	}

	/* Fork off child */
	pid_child = fork ();
	if (pid_child < 0)
	{
		syslog (LOG_ERR, "dns plugin: fork(2) failed: %s",
				strerror (errno));
		close (pipe_fds[0]);
		close (pipe_fds[1]);
		return;
	}
	else if (pid_child != 0)
	{
		/* parent: Close the writing end, keep the reading end. */
		pipe_fd = pipe_fds[0];
		close (pipe_fds[1]);
	}
	else
	{
		/* child: Close the reading end, keep the writing end. */
		pipe_fd = pipe_fds[1];
		close (pipe_fds[0]);

		dns_child_loop ();
		exit (0);
	}

	/* fcntl (pipe_fd, F_SETFL, O_NONBLOCK); */
#endif
}

static void traffic_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, traffic_file, val,
			traffic_ds_def, traffic_ds_num);
}

static void qtype_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, qtype_file, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, qtype_ds_def, qtype_ds_num);
}

static void rcode_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, rcode_file, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, rcode_ds_def, rcode_ds_num);
}

static void opcode_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, opcode_file, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, opcode_ds_def, opcode_ds_num);
}

static void traffic_submit (unsigned int queries, unsigned int replies)
{
	char buffer[64];
	int  status;

	status = snprintf (buffer, 64, "N:%u:%u", queries, replies);
	if ((status < 1) || (status >= 64))
		return;

	plugin_submit ("dns_traffic", "-", buffer);
}

static void qtype_submit (int qtype, unsigned int counter)
{
	char inst[32];
	char buffer[32];
	int  status;

	strncpy (inst, qtype_str (qtype), 32);
	inst[31] = '\0';

	status = snprintf (buffer, 32, "N:%u", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("dns_qtype", inst, buffer);
}

static void rcode_submit (int rcode, unsigned int counter)
{
	char inst[32];
	char buffer[32];
	int  status;

	strncpy (inst, rcode_str (rcode), 32);
	inst[31] = '\0';

	status = snprintf (buffer, 32, "N:%u", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("dns_rcode", inst, buffer);
}

static void opcode_submit (int opcode, unsigned int counter)
{
	char inst[32];
	char buffer[32];
	int  status;

	strncpy (inst, opcode_str (opcode), 32);
	inst[31] = '\0';

	status = snprintf (buffer, 32, "N:%u", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("dns_opcode", inst, buffer);
}

#if NAMED_HAVE_READ
static unsigned int dns_read_array (unsigned int *values)
{
	unsigned int values_num;

	if (pipe_fd < 0)
		return (0);

	if (sread (pipe_fd, (void *) &values_num, sizeof (values_num)) != 0)
	{
		DBG ("Reading from the pipe failed: %s",
				strerror (errno));
		syslog (LOG_ERR, "dns plugin: Reading from the pipe failed: %s",
				strerror (errno));
		pipe_fd = -1;
		return (0);
	}
	DBG ("sread (pipe_fd = %i, values_num = %u)", pipe_fd, values_num);

	assert (values_num <= T_MAX);

	if (values_num == 0)
		return (0);

	if (sread (pipe_fd, (void *) values, 2 * sizeof (unsigned int) * values_num) != 0)
	{
		DBG ("Reading from the pipe failed: %s",
				strerror (errno));
		syslog (LOG_ERR, "dns plugin: Reading from the pipe failed: %s",
				strerror (errno));
		pipe_fd = -1;
		return (0);
	}

	return (values_num);
}

static void dns_read (void)
{
	unsigned int values[2 * T_MAX];
	unsigned int values_num;
	int i;

	if (pipe_fd < 0)
		return;

	if (sread (pipe_fd, (void *) &tr_queries, sizeof (tr_queries)) != 0)
	{
		DBG ("Reading from the pipe failed: %s",
				strerror (errno));
		syslog (LOG_ERR, "dns plugin: Reading from the pipe failed: %s",
				strerror (errno));
		pipe_fd = -1;
		return;
	}
	DBG ("sread (pipe_fd = %i, tr_queries = %u)", pipe_fd, tr_queries);

	if (sread (pipe_fd, (void *) &tr_responses, sizeof (tr_responses)) != 0)
	{
		DBG ("Reading from the pipe failed: %s",
				strerror (errno));
		syslog (LOG_ERR, "dns plugin: Reading from the pipe failed: %s",
				strerror (errno));
		pipe_fd = -1;
		return;
	}
	DBG ("sread (pipe_fd = %i, tr_responses = %u)", pipe_fd, tr_responses);

	traffic_submit (tr_queries, tr_responses);

	values_num = dns_read_array (values);
	for (i = 0; i < values_num; i++)
	{
		DBG ("qtype = %u; counter = %u;", values[2 * i], values[(2 * i) + 1]);
		qtype_submit (values[2 * i], values[(2 * i) + 1]);
	}

	values_num = dns_read_array (values);
	for (i = 0; i < values_num; i++)
	{
		DBG ("opcode = %u; counter = %u;", values[2 * i], values[(2 * i) + 1]);
		opcode_submit (values[2 * i], values[(2 * i) + 1]);
	}

	values_num = dns_read_array (values);
	for (i = 0; i < values_num; i++)
	{
		DBG ("rcode = %u; counter = %u;", values[2 * i], values[(2 * i) + 1]);
		rcode_submit (values[2 * i], values[(2 * i) + 1]);
	}
}
#else /* if !NAMED_HAVE_READ */
# define dns_read NULL
#endif

void module_register (void)
{
	plugin_register (MODULE_NAME, dns_init, dns_read, NULL);
	plugin_register ("dns_traffic", NULL, NULL, traffic_write);
	plugin_register ("dns_qtype", NULL, NULL, qtype_write);
	plugin_register ("dns_rcode", NULL, NULL, rcode_write);
	plugin_register ("dns_opcode", NULL, NULL, opcode_write);
	cf_register (MODULE_NAME, dns_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
