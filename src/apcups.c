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

/* FIXME: Check defines before including anything! */
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <ctype.h>
#include <syslog.h>
#include <limits.h>
#include <pwd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <termios.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define NISPORT 3551
#define _(String) (String)
#define N_(String) (String)
#define MAXSTRING               256
#define Error_abort0(fmd) generic_error_out(__FILE__, __LINE__, fmd)
#define MODULE_NAME "apcups"


/* Prototypes */
static void generic_error_out(const char *, int , const char *, ...);

/* Default values for contacting daemon */
static char *host = "localhost";
static int port = NISPORT;

static struct sockaddr_in tcp_serv_addr;  /* socket information */
static int net_errno = 0;                 /* error number -- not yet implemented */
static char *net_errmsg = NULL;           /* pointer to error message */
static char net_errbuf[256];              /* error message buffer for messages */

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
 * Subroutine normally called by macro error_abort() to print
 * FATAL ERROR message and supplied error message
 */
static void generic_error_out(const char *file, int line, const char *fmt, ...)
{
	char buf[256];
	va_list arg_ptr;
	int i;

	snprintf(buf, sizeof(buf), _("FATAL ERROR in %s at line %d\n"), file, line);
	i = strlen(buf);
	va_start(arg_ptr, fmt);
	vsnprintf((char *)&buf[i], sizeof(buf) - i, (char *)fmt, arg_ptr);
	va_end(arg_ptr);
	fprintf(stdout, "%s", buf);

	exit(1);
}

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
			net_errno = errno;
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
static int write_nbytes(int fd, char *ptr, int nbytes)
{
	int nleft, nwritten;

	nleft = nbytes;
	while (nleft > 0) {
		nwritten = write(fd, ptr, nleft);

		if (nwritten <= 0) {
			net_errno = errno;
			return (nwritten);        /* error */
		}

		nleft -= nwritten;
		ptr += nwritten;
	}

	return (nbytes - nleft);
}

/* Close the network connection */
static void net_close (int sockfd)
{
	short pktsiz = 0;

	/* send EOF sentinel */
	write_nbytes(sockfd, (char *) &pktsiz, sizeof(short));
	close (sockfd);
}


/*     
 * Open a TCP connection to the UPS network server
 * Returns -1 on error
 * Returns socket file descriptor otherwise
 */
static int net_open(char *host, char *service, int port)
{
	int sockfd;
	struct hostent *hp;
	unsigned int inaddr; /* Careful here to use unsigned int for */
	                     /* compatibility with Alpha */

	/* 
	 * Fill in the structure serv_addr with the address of the server that
	 * we want to connect with.
	 */
	memset((char *)&tcp_serv_addr, 0, sizeof(tcp_serv_addr));
	tcp_serv_addr.sin_family = AF_INET;
	tcp_serv_addr.sin_port = htons(port);

	if ((inaddr = inet_addr(host)) != INADDR_NONE) {
		tcp_serv_addr.sin_addr.s_addr = inaddr;
	} else {
		if ((hp = gethostbyname(host)) == NULL) {
			net_errmsg = "tcp_open: hostname error\n";
			return -1;
		}

		if (hp->h_length != sizeof(inaddr) || hp->h_addrtype != AF_INET) {
			net_errmsg = "tcp_open: funny gethostbyname value\n";
			return -1;
		}

		tcp_serv_addr.sin_addr.s_addr = *(unsigned int *)hp->h_addr;
	}

	/* Open a TCP socket */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		net_errmsg = "tcp_open: cannot open stream socket\n";
		return -1;
	}

	/* connect to server */
#if defined HAVE_OPENBSD_OS || defined HAVE_FREEBSD_OS
	/* 
	 * Work around a bug in OpenBSD & FreeBSD userspace pthreads
	 * implementations. Rationale is the same as described above.
	 */
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL));
#endif

	if (connect(sockfd, (struct sockaddr *)&tcp_serv_addr, sizeof(tcp_serv_addr)) < 0) {
		snprintf(net_errbuf, sizeof(net_errbuf),
				_("tcp_open: cannot connect to server %s on port %d.\n"
					"ERR=%s\n"), host, port, strerror(errno));
		net_errmsg = net_errbuf;
		close(sockfd);
		return -1;
	}

	return sockfd;
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
static int net_recv(int sockfd, char *buff, int maxlen)
{
	int nbytes;
	short pktsiz;

	/* get data size -- in short */
	if ((nbytes = read_nbytes(sockfd, (char *)&pktsiz, sizeof(short))) <= 0) {
		/* probably pipe broken because client died */
		return -1;                   /* assume hard EOF received */
	}
	if (nbytes != sizeof(short))
		return -2;

	pktsiz = ntohs(pktsiz);         /* decode no. of bytes that follow */
	if (pktsiz > maxlen) {
		net_errmsg = "net_recv: record length too large\n";
		return -2;
	}
	if (pktsiz == 0)
		return 0;                    /* soft EOF */

	/* now read the actual data */
	if ((nbytes = read_nbytes(sockfd, buff, pktsiz)) <= 0) {
		net_errmsg = "net_recv: read_nbytes error\n";
		return -2;
	}
	if (nbytes != pktsiz) {
		net_errmsg = "net_recv: error in read_nbytes\n";
		return -2;
	}

	return (nbytes);                /* return actual length of message */
}

/*
 * Send a message over the network. The send consists of
 * two network packets. The first is sends a short containing
 * the length of the data packet which follows.
 * Returns number of bytes sent
 * Returns -1 on error
 */
static int net_send(int sockfd, char *buff, int len)
{
	int rc;
	short pktsiz;

	/* send short containing size of data packet */
	pktsiz = htons((short)len);
	rc = write_nbytes(sockfd, (char *)&pktsiz, sizeof(short));
	if (rc != sizeof(short)) {
		net_errmsg = "net_send: write_nbytes error of length prefix\n";
		return -1;
	}

	/* send data packet */
	rc = write_nbytes(sockfd, buff, len);
	if (rc != len) {
		net_errmsg = "net_send: write_nbytes error\n";
		return -1;
	}

	return rc;
}


/* Get and print status from apcupsd NIS server */
static int do_pthreads_status(char *host, int port, struct apc_detail_s *apcups_detail)
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

	if (n < 0)
		Error_abort0(net_errmsg);

	net_close(sockfd);

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
  
  do_pthreads_status(host, port, &apcups_detail);
 
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
