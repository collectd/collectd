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

#include "collectd.h"
#include "common.h" /* rrd_update_file */
#include "plugin.h" /* plugin_register, plugin_submit */
#include "configfile.h" /* cf_register */
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
#if HAVE_ARPA_INET_H
# include <arpa/inet.h> /* inet_addr */
#endif

#if 0
#include <pwd.h>
#include <setjmp.h> /* FIXME: Is this really neccessary? */
#include <termios.h> /* FIXME: Is this really neccessary? */
#include <sys/ioctl.h> /* FIXME: Is this really neccessary? */
#include <sys/ipc.h> /* FIXME: Is this really neccessary? */
#include <sys/sem.h> /* FIXME: Is this really neccessary? */
#include <sys/shm.h> /* FIXME: Is this really neccessary? */
#endif

#define NISPORT 3551
#define _(String) (String)
#define N_(String) (String)
#define MAXSTRING               256
#define MODULE_NAME "apcups"

/* Default values for contacting daemon */
static char *host = "localhost";
static int   port = NISPORT;

/* 
 * The following are only if not compiled to test the module with its own main.
*/
/* FIXME: Rename DSes to be more generic and follow established conventions. */
#ifndef APCMAIN
static char *volt_file_template = "apcups_volt-%s.rrd";
static char *volt_ds_def[] = 
{
	"DS:linev:GAUGE:"COLLECTD_HEARTBEAT":0:250",
	"DS:outputv:GAUGE:"COLLECTD_HEARTBEAT":0:250",
	NULL
};
static int volt_ds_num = 2;

static char *bvolt_file_template = "apcups_bvolt-%s.rrd";
static char *bvolt_ds_def[] = 
{
	"DS:battv:GAUGE:"COLLECTD_HEARTBEAT":0:100",
};
static int bvolt_ds_num = 1;

static char *load_file_template = "apcups_load-%s.rrd";
static char *load_ds_def[] = 
{
	"DS:loadpct:GAUGE:"COLLECTD_HEARTBEAT":0:120",
};
static int load_ds_num = 1;

static char *charge_file_template = "apcups_charge-%s.rrd";
static char *charge_ds_def[] = 
{
	"DS:bcharge:GAUGE:"COLLECTD_HEARTBEAT":0:100",
};
static int charge_ds_num = 1;

static char *time_file_template = "apcups_time-%s.rrd";
static char *time_ds_def[] = 
{
	"DS:timeleft:GAUGE:"COLLECTD_HEARTBEAT":0:100",
};
static int time_ds_num = 1;

static char *temp_file_template = "apcups_temp-%s.rrd";
static char *temp_ds_def[] = 
{
	"DS:itemp:GAUGE:"COLLECTD_HEARTBEAT":0:100",
};
static int temp_ds_num = 1;

static char *freq_file_template = "apcups_freq-%s.rrd";
static char *freq_ds_def[] = 
{
	"DS:linefreq:GAUGE:"COLLECTD_HEARTBEAT":0:65",
};
static int freq_ds_num = 1;

static char *config_keys[] =
{
	"Host",
	"Port",
	NULL
};
static int config_keys_num = 2;

#endif /* ifndef APCMAIN */

struct apc_detail_s
{
	float linev;
	float loadpct;
	float bcharge;
	float timeleft;
	float outputv;
	float itemp;
	float battv;
	float linefreq;
};

#define BIG_BUF 4096

/*
 * Read nbytes from the network.
 * It is possible that the total bytes require in several
 * read requests
 */
static int read_nbytes(int fd, char *ptr, int nbytes)
{
	int nleft, nread;

	nleft = nbytes;

	while (nleft > 0) {
		do {
			nread = read(fd, ptr, nleft);
		} while (nread == -1 && (errno == EINTR || errno == EAGAIN));

		if (nread <= 0) {
			return (nread);           /* error, or EOF */
		}

		nleft -= nread;
		ptr += nread;
	}

	return (nbytes - nleft);        /* return >= 0 */
}

/*
 * Write nbytes to the network.
 * It may require several writes.
 */
static int write_nbytes(int fd, void *buf, int buflen)
{
	int nleft;
	int nwritten;
	char *ptr;

	ptr = (char *) buf;

	nleft = buflen;
	while (nleft > 0)
	{
		nwritten = write(fd, ptr, nleft);

		if (nwritten <= 0)
		{
			syslog (LOG_ERR, "Writing to socket failed: %s", strerror (errno));
			return (nwritten);
		}

		nleft -= nwritten;
		ptr += nwritten;
	}

	/* If we get here, (nleft <= 0) is true */
	return (buflen);
}

/* Close the network connection */
static void net_close (int sockfd)
{
	short pktsiz = 0;

	/* send EOF sentinel */
	write_nbytes (sockfd, &pktsiz, sizeof (short));
	close (sockfd);
}


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
} /* int net_open(char *host, char *service, int port) */

/* 
 * Receive a message from the other end. Each message consists of
 * two packets. The first is a header that contains the size
 * of the data that follows in the second packet.
 * Returns number of bytes read
 * Returns 0 on end of file
 * Returns -1 on hard end of file (i.e. network connection close)
 * Returns -2 on error
 */
static int net_recv (int sockfd, char *buf, int buflen)
{
	int   nbytes;
	short pktsiz;

	/* get data size -- in short */
	if ((nbytes = read_nbytes (sockfd, (char *) &pktsiz, sizeof (short))) <= 0)
		return (-1);

	if (nbytes != sizeof (short))
		return (-2);

	pktsiz = ntohs(pktsiz);
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
static int net_send (int sockfd, char *buff, int len)
{
	int rc;
	short packet_size;

	/* send short containing size of data packet */
	packet_size = htons ((short) len);

	rc = write_nbytes(sockfd, &packet_size, sizeof (packet_size));
	if (rc != sizeof(packet_size))
		return (-1);

	/* send data packet */
	rc = write_nbytes (sockfd, buff, len);
	if (rc != len)
		return (-1);

	return (0);
}

/* Get and print status from apcupsd NIS server */
static int do_pthreads_status (char *host, int port,
		struct apc_detail_s *apcups_detail)
{
	int     sockfd;
	int     n;
	char    recvline[MAXSTRING + 1];
	char   *tokptr;
	char   *key;
	double  value;
#if APCMAIN
# define PRINT_VALUE(name, val) printf("  Found property: name = %s; value = %f;\n", name, val)
#else
# define PRINT_VALUE(name, val) /**/
#endif

	/* TODO: Keep the socket open, if possible */
	if ((sockfd = net_open (host, NULL, port)) < 0)
	{
		syslog (LOG_ERR, "apcups plugin: Connecting to the apcupsd failed.");
		return (-1);
	}

	net_send (sockfd, "status", 6);

	while ((n = net_recv (sockfd, recvline, sizeof (recvline))) > 0)
	{
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

	net_close (sockfd);

	if (n < 0)
	{
		syslog (LOG_WARNING, "apcups plugin: Error reading from socket");
		return (-1);
	}

	return (0);
}

#ifdef APCMAIN
int main(int argc, char **argv)
{
	/* we are not really going to use this */
	struct apc_detail_s apcups_detail;

	if (!*host || strcmp(host, "0.0.0.0") == 0)
		host = "localhost";

	do_pthreads_status(host, port, &apcups_detail);

	return 0;
}
#else
static void apcups_init (void)
{
	return;
}

static int apcups_config (char *key, char *value)
{
  static char lhost[126];
  
  if (strcasecmp (key, "host") == 0)
    {
      lhost[0] = '\0';
      strcpy(lhost,key);
      host = lhost;
    }
  else if (strcasecmp (key, "Port") == 0)
    {
      int port_tmp = atoi (value);
      if(port_tmp < 1 || port_tmp > 65535) {
	syslog (LOG_WARNING, "apcups: `port' failed: %s",
		value);
	return (1);
      } else {
	port = port_tmp;
      }
    }
  else
    {
      return (-1);
    }
  return(0);
}

#define BUFSIZE 256
static void apcups_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f:%f",
		      (unsigned int) curtime,
		      apcups_detail->linev,
		      apcups_detail->outputv) >= BUFSIZE)
	  return;
	
	plugin_submit (MODULE_NAME, host, buf);
}

static void apc_bvolt_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f",
		      (unsigned int) curtime,
		      apcups_detail->battv) >= BUFSIZE)
	  return;
	
	plugin_submit ("apcups_bvolt", host, buf);
}

static void apc_load_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f",
		      (unsigned int) curtime,
		      apcups_detail->loadpct) >= BUFSIZE)
	  return;
	
	plugin_submit ("apcups_load", host, buf);
}

static void apc_charge_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f",
		      (unsigned int) curtime,
		      apcups_detail->bcharge) >= BUFSIZE)
	  return;
	
	plugin_submit ("apcups_charge", host, buf);
}

static void apc_temp_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f",
		      (unsigned int) curtime,
		      apcups_detail->itemp) >= BUFSIZE)
	  return;
	
	plugin_submit ("apcups_temp", host, buf);
}

static void apc_time_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f",
		      (unsigned int) curtime,
		      apcups_detail->timeleft) >= BUFSIZE)
	  return;
	
	plugin_submit ("apcups_time", host, buf);
}

static void apc_freq_submit (char *host,
			   struct apc_detail_s *apcups_detail)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f",
		      (unsigned int) curtime,
		      apcups_detail->linefreq) >= BUFSIZE)
	  return;
	
	plugin_submit ("apcups_freq", host, buf);
}
#undef BUFSIZE

static void apcups_read (void)
{
  struct apc_detail_s apcups_detail;
  int status;
	
  apcups_detail.linev = 0.0;
  apcups_detail.loadpct = 0.0;
  apcups_detail.bcharge = 0.0;
  apcups_detail.timeleft = 0.0;
  apcups_detail.outputv = 0.0;
  apcups_detail.itemp = 0.0;
  apcups_detail.battv = 0.0;
  apcups_detail.linefreq = 0.0;

  
  if (!*host || strcmp(host, "0.0.0.0") == 0)
    host = "localhost";
  
  status = do_pthreads_status(host, port, &apcups_detail);
 
  /*
   * if we did not connect then do not bother submitting
   * zeros. We want rrd files to have NAN.
  */
  if (status != 0)
	  return;

  apcups_submit (host, &apcups_detail);
  apc_bvolt_submit (host, &apcups_detail);
  apc_load_submit (host, &apcups_detail);
  apc_charge_submit (host, &apcups_detail);
  apc_temp_submit (host, &apcups_detail);
  apc_time_submit (host, &apcups_detail);
  apc_freq_submit (host, &apcups_detail);
}


static void apcups_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, volt_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, volt_ds_def, volt_ds_num);
}

static void apc_bvolt_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, bvolt_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, bvolt_ds_def, bvolt_ds_num);
}

static void apc_load_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, load_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, load_ds_def, load_ds_num);
}

static void apc_charge_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, charge_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, charge_ds_def, charge_ds_num);
}

static void apc_temp_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, temp_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, temp_ds_def, temp_ds_num);
}

static void apc_time_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, time_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, time_ds_def, time_ds_num);
}

static void apc_freq_write (char *host, char *inst, char *val)
{
  char file[512];
  int status;
  
  status = snprintf (file, 512, freq_file_template, inst);
  if (status < 1)
    return;
  else if (status >= 512)
    return;
  
  rrd_update_file (host, file, val, freq_ds_def, freq_ds_num);
}

void module_register (void)
{
	plugin_register (MODULE_NAME, apcups_init, apcups_read, apcups_write);
	plugin_register ("apcups_bvolt", NULL, NULL, apc_bvolt_write);
	plugin_register ("apcups_load", NULL, NULL, apc_load_write);
	plugin_register ("apcups_charge", NULL, NULL, apc_charge_write);
	plugin_register ("apcups_temp", NULL, NULL, apc_temp_write);
	plugin_register ("apcups_time", NULL, NULL, apc_time_write);
	plugin_register ("apcups_freq", NULL, NULL, apc_freq_write);
	cf_register (MODULE_NAME, apcups_config, config_keys, config_keys_num);
}

#endif /* ifdef APCMAIN */
#undef MODULE_NAME
