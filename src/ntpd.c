/**
 * collectd - src/ntpd.c
 * Copyright (C) 2006  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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

#define MODULE_NAME "ntpd"

#if HAVE_SYS_SOCKET_H
# define NTPD_HAVE_READ 1
#else
# define NTPD_HAVE_READ 0
#endif

#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_NETDB_H
# include <netdb.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#if HAVE_SYS_POLL_H
# include <sys/poll.h>
#endif

/* drift */
static char *time_offset_file = "ntpd/time_offset-%s.rrd";
static char *time_offset_ds_def[] =
{
	"DS:ms:GAUGE:"COLLECTD_HEARTBEAT":0:100",
	NULL
};
static int time_offset_ds_num = 1;

static char *frequency_offset_file = "ntpd/frequency_offset-%s.rrd";
static char *frequency_offset_ds_def[] =
{
	"DS:ppm:GAUGE:"COLLECTD_HEARTBEAT":0:100",
	NULL
};
static int frequency_offset_ds_num = 1;

#if NTPD_HAVE_READ
# define NTPD_DEFAULT_HOST "localhost"
# define NTPD_DEFAULT_PORT "123"
static int   sock_descr = -1;
static char *ntpd_host = NULL;
static char *ntpd_port = NULL;
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * The following definitions were copied from the NTPd distribution  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#define MAXFILENAME 128
#define MAXSEQ  127
#define MODE_PRIVATE 7
#define NTP_OLDVERSION ((u_char) 1) /* oldest credible version */
#define IMPL_XNTPD 3

/* This structure is missing the message authentication code, since collectd
 * doesn't use it. */
struct req_pkt
{
	uint8_t  rm_vn_mode;
	uint8_t  auth_seq;
	uint8_t  implementation;		/* implementation number */
	uint8_t  request;			/* request number */
	uint16_t err_nitems;		/* error code/number of data items */
	uint16_t mbz_itemsize;		/* item size */
	char     data[MAXFILENAME + 48];	/* data area [32 prev](176 byte max) */
					/* struct conf_peer must fit */
};
#define REQ_LEN_NOMAC (sizeof(struct req_pkt))

/*
 * A response packet.  The length here is variable, this is a
 * maximally sized one.  Note that this implementation doesn't
 * authenticate responses.
 */
#define	RESP_HEADER_SIZE	(8)
#define	RESP_DATA_SIZE		(500)

struct resp_pkt
{
	uint8_t  rm_vn_mode;           /* response, more, version, mode */
	uint8_t  auth_seq;             /* key, sequence number */
	uint8_t  implementation;       /* implementation number */
	uint8_t  request;              /* request number */
	uint16_t err_nitems;           /* error code/number of data items */
	uint16_t mbz_itemsize;         /* item size */
	char     data[RESP_DATA_SIZE]; /* data area */
};

/*
 * Bit setting macros for multifield items.
 */
#define	RESP_BIT	0x80
#define	MORE_BIT	0x40

#define	ISRESPONSE(rm_vn_mode)	(((rm_vn_mode)&RESP_BIT)!=0)
#define	ISMORE(rm_vn_mode)	(((rm_vn_mode)&MORE_BIT)!=0)
#define INFO_VERSION(rm_vn_mode) ((u_char)(((rm_vn_mode)>>3)&0x7))
#define	INFO_MODE(rm_vn_mode)	((rm_vn_mode)&0x7)

#define	RM_VN_MODE(resp, more, version)		\
				((u_char)(((resp)?RESP_BIT:0)\
				|((more)?MORE_BIT:0)\
				|((version?version:(NTP_OLDVERSION+1))<<3)\
				|(MODE_PRIVATE)))

#define	INFO_IS_AUTH(auth_seq)	(((auth_seq) & 0x80) != 0)
#define	INFO_SEQ(auth_seq)	((auth_seq)&0x7f)
#define	AUTH_SEQ(auth, seq)	((u_char)((((auth)!=0)?0x80:0)|((seq)&0x7f)))

#define	INFO_ERR(err_nitems)	((u_short)((ntohs(err_nitems)>>12)&0xf))
#define	INFO_NITEMS(err_nitems)	((u_short)(ntohs(err_nitems)&0xfff))
#define	ERR_NITEMS(err, nitems)	(htons((u_short)((((u_short)(err)<<12)&0xf000)\
				|((u_short)(nitems)&0xfff))))

#define	INFO_MBZ(mbz_itemsize)	((ntohs(mbz_itemsize)>>12)&0xf)
#define	INFO_ITEMSIZE(mbz_itemsize)	((u_short)(ntohs(mbz_itemsize)&0xfff))
#define	MBZ_ITEMSIZE(itemsize)	(htons((u_short)(itemsize)))
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * End of the copied stuff..                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static void ntpd_init (void)
{
	return;
}

static void ntpd_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, time_offset_file, val,
			time_offset_ds_def, time_offset_ds_num); /* FIXME filename incorrect */
}

#if NTPD_HAVE_READ
static void ntpd_submit (double snum, double mnum, double lnum)
{
	char buf[256];

	if (snprintf (buf, 256, "%u:%.2f:%.2f:%.2f", (unsigned int) curtime,
				snum, mnum, lnum) >= 256)
		return;

	plugin_submit (MODULE_NAME, "-", buf);
}

/* returns `tv0 - tv1' in milliseconds or 0 if `tv1 > tv0' */
static int timeval_sub (const struct timeval *tv0, const struct timeval *tv1)
{
	int sec;
	int usec;

	if ((tv0->tv_sec < tv1->tv_sec)
			|| ((tv0->tv_sec == tv1->tv_sec) && (tv0->tv_usec < tv1->tv_usec)))
		return (0);

	sec  = tv0->tv_sec  - tv1->tv_sec;
	usec = tv0->tv_usec - tv1->tv_usec;

	while (usec < 0)
	{
		usec += 1000000;
		sec  -= 1;
	}

	if (sec < 0)
		return (0);

	return ((sec * 1000) + ((usec + 500) / 1000));
}

static int ntpd_connect (void)
{
	char *host;
	char *port;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list;
	struct addrinfo *ai_ptr;
	int              status;

	if (sock_descr >= 0)
		return (sock_descr);

	host = ntpd_host;
	if (host == NULL)
		host = NTPD_DEFAULT_HOST;

	port = ntpd_port;
	if (port == NULL)
		port = NTPD_DEFAULT_PORT;

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = AI_ADDRCONFIG;
	ai_hints.ai_family   = PF_UNSPEC;
	ai_hints.ai_socktype = SOCK_DGRAM;
	ai_hints.ai_protocol = IPPROTO_UDP;

	if ((status = getaddrinfo (host, port, &ai_hints, &ai_list)) != 0)
	{
		syslog (LOG_ERR, "ntpd plugin: getaddrinfo (%s, %s): %s",
				host, port,
				status == EAI_SYSTEM ? strerror (errno) : gai_strerror (status));
		return (-1);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		/* create our socket descriptor */
		if ((sock_descr = socket (ai_ptr->ai_family,
						ai_ptr->ai_socktype,
						ai_ptr->ai_protocol)) < 0)
			continue;

		/* connect to the ntpd */
		if (connect (sock_descr, ai_ptr->ai_addr, ai_ptr->ai_addrlen))
		{
			close (sock_descr);
			sock_descr = -1;
			continue;
		}

		break;
	}

	freeaddrinfo (ai_list);

	if (sock_descr < 0)
		syslog (LOG_ERR, "ntpd plugin: Unable to connect to server.");

	return (sock_descr);
}

/* For a description of the arguments see `ntpd_do_query' below. */
static int ntpd_receive_response (int req_code, int *res_items, int *res_size,
		char **res_data, int res_item_size)
{
	int              sd;
	struct pollfd    poll_s;
	struct resp_pkt  res;
	int              status;
	int              done;
	int              i;

	char            *items;
	size_t           items_num;

	struct timeval   time_end;
	struct timeval   time_now;
	int              timeout;

	int              pkt_item_num;        /* items in this packet */
	int              pkt_item_len;        /* size of the items in this packet */
	int              pkt_sequence;
	char             pkt_recvd[MAXSEQ+1]; /* sequence numbers that have been received */
	int              pkt_recvd_num;       /* number of packets that have been received */
	int              pkt_lastseq;         /* the last sequence number */
	ssize_t          pkt_padding;         /* Padding in this packet */

	if ((sd = ntpd_connect ()) < 0)
		return (-1);

	items = NULL;
	items_num = 0;

	memset (pkt_recvd, '\0', sizeof (pkt_recvd));
	pkt_recvd_num = 0;
	pkt_lastseq   = -1;

	*res_items = 0;
	*res_size  = 0;
	*res_data  = NULL;

	if (gettimeofday (&time_end, NULL) < 0)
	{
		syslog (LOG_ERR, "ntpd plugin: gettimeofday failed: %s",
				strerror (errno));
		return (-1);
	}
	time_end.tv_sec++; /* wait for a most one second */

	done = 0;
	while (done == 0)
	{
		if (gettimeofday (&time_now, NULL) < 0)
		{
			syslog (LOG_ERR, "ntpd plugin: gettimeofday failed: %s",
					strerror (errno));
			return (-1);
		}

		/* timeout reached */
		if ((timeout = timeval_sub (&time_end, &time_now)) == 0)
			break;

		poll_s.fd      = sd;
		poll_s.events  = POLLIN | POLLPRI;
		poll_s.revents = 0;
		
		status = poll (&poll_s, 1, timeout);

		if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (status < 0)
		{
			syslog (LOG_ERR, "ntpd plugin: poll failed: %s",
					strerror (errno));
			return (-1);
		}

		if (status == 0) /* timeout */
			break;

		memset ((void *) &res, '\0', sizeof (res));
		status = recv (sd, (void *) &res, sizeof (res), 0 /* no flags */);

		if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (status < 0)
			return (-1);

		/* 
		 * Do some sanity checks first
		 */
		if (status < RESP_HEADER_SIZE)
		{
			syslog (LOG_WARNING, "ntpd plugin: Short (%i bytes) packet received",
					(int) status);
			continue;
		}
		if (INFO_MODE (res.rm_vn_mode) != MODE_PRIVATE)
		{
			syslog (LOG_NOTICE, "ntpd plugin: Packet received with mode %i",
					INFO_MODE (res.rm_vn_mode));
			continue;
		}
		if (INFO_IS_AUTH (res.auth_seq))
		{
			syslog (LOG_NOTICE, "ntpd plugin: Encrypted packet received");
			continue;
		}
		if (!ISRESPONSE (res.rm_vn_mode))
		{
			syslog (LOG_NOTICE, "ntpd plugin: Received request packet, "
					"wanted response");
			continue;
		}
		if (INFO_MBZ (res.mbz_itemsize))
		{
			syslog (LOG_WARNING, "ntpd plugin: Received packet with nonzero "
					"MBZ field!");
			continue;
		}
		if (res.implementation != req_code)
		{
			syslog (LOG_WARNING, "ntpd plugin: Asked for request of type %i, "
					"got %i", (int) req_code, (int) res.implementation);
			continue;
		}

		/* Check for error code */
		if (INFO_ERR (res.err_nitems) != 0)
		{
			syslog (LOG_ERR, "ntpd plugin: Received error code %i",
					(int) INFO_ERR(res.err_nitems));
			return ((int) INFO_ERR (res.err_nitems));
		}

		/* extract number of items in this packet and the size of these items */
		pkt_item_num = INFO_NITEMS (res.err_nitems);
		pkt_item_len = INFO_ITEMSIZE (res.mbz_itemsize);

		/* Check if the reported items fit in the packet */
		if ((pkt_item_num * pkt_item_len) > (status - RESP_HEADER_SIZE))
		{
			syslog (LOG_ERR, "ntpd plugin: %i items * %i bytes > "
					"%i bytes - %i bytes header",
					(int) pkt_item_num, (int) pkt_item_len,
					(int) status, (int) RESP_HEADER_SIZE);
			continue;
		}

		/* If this is the first packet (time wise, not sequence wise),
		 * set `res_size'. If it's not the first packet check if the
		 * items have the same size. Discard invalid packets. */
		if (items_num == 0) /* first packet */
		{
			*res_size = pkt_item_len;
		}
		else if (*res_size != pkt_item_len)
		{
			syslog (LOG_ERR, "Item sizes differ.");
			continue;
		}

		/* Calculate the padding. No idea why there might be any padding.. */
		pkt_padding = 0;
		if (res_item_size > pkt_item_len)
			pkt_padding = res_item_size - pkt_item_len;

		/* Extract the sequence number */
		pkt_sequence = INFO_SEQ (res.auth_seq);
		if ((pkt_sequence < 0) || (pkt_sequence > MAXSEQ))
		{
			syslog (LOG_ERR, "ntpd plugin: Received packet with sequence %i",
					pkt_sequence);
			continue;
		}

		/* Check if this sequence has been received before. If so, discard it. */
		if (pkt_recvd[pkt_sequence] != '\0')
		{
			syslog (LOG_NOTICE, "ntpd plugin: Sequence %i received twice",
					pkt_sequence);
			continue;
		}

		/* If `pkt_lastseq != -1' another packet without `more bit' has
		 * been received. */
		if (!ISMORE (res.rm_vn_mode))
		{
			if (pkt_lastseq != -1)
			{
				syslog (LOG_ERR, "ntpd plugin: Two packets which both "
						"claim to be the last one in the "
						"sequence have been received.");
				continue;
			}
			pkt_lastseq = pkt_sequence;
		}

		/*
		 * Enough with the checks. Copy the data now.
		 * We start by allocating some more memory.
		 */
		items = realloc ((void *) *res_data,
				(items_num + pkt_item_num) * res_item_size);
		if (items == NULL)
		{
			items = *res_data;
			syslog (LOG_ERR, "ntpd plugin: realloc failed.");
			continue;
		}
		*res_data = items;

		for (i = 0; i < pkt_item_num; i++)
		{
			void *dst = (void *) (*res_data + ((*res_items) * res_item_size));
			void *src = (void *) (((char *) res.data) + (i * pkt_item_len));

			/* Set the padding to zeros */
			if (pkt_padding != 0)
				memset (dst, '\0', res_item_size);
			memcpy (dst, src, (size_t) pkt_item_len);

			(*res_items)++;
		}

		pkt_recvd[pkt_sequence] = (char) 1;
		pkt_recvd_num++;

		if ((pkt_recvd_num - 1) == pkt_lastseq)
			done = 1;
	} /* while (done == 0) */

	return (0);
}

/* For a description of the arguments see `ntpd_do_query' below. */
static int ntpd_send_request (int req_code, int req_items, int req_size, char *req_data)
{
	int             sd;
	struct req_pkt  req;
	size_t          req_data_len;
	int             status;

	assert (req_items >= 0);
	assert (req_size  >= 0);

	if ((sd = ntpd_connect ()) < 0)
		return (-1);

	memset ((void *) &req, '\0', sizeof (req));
	req.rm_vn_mode = RM_VN_MODE(0, 0, 0);
	req.auth_seq   = AUTH_SEQ (0, 0);
	req.implementation = IMPL_XNTPD;
	req.request = (unsigned char) req_code;

	req_data_len = (size_t) (req_items * req_size);

	assert (((req_data != NULL) && (req_data_len > 0))
			|| ((req_data == NULL) && (req_items == 0) && (req_size == 0)));

	req.err_nitems   = ERR_NITEMS (0, req_items);
	req.mbz_itemsize = MBZ_ITEMSIZE (req_size);
	
	if (req_data != NULL)
		memcpy ((void *) req.data, (const void *) req_data, req_data_len);

	status = swrite (sd, (const char *) &req, REQ_LEN_NOMAC);
	if (status < 0)
		return (status);

	return (0);
}

/*
 * ntpd_do_query:
 *
 * req_code:      Type of request packet
 * req_items:     Numver of items in the request
 * req_size:      Size of one item in the request
 * req_data:      Data of the request packet
 * res_items:     Pointer to where the number returned items will be stored.
 * res_size:      Pointer to where the size of one returned item will be stored.
 * res_data:      This is where a pointer to the (allocated) data will be stored.
 * res_item_size: Size of one returned item. (used to calculate padding)
 *
 * returns:       zero upon success, non-zero otherwise.
 */
static int ntpd_do_query (int req_code, int req_items, int req_size, char *req_data,
		int *res_items, int *res_size, char **res_data, int res_item_size)
{
	int status;

	status = ntpd_send_request (req_code, req_items, req_size, req_data);
	if (status != 0)
		return (status);

	status = ntpd_receive_response (req_code, res_items, res_size, res_data,
			res_item_size);
	return (status);
}

static void ntpd_read (void)
{
	return;
}
#else
# define ntpd_read NULL
#endif /* NTPD_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, ntpd_init, ntpd_read, ntpd_write);
}

#undef MODULE_NAME
