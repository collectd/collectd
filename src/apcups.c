/*
 * collectd - src/apcups.c
 * Copyright (C) 2006 Anthony Gialluca <tonyabg at charter.net>
 * Copyright (C) 2000-2004 Kern Sibbald
 * Copyright (C) 1996-99 Andre M. Hedrick <andre at suse.com>
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
 **/

/*
 * FIXME: Don't know why but without this here atof() was not returning
 * correct values for me. This is behavior that I don't understand and
 * should be examined in closer detail.
 */
#include <stdlib.h>

#include "collectd.h"
#include "common.h"      /* rrd_update_file */
#include "plugin.h"      /* plugin_register, plugin_submit */
#include "configfile.h"  /* cf_register */
#include "utils_debug.h"

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

#ifndef APCMAIN
# define APCMAIN 0
#endif

#define NISPORT 3551
#define MAXSTRING               256
#define MODULE_NAME "apcups"

/* Default values for contacting daemon */
static char *global_host = NULL;
static int   global_port = NISPORT;

/* 
 * The following are only if not compiled to test the module with its own main.
*/
#if !APCMAIN
static char *bvolt_file_template = "apcups/voltage-%s.rrd";
static char *bvolt_ds_def[] = 
{
	"DS:voltage:GAUGE:"COLLECTD_HEARTBEAT":0:U",
};
static int bvolt_ds_num = 1;

static char *load_file_template = "apcups/charge_percent.rrd";
static char *load_ds_def[] = 
{
	"DS:percent:GAUGE:"COLLECTD_HEARTBEAT":0:100",
};
static int load_ds_num = 1;

static char *charge_file_template = "apcups/charge.rrd";
static char *charge_ds_def[] = 
{
	"DS:charge:GAUGE:"COLLECTD_HEARTBEAT":0:U",
};
static int charge_ds_num = 1;

static char *time_file_template = "apcups/time.rrd";
static char *time_ds_def[] = 
{
	"DS:timeleft:GAUGE:"COLLECTD_HEARTBEAT":0:100",
};
static int time_ds_num = 1;

static char *temp_file_template = "apcups/temperature.rrd";
static char *temp_ds_def[] = 
{
	/* -273.15 is absolute zero */
	"DS:temperature:GAUGE:"COLLECTD_HEARTBEAT":-274:U",
};
static int temp_ds_num = 1;

static char *freq_file_template = "apcups/frequency-%s.rrd";
static char *freq_ds_def[] = 
{
	"DS:frequency:GAUGE:"COLLECTD_HEARTBEAT":0:U",
};
static int freq_ds_num = 1;

static char *config_keys[] =
{
	"Host",
	"Port",
	NULL
};
static int config_keys_num = 2;

#endif /* if APCMAIN */

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

#define BIG_BUF 4096

/*
 * Read nbytes from the network.
 * It is possible that the total bytes require in several
 * read requests
 */
static int read_nbytes (int *fd, char *ptr, int nbytes)
{
	int nleft;
	int nread;

	nleft = nbytes;
	nread = -1;

	assert (*fd >= 0);

	while ((nleft > 0) && (nread != 0))
	{
		nread = read (*fd, ptr, nleft);

		if (nread == -1 && (errno == EINTR || errno == EAGAIN))
			continue;

		if (nread == -1)
		{
			*fd = -1;
			syslog (LOG_ERR, "apcups plugin: write failed: %s", strerror (errno));
			return (-1);
		}

		nleft -= nread;
		ptr += nread;
	}

	return (nbytes - nleft);
}

/*
 * Write nbytes to the network.
 * It may require several writes.
 */
static int write_nbytes (int *fd, void *buf, int buflen)
{
	int nleft;
	int nwritten;
	char *ptr;

	assert (buflen > 0);
	assert (*fd >= 0);

	ptr = (char *) buf;

	nleft = buflen;
	while (nleft > 0)
	{
		nwritten = write (*fd, ptr, nleft);

		if ((nwritten == -1) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (nwritten == -1)
		{
			syslog (LOG_ERR, "Writing to socket failed: %s", strerror (errno));
			*fd = -1;
			return (-1);
		}

		nleft -= nwritten;
		ptr += nwritten;
	}

	/* If we get here, (nleft <= 0) is true */
	return (buflen);
}

#if 0
/* Close the network connection */
static void net_close (int *fd)
{
	short pktsiz = 0;

	assert (*fd >= 0);

	/* send EOF sentinel */
	write_nbytes (fd, &pktsiz, sizeof (short));

	close (*fd);
	*fd = -1;
}
#endif


/*     
 * Open a TCP connection to the UPS network server
 * Returns -1 on error
 * Returns socket file descriptor otherwise
 */
static int net_open (char *host, char *service, int port)
{
	int              sd;
	int              status;
	char             port_str[8];
	struct addrinfo  ai_hints;
	struct addrinfo *ai_return;
	struct addrinfo *ai_list;

	assert ((port > 0x00000000) && (port <= 0x0000FFFF));

	/* Convert the port to a string */
	snprintf (port_str, 8, "%i", port);
	port_str[7] = '\0';

	/* Resolve name */
	memset ((void *) &ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_family   = AF_INET; /* XXX: Change this to `AF_UNSPEC' if apcupsd can handle IPv6 */
	ai_hints.ai_socktype = SOCK_STREAM;

	status = getaddrinfo (host, port_str, &ai_hints, &ai_return);
	if (status != 0)
	{
		DBG ("getaddrinfo failed: %s", status == EAI_SYSTEM ? strerror (errno) : gai_strerror (status));
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
		DBG ("Unable to open a socket");
		freeaddrinfo (ai_return);
		return (-1);
	}

	status = connect (sd, ai_list->ai_addr, ai_list->ai_addrlen);

	freeaddrinfo (ai_return);

	if (status != 0) /* `connect(2)' failed */
	{
		DBG ("connect failed: %s", strerror (errno));
		return (-1);
	}

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
	int   nbytes;
	short pktsiz;

	/* get data size -- in short */
	if ((nbytes = read_nbytes (sockfd, (char *) &pktsiz, sizeof (short))) <= 0)
		return (-1);

	if (nbytes != sizeof (short))
		return (-2);

	pktsiz = ntohs (pktsiz);
	if (pktsiz > buflen)
	{
		DBG ("record length too large");
		return (-2);
	}

	if (pktsiz == 0)
		return (0);

	/* now read the actual data */
	if ((nbytes = read_nbytes (sockfd, buf, pktsiz)) <= 0)
		return (-2);

	if (nbytes != pktsiz)
		return (-2);

	return (nbytes);
} /* static int net_recv (int sockfd, char *buf, int buflen) */

/*
 * Send a message over the network. The send consists of
 * two network packets. The first is sends a short containing
 * the length of the data packet which follows.
 * Returns zero on success
 * Returns non-zero on error
 */
static int net_send (int *sockfd, char *buff, int len)
{
	int rc;
	short packet_size;

	assert (len > 0);

	/* send short containing size of data packet */
	packet_size = htons ((short) len);

	rc = write_nbytes (sockfd, &packet_size, sizeof (packet_size));
	if (rc != sizeof (packet_size))
		return (-1);

	/* send data packet */
	rc = write_nbytes (sockfd, buff, len);
	if (rc != len)
		return (-1);

	return (0);
}

/* Get and print status from apcupsd NIS server */
static int apc_query_server (char *host, int port,
		struct apc_detail_s *apcups_detail)
{
	int     n;
	char    recvline[1024];
	char   *tokptr;
	char   *key;
	double  value;

	static int sockfd   = -1;
	static int complain = 0;

#if APCMAIN
# define PRINT_VALUE(name, val) printf("  Found property: name = %s; value = %f;\n", name, val)
#else
# define PRINT_VALUE(name, val) /**/
#endif

	if (sockfd < 0)
	{
		if ((sockfd = net_open (host, NULL, port)) < 0)
		{
			/* Complain once every six hours. */
			int complain_step = 21600 / atoi (COLLECTD_STEP);

			if ((complain % complain_step) == 0)
				syslog (LOG_ERR, "apcups plugin: Connecting to the apcupsd failed.");
			complain++;

			return (-1);
		}
		complain = 0;
	}

	if (net_send (&sockfd, "status", 6) < 0)
	{
		syslog (LOG_ERR, "apcups plugin: Writing to the socket failed.");
		return (-1);
	}

 	/* XXX: Do we read `n' or `n-1' bytes? */
	while ((n = net_recv (&sockfd, recvline, sizeof (recvline) - 1)) > 0)
	{
		assert (n < sizeof (recvline));
		recvline[n] = '\0';
#if APCMAIN
		printf ("net_recv = `%s';\n", recvline);
#endif /* if APCMAIN */

		tokptr = strtok (recvline, ":");
		while (tokptr != NULL)
		{
			key = tokptr;
			if ((tokptr = strtok (NULL, " \t")) == NULL)
				continue;
			value = atof (tokptr);
			PRINT_VALUE (key, value);

			if (strcmp ("LINEV", key) == 0)
				apcups_detail->linev = value;
			else if (strcmp ("BATTV", tokptr) == 0)
				apcups_detail->battv = value;
			else if (strcmp ("ITEMP", tokptr) == 0)
				apcups_detail->itemp = value;
			else if (strcmp ("LOADPCT", tokptr) == 0)
				apcups_detail->loadpct = value;
			else if (strcmp ("BCHARGE", tokptr) == 0)
				apcups_detail->bcharge = value;
			else if (strcmp ("OUTPUTV", tokptr) == 0)
				apcups_detail->outputv = value;
			else if (strcmp ("LINEFREQ", tokptr) == 0)
				apcups_detail->linefreq = value;
			else if (strcmp ("TIMELEFT", tokptr) == 0)
				apcups_detail->timeleft = value;
			else
			{
				syslog (LOG_WARNING, "apcups plugin: Received unknown property from apcupsd: `%s' = %f",
						key, value);
			}

			tokptr = strtok (NULL, ":");
		} /* while (tokptr != NULL) */
	}

	if (n < 0)
	{
		syslog (LOG_WARNING, "apcups plugin: Error reading from socket");
		return (-1);
	}

	return (0);
}

#if APCMAIN
/*
 * This is used for testing apcups in a standalone mode.
 * Usefull for debugging.
 */
int main (int argc, char **argv)
{
	/* we are not really going to use this */
	struct apc_detail_s apcups_detail;

	openlog ("apcups", LOG_PID | LOG_NDELAY | LOG_LOCAL1);

	if (!*host || strcmp (host, "0.0.0.0") == 0)
		host = "localhost";

	if(do_apc_status (host, port, &apcups_detail) < 0)
	{
		printf("apcups: Failed...\n");
		return(-1);
	}

	apc_query_server (global_host, global_port, &apcups_detail);

	return 0;
}
#else
static int apcups_config (char *key, char *value)
{
	if (strcasecmp (key, "host") == 0)
	{
		if (global_host != NULL)
		{
			free (global_host);
			global_host = NULL;
		}
		if ((global_host = strdup (value)) == NULL)
			return (1);
	}
	else if (strcasecmp (key, "Port") == 0)
	{
		int port_tmp = atoi (value);
		if (port_tmp < 1 || port_tmp > 65535)
		{
			syslog (LOG_WARNING, "apcups plugin: Invalid port: %i", port_tmp);
			return (1);
		}
		global_port = port_tmp;
	}
	else
	{
		return (-1);
	}
	return (0);
}

static void apcups_init (void)
{
	return;
}

static void apc_write_voltage (char *host, char *inst, char *val)
{
	char file[512];
	int  status;

	status = snprintf (file, 512, bvolt_file_template, inst);
	if ((status < 1) || (status >= 512))
		return;

	rrd_update_file (host, file, val, bvolt_ds_def, bvolt_ds_num);
}

static void apc_write_charge (char *host, char *inst, char *val)
{
	rrd_update_file (host, charge_file_template, val, charge_ds_def, charge_ds_num);
}

static void apc_write_percent (char *host, char *inst, char *val)
{
	rrd_update_file (host, load_file_template, val, load_ds_def, load_ds_num);
}

static void apc_write_timeleft (char *host, char *inst, char *val)
{
	rrd_update_file (host, time_file_template, val, time_ds_def, time_ds_num);
}

static void apc_write_temperature (char *host, char *inst, char *val)
{
	rrd_update_file (host, temp_file_template, val, temp_ds_def, temp_ds_num);
}

static void apc_write_frequency (char *host, char *inst, char *val)
{
	char file[512];
	int  status;

	status = snprintf (file, 512, freq_file_template, inst);
	if ((status < 1) || (status >= 512))
		return;

	rrd_update_file (host, file, val, freq_ds_def, freq_ds_num);
}

static void apc_submit_generic (char *type, char *inst,
		double value)
{
	char buf[512];
	int  status;

	status = snprintf (buf, 512, "%u:%f",
			(unsigned int) curtime, value);
	if ((status < 1) || (status >= 512))
		return;

	plugin_submit (type, inst, buf);
}

static void apc_submit (struct apc_detail_s *apcups_detail)
{
	apc_submit_generic ("apcups_voltage",    "input",   apcups_detail->linev);
	apc_submit_generic ("apcups_voltage",    "output",  apcups_detail->outputv);
	apc_submit_generic ("apcups_voltage",    "battery", apcups_detail->battv);
	apc_submit_generic ("apcups_charge",     "-",       apcups_detail->bcharge);
	apc_submit_generic ("apcups_charge_pct", "-",       apcups_detail->loadpct);
	apc_submit_generic ("apcups_timeleft",   "-",       apcups_detail->timeleft);
	apc_submit_generic ("apcups_temp",       "-",       apcups_detail->itemp);
	apc_submit_generic ("apcups_frequency",  "input",   apcups_detail->linefreq);
}

static void apcups_read (void)
{
	struct apc_detail_s apcups_detail;
	int status;

	if (global_host == NULL)
		return;
	
	apcups_detail.linev    =   -1.0;
	apcups_detail.outputv  =   -1.0;
	apcups_detail.battv    =   -1.0;
	apcups_detail.loadpct  =   -1.0;
	apcups_detail.bcharge  =   -1.0;
	apcups_detail.timeleft =   -1.0;
	apcups_detail.itemp    = -300.0;
	apcups_detail.linefreq =   -1.0;
  
	status = apc_query_server (global_host, global_port, &apcups_detail);
 
	/*
	 * if we did not connect then do not bother submitting
	 * zeros. We want rrd files to have NAN.
	 */
	if (status != 0)
		return;

	apc_submit (&apcups_detail);
} /* apcups_read */

void module_register (void)
{
	plugin_register (MODULE_NAME, apcups_init, apcups_read, NULL);
	plugin_register ("apcups_voltage",    NULL, NULL, apc_write_voltage);
	plugin_register ("apcups_charge",     NULL, NULL, apc_write_charge);
	plugin_register ("apcups_charge_pct", NULL, NULL, apc_write_percent);
	plugin_register ("apcups_timeleft",   NULL, NULL, apc_write_timeleft);
	plugin_register ("apcups_temp",       NULL, NULL, apc_write_temperature);
	plugin_register ("apcups_frequency",  NULL, NULL, apc_write_frequency);
	cf_register (MODULE_NAME, apcups_config, config_keys, config_keys_num);
}

#endif /* if APCMAIN */
#undef MODULE_NAME
