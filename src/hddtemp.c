/**
 * collectd - src/hddtemp.c
 * Copyright (C) 2005-2006  Vincent Stehlé
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
 *   Vincent Stehlé <vincent.stehle at free.fr>
 *   Florian octo Forster <octo at verplant.org>
 *
 * TODO:
 *   Do a pass, some day, and spare some memory. We consume too much for now
 *   in string buffers and the like.
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_debug.h"

#define MODULE_NAME "hddtemp"

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <libgen.h> /* for basename */

#ifdef KERNEL_LINUX
# include <linux/major.h>
#endif

#define HDDTEMP_DEF_HOST "127.0.0.1"
#define HDDTEMP_DEF_PORT "7634"

/* BUFFER_SIZE
   Size of the buffer we use to receive from the hddtemp daemon. */
#define BUFFER_SIZE 1024

static char *filename_format = "hddtemp-%s.rrd";

static char *ds_def[] =
{
	"DS:value:GAUGE:"COLLECTD_HEARTBEAT":U:U",
	NULL
};
static int ds_num = 1;

static char *config_keys[] =
{
	"Host",
	"Port",
	NULL
};
static int config_keys_num = 2;

typedef struct hddname
{
	int major;
	int minor;
	char *name;
	struct hddname *next;
} hddname_t;

static hddname_t *first_hddname = NULL;
static char *hddtemp_host = NULL;
static char *hddtemp_port = NULL;

/*
 * NAME
 *  hddtemp_query_daemon
 *
 * DESCRIPTION
 * Connect to the hddtemp daemon and receive data.
 *
 * ARGUMENTS:
 *  `buffer'            The buffer where we put the received ascii string.
 *  `buffer_size'       Size of the buffer
 *
 * RETURN VALUE:
 *   >= 0 if ok, < 0 otherwise.
 *
 * NOTES:
 *  Example of possible strings, as received from daemon:
 *    |/dev/hda|ST340014A|36|C|
 *    |/dev/hda|ST380011A|46|C||/dev/hdd|ST340016A|SLP|*|
 *
 * FIXME:
 *  we need to create a new socket each time. Is there another way?
 *  Hm, maybe we can re-use the `sockaddr' structure? -octo
 */
static int hddtemp_query_daemon (char *buffer, int buffer_size)
{
	int fd;
	ssize_t status;
	int buffer_fill;

	const char *host;
	const char *port;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = AI_ADDRCONFIG;
	ai_hints.ai_family   = PF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;

	host = hddtemp_host;
	if (host == NULL)
		host = HDDTEMP_DEF_HOST;

	port = hddtemp_port;
	if (port == NULL)
		port = HDDTEMP_DEF_PORT;

    DBG ("Connect to %s:%s", host, port);

	if ((ai_return = getaddrinfo (host, port, &ai_hints, &ai_list)) != 0)
	{
		syslog (LOG_ERR, "hddtemp: getaddrinfo (%s, %s): %s",
				host, port,
				ai_return == EAI_SYSTEM ? strerror (errno) : gai_strerror (ai_return));
		return (-1);
	}

	fd = -1;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		/* create our socket descriptor */
		if ((fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol)) < 0)
		{
			syslog (LOG_ERR, "hddtemp: socket: %s",
					strerror (errno));
			continue;
		}

		/* connect to the hddtemp daemon */
		if (connect (fd, (struct sockaddr *) ai_ptr->ai_addr, ai_ptr->ai_addrlen))
		{
			syslog (LOG_ERR, "hddtemp: connect (%s, %s): %s",
					host, port, strerror (errno));
			close (fd);
			fd = -1;
			continue;
		}
	}

	freeaddrinfo (ai_list);

	if (fd < 0){
		syslog (LOG_ERR, "hddtemp: Could not connect to daemon?");
		return (-1);
    }

	/* receive data from the hddtemp daemon */
	memset (buffer, '\0', buffer_size);

	buffer_fill = 0;
	while ((status = read (fd, buffer + buffer_fill, buffer_size - buffer_fill)) != 0)
	{
		if (status == -1)
		{
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;

			syslog (LOG_ERR, "hddtemp: Error reading from socket: %s",
						strerror (errno));
			return (-1);
		}
		buffer_fill += status;

		if (buffer_fill >= buffer_size)
			break;
	}

	if (buffer_fill >= buffer_size)
	{
		buffer[buffer_size - 1] = '\0';
		syslog (LOG_WARNING, "hddtemp: Message from hddtemp has been truncated.");
	}
	else if (buffer_fill == 0)
	{
		syslog (LOG_WARNING, "hddtemp: Peer has unexpectedly shut down the socket. "
				"Buffer: `%s'", buffer);
		close (fd);
		return (-1);
	}

	close (fd);
	return (0);
}

static int hddtemp_config (char *key, char *value)
{
	if (strcasecmp (key, "host") == 0)
	{
		if (hddtemp_host != NULL)
			free (hddtemp_host);
		hddtemp_host = strdup (value);
	}
	else if (strcasecmp (key, "port") == 0)
	{
		if (hddtemp_port != NULL)
			free (hddtemp_port);
		hddtemp_port = strdup (value);
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void hddtemp_init (void)
{
	FILE *fh;
	char buf[BUFFER_SIZE];
	int buflen;

	char *fields[16];
	int num_fields;

	int major;
	int minor;
	char *name;
	hddname_t *next;
	hddname_t *entry;

	next = first_hddname;
	while (next != NULL)
	{
		entry = next;
		next = entry->next;

		free (entry->name);
		free (entry);
	}
	first_hddname = NULL;

	if ((fh = fopen ("/proc/partitions", "r")) != NULL)
	{
        DBG ("Looking at /proc/partitions...");

		while (fgets (buf, BUFFER_SIZE, fh) != NULL)
		{
			/* Delete trailing newlines */
			buflen = strlen (buf);

			while ((buflen > 0) && ((buf[buflen-1] == '\n') || (buf[buflen-1] == '\r')))
				buf[--buflen] = '\0';

            /* We want lines of the form:
             *
             *     3     1   77842926 hda1
             *
             * ...so, skip everything else. */
			if (buflen == 0)
				continue;
			
			num_fields = strsplit (buf, fields, 16);

			if (num_fields != 4)
				continue;

			major = atoi (fields[0]);
			minor = atoi (fields[1]);

            /* We try to keep only entries, which may correspond to
             * physical disks and that may have a corresponding entry
             * in the hddtemp daemon. Basically, this means IDE and SCSI. */
            switch(major){
#           ifdef KERNEL_LINUX

            /* SCSI. */
            case SCSI_DISK0_MAJOR:
            case SCSI_DISK1_MAJOR:
            case SCSI_DISK2_MAJOR:
            case SCSI_DISK3_MAJOR:
            case SCSI_DISK4_MAJOR:
            case SCSI_DISK5_MAJOR:
            case SCSI_DISK6_MAJOR:
            case SCSI_DISK7_MAJOR:
            case SCSI_DISK8_MAJOR:
            case SCSI_DISK9_MAJOR:
            case SCSI_DISK10_MAJOR:
            case SCSI_DISK11_MAJOR:
            case SCSI_DISK12_MAJOR:
            case SCSI_DISK13_MAJOR:
            case SCSI_DISK14_MAJOR:
            case SCSI_DISK15_MAJOR:
                /* SCSI disks minors are multiples of 16.
                 * Keep only those. */
                if(minor % 16)
                    continue;

                break;

            /* IDE. */
            case IDE0_MAJOR:
            case IDE1_MAJOR:
            case IDE2_MAJOR:
            case IDE3_MAJOR:
            case IDE4_MAJOR:
            case IDE5_MAJOR:
            case IDE6_MAJOR:
            case IDE7_MAJOR:
            case IDE8_MAJOR:
            case IDE9_MAJOR:
                /* IDE disks minors can only be 0 or 64.
                 * Keep only those. */
			    if(minor != 0 && minor != 64)
                    continue;

                break;

            /* Skip all other majors. */
            default:
                continue;

#           else   /* not KERNEL_LINUX */

            /* VS: Do we need this on other systems?
                   We tried to open /proc/partitions at first anyway,
                   so maybe we know we are under Linux always? */
			case 0:
			    /* I know that this makes `minor' redundant, but I want
			     * to be able to change this beavior in the future..
			     * And 4 or 8 bytes won't hurt anybody.. -octo */
			    continue;

            /* Unknown major. Keep for now.
             * VS: refine as more cases are precisely known. */
            default:
                break;

#           endif   /* KERNEL_LINUX */
            }

			if ((name = strdup (fields[3])) == NULL){
		        syslog (LOG_ERR, "hddtemp: NULL strdup(%s)!", fields[3]);
				continue;
            }

			if ((entry = (hddname_t *) malloc (sizeof (hddname_t))) == NULL)
			{
		        syslog (LOG_ERR, "hddtemp: NULL malloc(%u)!", sizeof (hddname_t));
				free (name);
				continue;
			}

            DBG ("Found disk: %s (%u:%u).", name, major, minor);

			entry->major = major;
			entry->minor = minor;
			entry->name  = name;
			entry->next  = NULL;

			if (first_hddname == NULL)
			{
				first_hddname = entry;
			}
			else
			{
				entry->next = first_hddname;
				first_hddname = entry;
			}
		}
	} else
        DBG ("Could not open /proc/partitions.");
}

static void hddtemp_write (char *host, char *inst, char *val)
{
	char filename[BUFFER_SIZE];
	int status;

	/* construct filename */
	status = snprintf (filename, BUFFER_SIZE, filename_format, inst);
	if (status < 1)
		return;
	else if (status >= BUFFER_SIZE)
		return;

	rrd_update_file (host, filename, val, ds_def, ds_num);
}

/*
 * hddtemp_get_name
 *
 * Description:
 *   Try to "cook" a bit the drive name as returned
 *   by the hddtemp daemon. The intend is to transform disk
 *   names into <major>-<minor> when possible.
 */
static char *hddtemp_get_name (char *drive)
{
	hddname_t *list;
	char *ret;

	for (list = first_hddname; list != NULL; list = list->next)
		if (strcmp (drive, list->name) == 0)
			break;

	if (list == NULL){
        DBG ("Don't know %s, keeping name as-is.", drive);
		return (strdup (drive));
    }

	if ((ret = (char *) malloc (128 * sizeof (char))) == NULL)
		return (NULL);

	if (snprintf (ret, 128, "%i-%i", list->major, list->minor) >= 128)
	{
		free (ret);
		return (NULL);
	}

	return (ret);
}

static void hddtemp_submit (char *inst, double temperature)
{
	char buf[BUFFER_SIZE];

	if (snprintf (buf, BUFFER_SIZE, "%u:%.3f", (unsigned int) curtime, temperature)
            >= BUFFER_SIZE)
		return;

	plugin_submit (MODULE_NAME, inst, buf);
}

static void hddtemp_read (void)
{
	char buf[BUFFER_SIZE];
	char *fields[128];
	char *ptr;
	int num_fields;
	int num_disks;
	int i;

	static int wait_time = 1;
	static int wait_left = 0;

	if (wait_left >= 10)
	{
		wait_left -= 10;
		return;
	}

	/* get data from daemon */
	if (hddtemp_query_daemon (buf, BUFFER_SIZE) < 0)
	{
		/* This limit is reached in log2(86400) =~ 17 steps. Since
		 * there is a 2^n seconds wait between each step it will need
		 * roughly one day to reach this limit. -octo */
		
		wait_time *= 2;
		if (wait_time > 86400)
			wait_time = 86400;

		wait_left = wait_time;

		return;
	}
	else
	{
		wait_time = 1;
		wait_left = 0;
	}

    DBG ("Received: %s", buf);

	/* NB: strtok will eat up "||" and leading "|"'s */
	num_fields = 0;
	ptr = buf;
	while ((fields[num_fields] = strtok (ptr, "|")) != NULL)
	{
		ptr = NULL;
		num_fields++;

		if (num_fields >= 128)
			break;
	}

	num_disks = num_fields / 4;

	for (i = 0; i < num_disks; i++)
	{
		char *name, *submit_name;
		double temperature;
		char *mode;

		mode = fields[4*i + 3];
		name = basename (fields[4*i + 0]);

		/* Skip non-temperature information */
		if (mode[0] != 'C' && mode[0] != 'F'){
            DBG ("No temp. for %s", name);
			continue;
        }

		temperature = atof (fields[4*i + 2]);

		/* Convert farenheit to celsius */
		if (mode[0] == 'F')
			temperature = (temperature - 32.0) * 5.0 / 9.0;

		if ((submit_name = hddtemp_get_name (name)) != NULL)
		{
			hddtemp_submit (submit_name, temperature);
			free (submit_name);
		}
		else
		{
			hddtemp_submit (name, temperature);
		}
	}
}

/* module_register
   Register collectd plugin. */
void module_register (void)
{
	plugin_register (MODULE_NAME, hddtemp_init, hddtemp_read, hddtemp_write);
	cf_register (MODULE_NAME, hddtemp_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
