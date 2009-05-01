/*
 * collectd - src/apcups.c
 * Copyright (C) 2006-2007  Florian octo Forster
 * Copyright (C) 2006       Anthony Gialluca <tonyabg at charter.net>
 * Copyright (C) 2000-2004  Kern Sibbald
 * Copyright (C) 1996-1999  Andre M. Hedrick <andre at suse.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 * Authors:
 *   Anthony Gialluca <tonyabg at charter.net>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"      /* rrd_update_file */
#include "plugin.h"      /* plugin_register, plugin_submit */
#include "configfile.h"  /* cf_register */

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NETDB_H
# include <netdb.h>
#endif

#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif

#define NISPORT 3551
#define MAXSTRING               256
#define MODULE_NAME "apcups"

#define APCUPS_DEFAULT_HOST "localhost"

/*
 * Private data types
 */
struct apc_detail_s
{
	double linev;
	double loadpct;
	double bcharge;
	double timeleft;
	double outputv;
	double itemp;
	double battv;
	double linefreq;
};

/*
 * Private variables
 */
/* Default values for contacting daemon */
static char *conf_host = NULL;
static int   conf_port = NISPORT;

static int global_sockfd = -1;

static const char *config_keys[] =
{
	"Host",
	"Port",
	NULL
};
static int config_keys_num = 2;

/* Close the network connection */
static int apcups_shutdown (void)
{
	uint16_t packet_size = 0;

	if (global_sockfd < 0)
		return (0);

	DEBUG ("Gracefully shutting down socket %i.", global_sockfd);

	/* send EOF sentinel */
	swrite (global_sockfd, (void *) &packet_size, sizeof (packet_size));

	close (global_sockfd);
	global_sockfd = -1;

	return (0);
} /* int apcups_shutdown */

/*     
 * Open a TCP connection to the UPS network server
 * Returns -1 on error
 * Returns socket file descriptor otherwise
 */
static int net_open (char *host, int port)
{
	int              sd;
	int              status;
	char             port_str[8];
	struct addrinfo  ai_hints;
	struct addrinfo *ai_return;
	struct addrinfo *ai_list;

	assert ((port > 0x00000000) && (port <= 0x0000FFFF));

	/* Convert the port to a string */
	ssnprintf (port_str, sizeof (port_str), "%i", port);

	/* Resolve name */
	memset ((void *) &ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_family   = AF_INET; /* XXX: Change this to `AF_UNSPEC' if apcupsd can handle IPv6 */
	ai_hints.ai_socktype = SOCK_STREAM;

	status = getaddrinfo (host, port_str, &ai_hints, &ai_return);
	if (status != 0)
	{
		char errbuf[1024];
		INFO ("getaddrinfo failed: %s",
				(status == EAI_SYSTEM)
				? sstrerror (errno, errbuf, sizeof (errbuf))
				: gai_strerror (status));
		return (-1);
	}

	/* Create socket */
	sd = -1;
	for (ai_list = ai_return; ai_list != NULL; ai_list = ai_list->ai_next)
	{
		sd = socket (ai_list->ai_family, ai_list->ai_socktype, ai_list->ai_protocol);
		if (sd >= 0)
			break;
	}
	/* `ai_list' still holds the current description of the socket.. */

	if (sd < 0)
	{
		DEBUG ("Unable to open a socket");
		freeaddrinfo (ai_return);
		return (-1);
	}

	status = connect (sd, ai_list->ai_addr, ai_list->ai_addrlen);

	freeaddrinfo (ai_return);

	if (status != 0) /* `connect(2)' failed */
	{
		char errbuf[1024];
		INFO ("connect failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (sd);
		return (-1);
	}

	DEBUG ("Done opening a socket %i", sd);

	return (sd);
} /* int net_open (char *host, char *service, int port) */

/* 
 * Receive a message from the other end. Each message consists of
 * two packets. The first is a header that contains the size
 * of the data that follows in the second packet.
 * Returns number of bytes read
 * Returns 0 on end of file
 * Returns -1 on hard end of file (i.e. network connection close)
 * Returns -2 on error
 */
static int net_recv (int *sockfd, char *buf, int buflen)
{
	uint16_t packet_size;

	/* get data size -- in short */
	if (sread (*sockfd, (void *) &packet_size, sizeof (packet_size)) != 0)
	{
		*sockfd = -1;
		return (-1);
	}

	packet_size = ntohs (packet_size);
	if (packet_size > buflen)
	{
		DEBUG ("record length too large");
		return (-2);
	}

	if (packet_size == 0)
		return (0);

	/* now read the actual data */
	if (sread (*sockfd, (void *) buf, packet_size) != 0)
	{
		*sockfd = -1;
		return (-1);
	}

	return ((int) packet_size);
} /* static int net_recv (int *sockfd, char *buf, int buflen) */

/*
 * Send a message over the network. The send consists of
 * two network packets. The first is sends a short containing
 * the length of the data packet which follows.
 * Returns zero on success
 * Returns non-zero on error
 */
static int net_send (int *sockfd, char *buff, int len)
{
	uint16_t packet_size;

	assert (len > 0);
	assert (*sockfd >= 0);

	/* send short containing size of data packet */
	packet_size = htons ((uint16_t) len);

	if (swrite (*sockfd, (void *) &packet_size, sizeof (packet_size)) != 0)
	{
		*sockfd = -1;
		return (-1);
	}

	/* send data packet */
	if (swrite (*sockfd, (void *) buff, len) != 0)
	{
		*sockfd = -1;
		return (-2);
	}

	return (0);
}

/* Get and print status from apcupsd NIS server */
static int apc_query_server (char *host, int port,
		struct apc_detail_s *apcups_detail)
{
	int     n;
	char    recvline[1024];
	char   *tokptr;
	char   *toksaveptr;
	char   *key;
	double  value;

#if APCMAIN
# define PRINT_VALUE(name, val) printf("  Found property: name = %s; value = %f;\n", name, val)
#else
# define PRINT_VALUE(name, val) /**/
#endif

	if (global_sockfd < 0)
	{
		global_sockfd = net_open (host, port);
		if (global_sockfd < 0)
		{
			ERROR ("apcups plugin: Connecting to the "
					"apcupsd failed.");
			return (-1);
		}
	}

	if (net_send (&global_sockfd, "status", 6) < 0)
	{
		ERROR ("apcups plugin: Writing to the socket failed.");
		return (-1);
	}

	while ((n = net_recv (&global_sockfd, recvline, sizeof (recvline) - 1)) > 0)
	{
		assert ((unsigned int)n < sizeof (recvline));
		recvline[n] = '\0';
#if APCMAIN
		printf ("net_recv = `%s';\n", recvline);
#endif /* if APCMAIN */

		toksaveptr = NULL;
		tokptr = strtok_r (recvline, " :\t", &toksaveptr);
		while (tokptr != NULL)
		{
			key = tokptr;
			if ((tokptr = strtok_r (NULL, " :\t", &toksaveptr)) == NULL)
				continue;
			value = atof (tokptr);

			PRINT_VALUE (key, value);

			if (strcmp ("LINEV", key) == 0)
				apcups_detail->linev = value;
			else if (strcmp ("BATTV", key) == 0) 
				apcups_detail->battv = value;
			else if (strcmp ("ITEMP", key) == 0)
				apcups_detail->itemp = value;
			else if (strcmp ("LOADPCT", key) == 0)
				apcups_detail->loadpct = value;
			else if (strcmp ("BCHARGE", key) == 0)
				apcups_detail->bcharge = value;
			else if (strcmp ("OUTPUTV", key) == 0)
				apcups_detail->outputv = value;
			else if (strcmp ("LINEFREQ", key) == 0)
				apcups_detail->linefreq = value;
			else if (strcmp ("TIMELEFT", key) == 0)
				apcups_detail->timeleft = value;

			tokptr = strtok_r (NULL, ":", &toksaveptr);
		} /* while (tokptr != NULL) */
	}
	
	if (n < 0)
	{
		WARNING ("apcups plugin: Error reading from socket");
		return (-1);
	}

	return (0);
}

static int apcups_config (const char *key, const char *value)
{
	if (strcasecmp (key, "host") == 0)
	{
		if (conf_host != NULL)
		{
			free (conf_host);
			conf_host = NULL;
		}
		if ((conf_host = strdup (value)) == NULL)
			return (1);
	}
	else if (strcasecmp (key, "Port") == 0)
	{
		int port_tmp = atoi (value);
		if (port_tmp < 1 || port_tmp > 65535)
		{
			WARNING ("apcups plugin: Invalid port: %i", port_tmp);
			return (1);
		}
		conf_port = port_tmp;
	}
	else
	{
		return (-1);
	}
	return (0);
}

static void apc_submit_generic (char *type, char *type_inst, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "apcups", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static void apc_submit (struct apc_detail_s *apcups_detail)
{
	apc_submit_generic ("voltage",    "input",   apcups_detail->linev);
	apc_submit_generic ("voltage",    "output",  apcups_detail->outputv);
	apc_submit_generic ("voltage",    "battery", apcups_detail->battv);
	apc_submit_generic ("charge",     "",        apcups_detail->bcharge);
	apc_submit_generic ("percent",    "load",    apcups_detail->loadpct);
	apc_submit_generic ("timeleft",   "",        apcups_detail->timeleft);
	apc_submit_generic ("temperature", "",       apcups_detail->itemp);
	apc_submit_generic ("frequency",  "input",   apcups_detail->linefreq);
}

static int apcups_read (void)
{
	struct apc_detail_s apcups_detail;
	int status;

	apcups_detail.linev    =   -1.0;
	apcups_detail.outputv  =   -1.0;
	apcups_detail.battv    =   -1.0;
	apcups_detail.loadpct  =   -1.0;
	apcups_detail.bcharge  =   -1.0;
	apcups_detail.timeleft =   -1.0;
	apcups_detail.itemp    = -300.0;
	apcups_detail.linefreq =   -1.0;
  
	status = apc_query_server (conf_host == NULL
			? APCUPS_DEFAULT_HOST
			: conf_host,
			conf_port, &apcups_detail);
 
	/*
	 * if we did not connect then do not bother submitting
	 * zeros. We want rrd files to have NAN.
	 */
	if (status != 0)
	{
		DEBUG ("apc_query_server (%s, %i) = %i",
				conf_host == NULL
				? APCUPS_DEFAULT_HOST
				: conf_host,
				conf_port, status);
		return (-1);
	}

	apc_submit (&apcups_detail);

	return (0);
} /* apcups_read */

void module_register (void)
{
	plugin_register_config ("apcups", apcups_config, config_keys,
			config_keys_num);
	plugin_register_read ("apcups", apcups_read);
	plugin_register_shutdown ("apcups", apcups_shutdown);
} /* void module_register */
