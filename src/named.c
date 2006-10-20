/**
 * collectd - src/named.c
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

#define MODULE_NAME "named"

#if HAVE_LIBPCAP
# define NAMED_HAVE_CONFIG 1
#else
# define NAMED_HAVE_CONFIG 0
#endif

#if HAVE_LIBPCAP
# include "dnstop.h"
# define NAMED_HAVE_READ 1
#else
# define NAMED_HAVE_READ 0
#endif

static char *qtype_file = "named/qtype-%s.rrd";

static char *qtype_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int qtype_ds_num = 1;

#if NAMED_HAVE_CONFIG
#if HAVE_LIBPCAP
static char *config_keys[] =
{
	"Interface",
	NULL
};
static int config_keys_num = 1;
#endif /* HAVE_LIBPCAP */
#endif /* NAMED_HAVE_CONFIG */

#if HAVE_LIBPCAP
#define PCAP_SNAPLEN 1460
static char   *pcap_device = NULL;
static int     pipe_fd = -1;
#endif

#if NAMED_HAVE_CONFIG
static int named_config (char *key, char *value)
{
#if HAVE_LIBPCAP
	if (strcasecmp (key, "Interface") == 0)
	{
		if (pcap_device != NULL)
			free (pcap_device);
		if ((pcap_device = strdup (value)) == NULL)
			return (1);
	}
	else
	{
		return (-1);
	}

	return (0);
#endif /* HAVE_LIBPCAP */
}
#endif /* NAMED_HAVE_CONFIG */

static int named_child_send_data (void)
{
	int values[2 * T_MAX];
	int values_num;
	int i;

	if (pipe_fd < 0)
		return (-1);

	values_num = 0;
	for (i = 0; i < T_MAX; i++)
	{
		if (qtype_counts[i] != 0)
		{
			values[2 * values_num] = i;
			values[(2 * values_num) + 1] = qtype_counts[i];
			values_num++;
		}
	}

	DBG ("swrite (pipe_fd = %i, values_num = %i)", pipe_fd, values_num);
	if (swrite (pipe_fd, (const void *) &values_num, sizeof (values_num)) != 0)
	{
		DBG ("Writing to pipe failed: %s", strerror (errno));
		syslog (LOG_ERR, "named plugin: Writing to pipe failed: %s",
				strerror (errno));
		return (-1);
	}

	if (values_num == 0)
		return (0);

	DBG ("swrite (pipe_fd = %i, values = %p, size = %i)", pipe_fd, (void *) values, (int) (sizeof (int) * values_num));
	if (swrite (pipe_fd, (const void *) values, 2 * sizeof (int) * values_num) != 0)
	{
		DBG ("Writing to pipe failed: %s", strerror (errno));
		syslog (LOG_ERR, "named plugin: Writing to pipe failed: %s",
				strerror (errno));
		return (-1);
	}

	return (values_num);
}

static void named_child_loop (void)
{
	pcap_t *pcap_obj;
	char    pcap_error[PCAP_ERRBUF_SIZE];
	struct  bpf_program fp;

	struct pollfd poll_fds[2];
	int status;

	/* Passing `pcap_device == NULL' is okay and the same as passign "any" */
	DBG ("Creating PCAP object..");
	pcap_obj = pcap_open_live (pcap_device,
			PCAP_SNAPLEN,
			0 /* Not promiscuous */,
			0 /* no read timeout */,
			pcap_error);
	if (pcap_obj == NULL)
	{
		syslog (LOG_ERR, "named plugin: Opening interface `%s' failed: %s",
				(pcap_device != NULL) ? pcap_device : "any",
				pcap_error);
		close (pipe_fd);
		pipe_fd = -1;
		return;
	}
	pcap = pcap_obj; /* FIXME: This is used by `handle_pcap' */

	memset (&fp, 0, sizeof (fp));
	if (pcap_compile (pcap_obj, &fp, "udp dst port 53", 1, 0) < 0)
	{
		DBG ("pcap_compile failed");
		syslog (LOG_ERR, "named plugin: pcap_compile failed");
		close (pipe_fd);
		pipe_fd = -1;
		return;
	}
	if (pcap_setfilter (pcap_obj, &fp) < 0)
	{
		DBG ("pcap_setfilter failed");
		syslog (LOG_ERR, "named plugin: pcap_setfilter failed");
		close (pipe_fd);
		pipe_fd = -1;
		return;
	}

	DBG ("PCAP object created.");

	/* Set up pipe end */
	poll_fds[0].fd = pipe_fd;
	poll_fds[0].events = POLLOUT;

	/* Set up pcap device */
	poll_fds[1].fd = pcap_fileno (pcap_obj);
	poll_fds[1].events = POLLIN | POLLPRI;

	while (42)
	{
		DBG ("poll (...)");
		status = poll (poll_fds, 2, -1 /* wait forever for a change */);

		if (status < 0)
		{
			syslog (LOG_ERR, "named plugin: poll(2) failed: %s",
					strerror (errno));
			break;
		}

		if (poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			DBG ("Pipe closed. Exiting.");
			syslog (LOG_NOTICE, "named plugin: Pipe closed. Exiting.");
			break;
		}
		else if (poll_fds[0].revents & POLLOUT)
		{
			DBG ("Calling `named_child_send_data'");
			if (named_child_send_data () < 0)
			{
				break;
			}
		}

		if (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
		{
			DBG ("pcap-device closed. Exiting.");
			syslog (LOG_ERR, "named plugin: pcap-device closed. Exiting.");
			break;
		}
		else if (poll_fds[1].revents & (POLLIN | POLLPRI))
		{
			/* TODO: Read and analyse packet */
			status = pcap_dispatch (pcap_obj,
					1 /* Only handle 10 packets at a time */,
					handle_pcap /* callback */,
					NULL /* Whatever this means.. */);
			if (status < 0)
			{
				DBG ("pcap_dispatch failed: %s", pcap_geterr (pcap_obj));
				syslog (LOG_ERR, "named plugin: pcap_dispatch failed: %s",
						pcap_geterr (pcap_obj));
				break;
			}
		}
	} /* while (42) */

	DBG ("child is exiting");

	close (pipe_fd);
	pipe_fd = -1;
	pcap_close (pcap_obj);
} /* static void named_child_loop (void) */

static void named_init (void)
{
#if HAVE_LIBPCAP
	int pipe_fds[2];
	pid_t pid_child;

	if (pipe (pipe_fds) != 0)
	{
		syslog (LOG_ERR, "named plugin: pipe(2) failed: %s",
				strerror (errno));
		return;
	}

	/* Fork off child */
	pid_child = fork ();
	if (pid_child < 0)
	{
		syslog (LOG_ERR, "named plugin: fork(2) failed: %s",
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

		named_child_loop ();
		exit (0);
	}

	/* fcntl (pipe_fd, F_SETFL, O_NONBLOCK); */
#endif
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

static void qtype_submit (int qtype_int, unsigned int counter)
{
	char *qtype_char;
	char  buffer[32];
	int   status;

	qtype_char = qtype_str (qtype_int);

	status = snprintf (buffer, 32, "N:%i", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("named_qtype", qtype_char, buffer);
}

#if NAMED_HAVE_READ
static void named_read (void)
{
	int values[2 * T_MAX];
	int values_num;
	int qtype;
	int counter;
	int i;

	if (pipe_fd < 0)
		return;

	DBG ("Reading from pipe_fd = %i..", pipe_fd);
	if (sread (pipe_fd, (void *) &values_num, sizeof (values_num)) != 0)
	{
		syslog (LOG_ERR, "named plugin: Reading from the pipe failed: %s",
				strerror (errno));
		pipe_fd = -1;
		return;
	}

	assert ((values_num >= 0) && (values_num <= T_MAX));

	if (values_num == 0)
	{
		DBG ("No values available; returning");
		return;
	}

	DBG ("Reading %i qtype/values from pipe_fd = %i..", values_num, pipe_fd);
	if (sread (pipe_fd, (void *) values, 2 * sizeof (int) * values_num) != 0)
	{
		syslog (LOG_ERR, "named plugin: Reading from the pipe failed: %s",
				strerror (errno));
		pipe_fd = -1;
		return;
	}

	for (i = 0; i < values_num; i++)
	{
		qtype = values[2 * i];
		counter = values[(2 * i) + 1];

		DBG ("qtype = %i; counter = %i;", qtype, counter);
		qtype_submit (qtype, counter);
	}
}
#else /* if !NAMED_HAVE_READ */
# define named_read NULL
#endif

void module_register (void)
{
	plugin_register (MODULE_NAME, named_init, named_read, NULL);
	plugin_register ("named_qtype", NULL, NULL, qtype_write);
	/* TODO */
	cf_register (MODULE_NAME, named_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
