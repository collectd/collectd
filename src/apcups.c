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

<<<<<<< .mine
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>			/* Used for socket connections */
#include <netinet/in.h>			/* Used for socket connections */
#include <arpa/inet.h>			/* Used for socket connections */
=======
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

#if 0
#if HAVE_ARPA_INET_H
# include <arpa/inet.h> /* inet_addr */
#endif
#include <pwd.h>
#include <setjmp.h> /* FIXME: Is this really neccessary? */
#include <termios.h> /* FIXME: Is this really neccessary? */
#include <sys/ioctl.h> /* FIXME: Is this really neccessary? */
#include <sys/ipc.h> /* FIXME: Is this really neccessary? */
#include <sys/sem.h> /* FIXME: Is this really neccessary? */
#include <sys/shm.h> /* FIXME: Is this really neccessary? */
#endif
>>>>>>> .r743



#define NISPORT 3551
#define _(String) (String)
#define N_(String) (String)
#define MAXSTRING               256
#define MODULE_NAME "apcups"

<<<<<<< .mine

=======
>>>>>>> .r743
/* Default values for contacting daemon */
<<<<<<< .mine
static char *host = "localhost";	/* the default host to connect to */
static int port = NISPORT;		/* the default port to connect to */
=======
static char *global_host = NULL;
static int   global_port = NISPORT;
>>>>>>> .r743

<<<<<<< .mine
/*
 * This is used in do_apc_status() to track the last connection state.
 * We do not want the read function spitting out an error every "step"
 * seconds (usually 10 secs).
 */
static int apcConnStatus = 0;

static struct sockaddr_in tcp_serv_addr;  /* socket information */
static char *net_errmsg = NULL;           /* pointer to error message */
static char net_errbuf[256];              /* error message buffer for messages */

=======
>>>>>>> .r743
/* 
 * The following are normally compiled, when the module is compiled with its
 * own main for testing these are ifdef'd out.
 */
/* 
 * FIXME: Rename DSes to be more generic and follow established conventions.
 *  However, they currently match the values put out by apcupsd.
 */
#ifndef APCMAIN
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

#endif /* ifndef APCMAIN */

struct apc_detail_s
{
<<<<<<< .mine
        int connected;
	float linev;
	float loadpct;
	float bcharge;
	float timeleft;
	float outputv;
	float itemp;
	float battv;
	float linefreq;
=======
	double linev;
	double loadpct;
	double bcharge;
	double timeleft;
	double outputv;
	double itemp;
	double battv;
	double linefreq;
>>>>>>> .r743
};

<<<<<<< .mine
=======
#define BIG_BUF 4096

>>>>>>> .r743
/*
 * Read nbytes from the network.
 * It is possible that the total bytes require in several
 * read requests
 */
static int read_nbytes (int fd, char *ptr, int nbytes)
{
	int nleft, nread;

	nleft = nbytes;

	while (nleft > 0) {
		do {
			nread = read (fd, ptr, nleft);
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
static int write_nbytes (int fd, void *buf, int buflen)
{
	int nleft;
	int nwritten;
	char *ptr;

	ptr = (char *) buf;

<<<<<<< .mine
		if (nwritten <= 0) {
			return (nwritten);        /* error */
=======
	nleft = buflen;
	while (nleft > 0)
	{
		nwritten = write (fd, ptr, nleft);

		if (nwritten <= 0)
		{
			syslog (LOG_ERR, "Writing to socket failed: %s", strerror (errno));
			return (nwritten);
>>>>>>> .r743
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
static int net_recv (int sockfd, char *buf, int buflen)
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
static int net_send (int sockfd, char *buff, int len)
{
	int rc;
	short packet_size;

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

<<<<<<< .mine
/* 
 * Get and print status from apcupsd NIS server if APCMAIN is defined. 
 * Poplate apcups_detail structure for plugin submit().
 */
static int do_apc_status(char *host, int port, struct apc_detail_s *apcups_detail)
=======
/* Get and print status from apcupsd NIS server */
static int apc_query_server (char *host, int port,
		struct apc_detail_s *apcups_detail)
>>>>>>> .r743
{
	int     sockfd;
	int     n;
	char    recvline[MAXSTRING + 1];
	char   *tokptr;
	char   *key;
	float  value;
#if APCMAIN
# define PRINT_VALUE(name, val) printf("  Found property: name = %s; value = %f;\n", name, val)
#else
# define PRINT_VALUE(name, val) /**/
#endif

<<<<<<< .mine
	/* 
	 * TODO: Keep the socket open, if possible.
	 * Can open socket in module init, but without a corresponding module
	 * uninit there is no place to gracefully close the socket.
	 */
=======
	/* TODO: Keep the socket open, if possible */
>>>>>>> .r743
	if ((sockfd = net_open (host, NULL, port)) < 0)
	{
		/* 
		 * When the integer apcConnStatus rolls over it will print out
		 * again, if we haven't connected by then.
		 */
		if (apcConnStatus++ == 0)
			syslog (LOG_ERR, "apcups plugin: Connecting to the apcupsd failed: %s",
				net_errmsg);
		return (-1);
	} else apcConnStatus = 0;

	if (net_send (sockfd, "status", 6) < 0)
	{
		syslog (LOG_ERR, "apcups plugin: sending to the apcupsd failed: %s",
			net_errmsg);
		return (-1);
	}

<<<<<<< .mine
=======
	net_send (sockfd, "status", 6);

>>>>>>> .r743
	while ((n = net_recv (sockfd, recvline, sizeof (recvline))) > 0)
	{
		recvline[n-1] = '\0';
#if APCMAIN
		printf ("net_recv = \"%s\"\n", recvline);
#endif /* if APCMAIN */

		tokptr = strtok (recvline, ":");
		while (tokptr != NULL)
		{
			key = tokptr;
			if ((tokptr = strtok (NULL, " \t")) == NULL)
				continue; 

			if (strncmp ("LINEV", key,5) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->linev = value;
			} else if (strncmp ("BATTV", key,5) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->battv = value;
			} else if (strncmp ("ITEMP", key,5) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->itemp = value;
			} else if (strncmp ("LOADPCT", key,7) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->loadpct = value;
			} else if (strncmp ("BCHARGE", key,7) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->bcharge = value;
			} else if (strncmp ("OUTPUTV", key,7) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->outputv = value;
			} else if (strncmp ("LINEFREQ", key,8) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->linefreq = value;
			} else if (strncmp ("TIMELEFT", key,8) == 0) {
				value = atof (tokptr);
				PRINT_VALUE (key, value);
				apcups_detail->timeleft = value;
			} 

			tokptr = strtok (NULL, ":");
		} /* while (tokptr != NULL) */
	}

<<<<<<< .mine
	if (n < 0) {
	  syslog(LOG_ERR, "apcups plugin: Error recieving data: %s",
		 net_errmsg);
	  net_errmsg = NULL;
	  return(-1);
	}
=======
	net_close (sockfd);

	if (n < 0)
	{
		syslog (LOG_WARNING, "apcups plugin: Error reading from socket");
		return (-1);
	}
>>>>>>> .r743

<<<<<<< .mine
	/* signal that we did in fact connect. */
	apcups_detail->connected = 1;

	net_close(sockfd);

=======
>>>>>>> .r743
	return (0);
}

#ifdef APCMAIN
<<<<<<< .mine
/*
 * This is used for testing apcups in a standalone mode.
 * Usefull for debugging.
 */
int main(int argc, char **argv)
=======
int main (int argc, char **argv)
>>>>>>> .r743
{
	/* we are not really going to use this */
	struct apc_detail_s apcups_detail;

<<<<<<< .mine
	openlog("apcups",LOG_PID | LOG_NDELAY | LOG_LOCAL1);

	if (!*host || strcmp(host, "0.0.0.0") == 0)
=======
	if (!*host || strcmp (host, "0.0.0.0") == 0)
>>>>>>> .r743
		host = "localhost";

<<<<<<< .mine
	if(do_apc_status(host, port, &apcups_detail) < 0) {
		printf("apcups: Failed...\n");
		return(-1);
	}
=======
	apc_query_server (global_host, global_port, &apcups_detail);
>>>>>>> .r743

	return 0;
}
#else
static int apcups_config (char *key, char *value)
{
<<<<<<< .mine
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

  if (strcmp(host, "0.0.0.0") == 0)
	host = "localhost";

  return(0);
=======
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
>>>>>>> .r743
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
<<<<<<< .mine
	struct apc_detail_s apcups_detail;
	
	apcups_detail.linev     = 0.0;
	apcups_detail.loadpct   = 0.0;
	apcups_detail.bcharge   = 0.0;
	apcups_detail.timeleft  = 0.0;
	apcups_detail.outputv   = 0.0;
	apcups_detail.itemp     = 0.0;
	apcups_detail.battv     = 0.0;
	apcups_detail.linefreq  = 0.0;
	apcups_detail.connected = 0;
=======
	char file[512];
	int  status;
>>>>>>> .r743

<<<<<<< .mine
  
	if (!*host || strcmp(host, "0.0.0.0") == 0)
		host = "localhost";
  
	if(do_apc_status(host, port, &apcups_detail) < 0) return;
 
	/*
 	 * if we did not connect then do not bother submitting
 	 * zeros. We want rrd files to have NAN.
	 */
	if(!apcups_detail.connected) return;
=======
	status = snprintf (file, 512, freq_file_template, inst);
	if ((status < 1) || (status >= 512))
		return;
>>>>>>> .r743

<<<<<<< .mine
	apcups_submit     (host, &apcups_detail);
	apc_bvolt_submit  (host, &apcups_detail);
	apc_load_submit   (host, &apcups_detail);
	apc_charge_submit (host, &apcups_detail);
	apc_temp_submit   (host, &apcups_detail);
	apc_time_submit   (host, &apcups_detail);
	apc_freq_submit   (host, &apcups_detail);
=======
	rrd_update_file (host, file, val, freq_ds_def, freq_ds_num);
>>>>>>> .r743
}

static void apc_submit_generic (char *type, char *inst,
		double value)
{
<<<<<<< .mine
	char file[512];
	int status;

	status = snprintf (file, 512, volt_file_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, volt_ds_def, volt_ds_num);
}
=======
	char buf[512];
	int  status;
>>>>>>> .r743

<<<<<<< .mine
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
=======
	status = snprintf (buf, 512, "%u:%f",
			(unsigned int) curtime, value);
	if ((status < 1) || (status >= 512))
		return;
>>>>>>> .r743

<<<<<<< .mine
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
=======
	plugin_submit (type, inst, buf);
>>>>>>> .r743
}

static void apc_submit (struct apc_detail_s *apcups_detail)
{
<<<<<<< .mine
	char file[512];
	int status;
  
	status = snprintf (file, 512, charge_file_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;
  
	rrd_update_file (host, file, val, charge_ds_def, charge_ds_num);
=======
	apc_submit_generic ("apcups_voltage",    "input",   apcups_detail->linev);
	apc_submit_generic ("apcups_voltage",    "output",  apcups_detail->outputv);
	apc_submit_generic ("apcups_voltage",    "battery", apcups_detail->battv);
	apc_submit_generic ("apcups_charge",     "-",       apcups_detail->bcharge);
	apc_submit_generic ("apcups_charge_pct", "-",       apcups_detail->loadpct);
	apc_submit_generic ("apcups_timeleft",   "-",       apcups_detail->timeleft);
	apc_submit_generic ("apcups_temp",       "-",       apcups_detail->itemp);
	apc_submit_generic ("apcups_frequency",  "input",   apcups_detail->linefreq);
>>>>>>> .r743
}

static void apcups_read (void)
{
<<<<<<< .mine
	char file[512];
	int status;
  
	status = snprintf (file, 512, temp_file_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;
  
	rrd_update_file (host, file, val, temp_ds_def, temp_ds_num);
}
=======
	struct apc_detail_s apcups_detail;
	int status;
>>>>>>> .r743

<<<<<<< .mine
static void apc_time_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;
=======
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
>>>>>>> .r743
  
<<<<<<< .mine
	status = snprintf (file, 512, time_file_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;
  
	rrd_update_file (host, file, val, time_ds_def, time_ds_num);
}
=======
	status = apc_query_server (global_host, global_port, &apcups_detail);
 
	/*
	 * if we did not connect then do not bother submitting
	 * zeros. We want rrd files to have NAN.
	 */
	if (status != 0)
		return;
>>>>>>> .r743

<<<<<<< .mine
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
=======
	apc_submit (&apcups_detail);
} /* apcups_read */
>>>>>>> .r743

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

#endif /* ifdef APCMAIN */
#undef MODULE_NAME
#undef MAXSTRING
#undef NISPORT
