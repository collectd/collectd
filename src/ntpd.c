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

#include "ntp_request.h"

#define MODULE_NAME "ntpd"

#if HAVE_SYS_SOCKET_H
# define NTPD_HAVE_READ 1
#else
# define NTPD_HAVE_READ 0
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

static void ntpd_init (void)
{
	return;
}

static void ntpd_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, ntpd_file, val, ds_def, ds_num);
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

static void ntpd_connect (void)
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
	struct resp_pkt  res;
	ssize_t          status;
	int              done;

	char            *items;
	size_t           items_num;

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

	done = 0;
	while (done == 0)
	{
		/* TODO calculate time */
		/* TODO poll(2) */

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
		if (INFO_ERR (res.err_nitems) != INFO_OKAY)
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

	req.err_nitems   = ERR_NITEMS (0, qitems);
	req.mbz_itemsize = MBZ_ITEMSIZE (qsize);
	
	if (req_data != NULL)
		memcpy ((void *) req.data, (const void *) req_data, req_data_len);

	status = swrite (fd, (const char *) &req, REQ_LEN_NOMAC);
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
	static sd = -1;
	struct req_pkt req;

	ntpd_connect (&sd);
	if (sd < 0)
		return;

	memset ((void *) &req, '\0', sizeof (req));
	req.rm_vn_mode = RM_VN_MODE (0, 0, 0); /* response, more, version, mode */
	req.implementation = IMPL_XNTPD;
}
#else
# define ntpd_read NULL
#endif /* NTPD_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, ntpd_init, ntpd_read, ntpd_write);
}

#undef MODULE_NAME
