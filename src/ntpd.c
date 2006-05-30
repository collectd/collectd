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
#include "utils_debug.h"

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
	"DS:ms:GAUGE:"COLLECTD_HEARTBEAT":-1000000:1000000",
	NULL
};
static int time_offset_ds_num = 1;

static char *frequency_offset_file = "ntpd/frequency_offset-%s.rrd";
static char *frequency_offset_ds_def[] =
{
	"DS:ppm:GAUGE:"COLLECTD_HEARTBEAT":-1000000:1000000",
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
#define FP_FRAC 65536.0

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

/* negate a long float type */
#define M_NEG(v_i, v_f) \
	do { \
		if ((v_f) == 0) \
		(v_i) = -((uint32_t)(v_i)); \
		else { \
			(v_f) = -((uint32_t)(v_f)); \
			(v_i) = ~(v_i); \
		} \
	} while(0)
/* l_fp to double */
#define M_LFPTOD(r_i, r_uf, d) \
	do { \
		register int32_t  i; \
		register uint32_t f; \
		\
		i = (r_i); \
		f = (r_uf); \
		if (i < 0) { \
			M_NEG(i, f); \
			(d) = -((double) i + ((double) f) / 4294967296.0); \
		} else { \
			(d) = (double) i + ((double) f) / 4294967296.0; \
		} \
	} while (0)

#define REQ_PEER_LIST_SUM 1
struct info_peer_summary
{
	uint32_t dstadr;         /* local address (zero for undetermined) */
	uint32_t srcadr;         /* source address */
	uint16_t srcport;        /* source port */
	uint8_t stratum;         /* stratum of peer */
	int8_t hpoll;            /* host polling interval */
	int8_t ppoll;            /* peer polling interval */
	uint8_t reach;           /* reachability register */
	uint8_t flags;           /* flags, from above */
	uint8_t hmode;           /* peer mode */
	int32_t  delay;          /* peer.estdelay; s_fp */
	int32_t  offset_int;     /* peer.estoffset; integral part */
	int32_t  offset_frc;     /* peer.estoffset; fractional part */
	uint32_t dispersion;     /* peer.estdisp; u_fp */
	uint32_t v6_flag;        /* is this v6 or not */
	uint32_t unused1;        /* (unused) padding for dstadr6 */
	struct in6_addr dstadr6; /* local address (v6) */
	struct in6_addr srcadr6; /* source address (v6) */
};

#define REQ_SYS_INFO 4
struct info_sys
{
	uint32_t peer;           /* system peer address (v4) */
	uint8_t  peer_mode;      /* mode we are syncing to peer in */
	uint8_t  leap;           /* system leap bits */
	uint8_t  stratum;        /* our stratum */
	int8_t   precision;      /* local clock precision */
	int32_t  rootdelay;      /* distance from sync source */
	uint32_t rootdispersion; /* dispersion from sync source */
	uint32_t refid;          /* reference ID of sync source */
	uint64_t reftime;        /* system reference time */
	uint32_t poll;           /* system poll interval */
	uint8_t  flags;          /* system flags */
	uint8_t  unused1;        /* unused */
	uint8_t  unused2;        /* unused */
	uint8_t  unused3;        /* unused */
	int32_t  bdelay;         /* default broadcast offset */
	int32_t  frequency;      /* frequency residual (scaled ppm)  */
	uint64_t authdelay;      /* default authentication delay */
	uint32_t stability;      /* clock stability (scaled ppm) */
	int32_t  v6_flag;        /* is this v6 or not */
	int32_t  unused4;        /* unused, padding for peer6 */
	struct in6_addr peer6;   /* system peer address (v6) */
};

#define REQ_GET_KERNEL 38
struct info_kernel
{
	int32_t  offset;
	int32_t  freq;
	int32_t  maxerror;
	int32_t  esterror;
	uint16_t status;
	uint16_t shift;
	int32_t  constant;
	int32_t  precision;
	int32_t  tolerance;
	/* pps stuff */
	int32_t  ppsfreq;
	int32_t  jitter;
	int32_t  stabil;
	int32_t  jitcnt;
	int32_t  calcnt;
	int32_t  errcnt;
	int32_t  stbcnt;
};
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * End of the copied stuff..                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static void ntpd_init (void)
{
	return;
}

static void ntpd_write_time_offset (char *host, char *inst, char *val)
{
	char buf[256];
	int  status;

	status = snprintf (buf, 256, time_offset_file, inst);
	if ((status < 1) || (status >= 256))
		return;

	rrd_update_file (host, buf, val,
			time_offset_ds_def, time_offset_ds_num);
}

static void ntpd_write_frequency_offset (char *host, char *inst, char *val)
{
	char buf[256];
	int  status;

	status = snprintf (buf, 256, frequency_offset_file, inst);
	if ((status < 1) || (status >= 256))
		return;

	rrd_update_file (host, buf, val,
			frequency_offset_ds_def, frequency_offset_ds_num);
}

#if NTPD_HAVE_READ
static void ntpd_submit (char *type, char *inst, double value)
{
	char buf[256];

	if (snprintf (buf, 256, "%u:%.8f", (unsigned int) curtime, value) >= 256)
		return;

	DBG ("type = %s; inst = %s; value = %s;",
			type, inst, buf);

	plugin_submit (type, inst, buf);
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

	DBG ("Opening a new socket");

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
		DBG ("getaddrinfo (%s, %s): %s",
				host, port,
				status == EAI_SYSTEM ? strerror (errno) : gai_strerror (status));
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
	{
		DBG ("Unable to connect to server.");
		syslog (LOG_ERR, "ntpd plugin: Unable to connect to server.");
	}

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
		
		DBG ("Polling for %ims", timeout);
		status = poll (&poll_s, 1, timeout);

		if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (status < 0)
		{
			DBG ("poll failed: %s", strerror (errno));
			syslog (LOG_ERR, "ntpd plugin: poll failed: %s",
					strerror (errno));
			return (-1);
		}

		if (status == 0) /* timeout */
		{
			DBG ("timeout reached.");
			break;
		}

		memset ((void *) &res, '\0', sizeof (res));
		status = recv (sd, (void *) &res, sizeof (res), 0 /* no flags */);

		if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
			continue;

		if (status < 0)
		{
			DBG ("recv(2) failed: %s", strerror (errno));
			return (-1);
		}

		DBG ("recv'd %i bytes", status);

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
		if (res.implementation != IMPL_XNTPD)
		{
			syslog (LOG_WARNING, "ntpd plugin: Asked for request of type %i, "
					"got %i", (int) IMPL_XNTPD, (int) res.implementation);
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
		DBG ("pkt_item_num = %i; pkt_item_len = %i;",
				pkt_item_num, pkt_item_len);

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
			DBG ("*res_size = %i", pkt_item_len);
			*res_size = pkt_item_len;
		}
		else if (*res_size != pkt_item_len)
		{
			DBG ("Error: *res_size = %i; pkt_item_len = %i;",
					*res_size, pkt_item_len);
			syslog (LOG_ERR, "Item sizes differ.");
			continue;
		}

		/* Calculate the padding. No idea why there might be any padding.. */
		pkt_padding = 0;
		if (res_item_size > pkt_item_len)
			pkt_padding = res_item_size - pkt_item_len;
		DBG ("res_item_size = %i; pkt_padding = %i;",
				res_item_size, pkt_padding);

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
			DBG ("Last sequence = %i;", pkt_lastseq);
		}

		/*
		 * Enough with the checks. Copy the data now.
		 * We start by allocating some more memory.
		 */
		DBG ("realloc (%p, %i)", (void *) *res_data,
				(items_num + pkt_item_num) * res_item_size);
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

	DBG ("req_items = %i; req_size = %i; req_data = %p;",
			req_items, req_size, (void *) req_data);

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

static double ntpd_read_fp (int32_t val_int)
{
	double val_double;

	val_int = ntohl (val_int);
	val_double = ((double) val_int) / FP_FRAC;

	return (val_double);
}

static void ntpd_read (void)
{
	struct info_kernel *ik;
	int                 ik_num;
	int                 ik_size;

	struct info_peer_summary *ps;
	int                       ps_num;
	int                       ps_size;

	int status;
	int i;

	ik      = NULL;
	ik_num  = 0;
	ik_size = 0;

	status = ntpd_do_query (REQ_GET_KERNEL,
			0, 0, NULL, /* request data */
			&ik_num, &ik_size, (char **) ((void *) &ik), /* response data */
			sizeof (struct info_kernel));

	if (status != 0)
	{
		DBG ("ntpd_do_query failed with status %i", status);
		return;
	}
	if ((ik == NULL) || (ik_num == 0) || (ik_size == 0))
	{
		DBG ("ntpd_do_query returned: ik = %p; ik_num = %i; ik_size = %i;",
				(void *) ik, ik_num, ik_size);
		return;
	}

	/* kerninfo -> estimated error */

	DBG ("info_kernel:\n"
			"  pll offset    = %.8f\n"
			"  pll frequency = %.8f\n" /* drift compensation */
			"  est error     = %.8f\n",
			ntpd_read_fp (ik->offset),
			ntpd_read_fp (ik->freq),
			ntpd_read_fp (ik->esterror));

	ntpd_submit ("ntpd_frequency_offset", "loop",  ntpd_read_fp (ik->freq));
	ntpd_submit ("ntpd_time_offset",      "loop",  ntpd_read_fp (ik->offset));
	ntpd_submit ("ntpd_time_offset",      "error", ntpd_read_fp (ik->esterror));

	free (ik);
	ik = NULL;

	status = ntpd_do_query (REQ_PEER_LIST_SUM,
			0, 0, NULL, /* request data */
			&ps_num, &ps_size, (char **) ((void *) &ps), /* response data */
			sizeof (struct info_peer_summary));
	if (status != 0)
	{
		DBG ("ntpd_do_query failed with status %i", status);
		return;
	}
	if ((ps == NULL) || (ps_num == 0) || (ps_size == 0))
	{
		DBG ("ntpd_do_query returned: ps = %p; ps_num = %i; ps_size = %i;",
				(void *) ps, ps_num, ps_size);
		return;
	}

	for (i = 0; i < ps_num; i++)
	{
		struct info_peer_summary *ptr;
		double offset;
		ptr = ps + i;

		if (((ntohl (ptr->dstadr) & 0x7F000000) == 0x7F000000) || (ptr->dstadr == 0))
			continue;

		/* Convert the `long floating point' offset value to double */
		M_LFPTOD (ntohl (ptr->offset_int), ntohl (ptr->offset_frc), offset);

		DBG ("peer %i:\n"
				"  srcadr     = 0x%08x\n"
				"  delay      = %f\n"
				"  offset_int = %i\n"
				"  offset_frc = %i\n"
				"  offset     = %f\n"
				"  dispersion = %f\n",
				i,
				ntohl (ptr->srcadr),
				ntpd_read_fp (ptr->delay),
				ntohl (ptr->offset_int),
				ntohl (ptr->offset_frc),
				offset,
				ntpd_read_fp (ptr->dispersion));
	}

	free (ps);
	ps = NULL;

	return;
}
#else
# define ntpd_read NULL
#endif /* NTPD_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, ntpd_init, ntpd_read, NULL);
	plugin_register ("ntpd_time_offset", NULL, NULL, ntpd_write_time_offset);
	plugin_register ("ntpd_frequency_offset", NULL, NULL, ntpd_write_frequency_offset);
}

#undef MODULE_NAME
