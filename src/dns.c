/**
 * collectd - src/dns.c
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
#include "utils_dns.h"

#define MODULE_NAME "dns"

#if HAVE_LIBPCAP && HAVE_LIBPTHREAD
# include <pthread.h>
# include <pcap.h>
# include <poll.h>
# define DNS_HAVE_READ 1
#else
# define DNS_HAVE_READ 0
#endif

/*
 * Private data types
 */
#if DNS_HAVE_READ
struct counter_list_s
{
	unsigned int key;
	unsigned int value;
	struct counter_list_s *next;
};
typedef struct counter_list_s counter_list_t;
#endif

/*
 * Private variables
 */
static char *traffic_file   = "dns/dns_traffic.rrd";
static char *qtype_file   = "dns/qtype-%s.rrd";
static char *opcode_file  = "dns/opcode-%s.rrd";
static char *rcode_file   = "dns/rcode-%s.rrd";

static char *traffic_ds_def[] =
{
	/* Limit to 1GBit/s */
	"DS:queries:COUNTER:"COLLECTD_HEARTBEAT":0:125000000",
	"DS:responses:COUNTER:"COLLECTD_HEARTBEAT":0:125000000",
	NULL
};
static int traffic_ds_num = 2;

static char *qtype_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int qtype_ds_num = 1;

static char *opcode_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int opcode_ds_num = 1;

static char *rcode_ds_def[] =
{
	"DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int rcode_ds_num = 1;

#if DNS_HAVE_READ
static char *config_keys[] =
{
	"Interface",
	"IgnoreSource",
	NULL
};
static int config_keys_num = 2;

#define PCAP_SNAPLEN 1460
static char   *pcap_device = NULL;

static unsigned int    tr_queries;
static unsigned int    tr_responses;
static counter_list_t *qtype_list;
static counter_list_t *opcode_list;
static counter_list_t *rcode_list;

static pthread_t       listen_thread;
static int             listen_thread_init = 0;
/* The `traffic' mutex if for `tr_queries' and `tr_responses' */
static pthread_mutex_t traffic_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t qtype_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t opcode_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t rcode_mutex   = PTHREAD_MUTEX_INITIALIZER;
#endif /* DNS_HAVE_READ */

/*
 * Private functions
 */
#if DNS_HAVE_READ
static counter_list_t *counter_list_search (counter_list_t **list, unsigned int key)
{
	counter_list_t *entry;

	DBG ("counter_list_search (list = %p, key = %u)",
			(void *) *list, key);

	for (entry = *list; entry != NULL; entry = entry->next)
		if (entry->key == key)
			break;

	DBG ("return (%p)", (void *) entry);
	return (entry);
}

static counter_list_t *counter_list_create (counter_list_t **list,
		unsigned int key, unsigned int value)
{
	counter_list_t *entry;

	DBG ("counter_list_create (list = %p, key = %u, value = %u)",
			(void *) *list, key, value);

	entry = (counter_list_t *) malloc (sizeof (counter_list_t));
	if (entry == NULL)
		return (NULL);

	memset (entry, 0, sizeof (counter_list_t));
	entry->key = key;
	entry->value = value;

	if (*list == NULL)
	{
		*list = entry;
	}
	else
	{
		counter_list_t *last;

		last = *list;
		while (last->next != NULL)
			last = last->next;

		last->next = entry;
	}

	DBG ("return (%p)", (void *) entry);
	return (entry);
}

static void counter_list_add (counter_list_t **list,
		unsigned int key, unsigned int increment)
{
	counter_list_t *entry;

	DBG ("counter_list_add (list = %p, key = %u, increment = %u)",
			(void *) *list, key, increment);

	entry = counter_list_search (list, key);

	if (entry != NULL)
	{
		entry->value += increment;
	}
	else
	{
		counter_list_create (list, key, increment);
	}
	DBG ("return ()");
}

static int dns_config (char *key, char *value)
{
	if (strcasecmp (key, "Interface") == 0)
	{
		if (pcap_device != NULL)
			free (pcap_device);
		if ((pcap_device = strdup (value)) == NULL)
			return (1);
	}
	else if (strcasecmp (key, "IgnoreSource") == 0)
	{
		if (value != NULL)
			ignore_list_add_name (value);
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void dns_child_callback (const rfc1035_header_t *dns)
{
	if (dns->qr == 0)
	{
		/* This is a query */
		pthread_mutex_lock (&traffic_mutex);
		tr_queries += dns->length;
		pthread_mutex_unlock (&traffic_mutex);

		pthread_mutex_lock (&qtype_mutex);
		counter_list_add (&qtype_list,  dns->qtype,  1);
		pthread_mutex_unlock (&qtype_mutex);
	}
	else
	{
		/* This is a reply */
		pthread_mutex_lock (&traffic_mutex);
		tr_responses += dns->length;
		pthread_mutex_unlock (&traffic_mutex);

		pthread_mutex_lock (&rcode_mutex);
		counter_list_add (&rcode_list,  dns->rcode,  1);
		pthread_mutex_unlock (&rcode_mutex);
	}

	/* FIXME: Are queries, replies or both interesting? */
	pthread_mutex_lock (&opcode_mutex);
	counter_list_add (&opcode_list, dns->opcode, 1);
	pthread_mutex_unlock (&opcode_mutex);
}

static void *dns_child_loop (void *dummy)
{
	pcap_t *pcap_obj;
	char    pcap_error[PCAP_ERRBUF_SIZE];
	struct  bpf_program fp;

	int status;

	/* Don't block any signals */
	{
		sigset_t sigmask;
		sigemptyset (&sigmask);
		pthread_sigmask (SIG_SETMASK, &sigmask, NULL);
	}

	/* Passing `pcap_device == NULL' is okay and the same as passign "any" */
	DBG ("Creating PCAP object..");
	pcap_obj = pcap_open_live (pcap_device,
			PCAP_SNAPLEN,
			0 /* Not promiscuous */,
			atoi (COLLECTD_STEP),
			pcap_error);
	if (pcap_obj == NULL)
	{
		syslog (LOG_ERR, "dns plugin: Opening interface `%s' "
				"failed: %s",
				(pcap_device != NULL) ? pcap_device : "any",
				pcap_error);
		return (NULL);
	}

	memset (&fp, 0, sizeof (fp));
	if (pcap_compile (pcap_obj, &fp, "udp port 53", 1, 0) < 0)
	{
		DBG ("pcap_compile failed");
		syslog (LOG_ERR, "dns plugin: pcap_compile failed");
		return (NULL);
	}
	if (pcap_setfilter (pcap_obj, &fp) < 0)
	{
		DBG ("pcap_setfilter failed");
		syslog (LOG_ERR, "dns plugin: pcap_setfilter failed");
		return (NULL);
	}

	DBG ("PCAP object created.");

	dnstop_set_pcap_obj (pcap_obj);
	dnstop_set_callback (dns_child_callback);

	status = pcap_loop (pcap_obj,
			-1 /* loop forever */,
			handle_pcap /* callback */,
			NULL /* Whatever this means.. */);
	if (status < 0)
		syslog (LOG_ERR, "dns plugin: Listener thread is exiting "
				"abnormally: %s", pcap_geterr (pcap_obj));

	DBG ("child is exiting");

	pcap_close (pcap_obj);
	listen_thread_init = 0;
	pthread_exit (NULL);

	return (NULL);
} /* static void dns_child_loop (void) */
#endif /* DNS_HAVE_READ */

static void dns_init (void)
{
#if DNS_HAVE_READ
	/* clean up an old thread */
	int status;

	pthread_mutex_lock (&traffic_mutex);
	tr_queries   = 0;
	tr_responses = 0;
	pthread_mutex_unlock (&traffic_mutex);

	if (listen_thread_init != 0)
		return;

	status = pthread_create (&listen_thread, NULL, dns_child_loop,
			(void *) 0);
	if (status != 0)
	{
		syslog (LOG_ERR, "dns plugin: pthread_create failed: %s",
				strerror (status));
		return;
	}

	listen_thread_init = 1;
#endif /* DNS_HAVE_READ */
}

static void traffic_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, traffic_file, val,
			traffic_ds_def, traffic_ds_num);
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

static void rcode_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, rcode_file, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, rcode_ds_def, rcode_ds_num);
}

static void opcode_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, opcode_file, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, opcode_ds_def, opcode_ds_num);
}

#if DNS_HAVE_READ
static void traffic_submit (unsigned int queries, unsigned int replies)
{
	char buffer[64];
	int  status;

	status = snprintf (buffer, 64, "N:%u:%u", queries, replies);
	if ((status < 1) || (status >= 64))
		return;

	plugin_submit ("dns_traffic", "-", buffer);
}

static void qtype_submit (int qtype, unsigned int counter)
{
	char inst[32];
	char buffer[32];
	int  status;

	strncpy (inst, qtype_str (qtype), 32);
	inst[31] = '\0';

	status = snprintf (buffer, 32, "N:%u", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("dns_qtype", inst, buffer);
}

static void rcode_submit (int rcode, unsigned int counter)
{
	char inst[32];
	char buffer[32];
	int  status;

	strncpy (inst, rcode_str (rcode), 32);
	inst[31] = '\0';

	status = snprintf (buffer, 32, "N:%u", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("dns_rcode", inst, buffer);
}

static void opcode_submit (int opcode, unsigned int counter)
{
	char inst[32];
	char buffer[32];
	int  status;

	strncpy (inst, opcode_str (opcode), 32);
	inst[31] = '\0';

	status = snprintf (buffer, 32, "N:%u", counter);
	if ((status < 1) || (status >= 32))
		return;

	plugin_submit ("dns_opcode", inst, buffer);
}

static void dns_read (void)
{
	unsigned int keys[T_MAX];
	unsigned int values[T_MAX];
	int len;
	int i;

	counter_list_t *ptr;

	pthread_mutex_lock (&traffic_mutex);
	values[0] = tr_queries;
	values[1] = tr_responses;
	pthread_mutex_unlock (&traffic_mutex);
	traffic_submit (values[0], values[1]);

	pthread_mutex_lock (&qtype_mutex);
	for (ptr = qtype_list, len = 0;
		       	(ptr != NULL) && (len < T_MAX);
		       	ptr = ptr->next, len++)
	{
		keys[len]   = ptr->key;
		values[len] = ptr->value;
	}
	pthread_mutex_unlock (&qtype_mutex);

	for (i = 0; i < len; i++)
	{
		DBG ("qtype = %u; counter = %u;", keys[i], values[i]);
		qtype_submit (keys[i], values[i]);
	}

	pthread_mutex_lock (&opcode_mutex);
	for (ptr = opcode_list, len = 0;
		       	(ptr != NULL) && (len < T_MAX);
		       	ptr = ptr->next, len++)
	{
		keys[len]   = ptr->key;
		values[len] = ptr->value;
	}
	pthread_mutex_unlock (&opcode_mutex);

	for (i = 0; i < len; i++)
	{
		DBG ("opcode = %u; counter = %u;", keys[i], values[i]);
		opcode_submit (keys[i], values[i]);
	}

	pthread_mutex_lock (&rcode_mutex);
	for (ptr = rcode_list, len = 0;
		       	(ptr != NULL) && (len < T_MAX);
		       	ptr = ptr->next, len++)
	{
		keys[len]   = ptr->key;
		values[len] = ptr->value;
	}
	pthread_mutex_unlock (&rcode_mutex);

	for (i = 0; i < len; i++)
	{
		DBG ("rcode = %u; counter = %u;", keys[i], values[i]);
		rcode_submit (keys[i], values[i]);
	}
}
#else /* if !DNS_HAVE_READ */
# define dns_read NULL
#endif

void module_register (void)
{
	plugin_register (MODULE_NAME, dns_init, dns_read, NULL);
	plugin_register ("dns_traffic", NULL, NULL, traffic_write);
	plugin_register ("dns_qtype", NULL, NULL, qtype_write);
	plugin_register ("dns_rcode", NULL, NULL, rcode_write);
	plugin_register ("dns_opcode", NULL, NULL, opcode_write);
#if DNS_HAVE_READ
	cf_register (MODULE_NAME, dns_config, config_keys, config_keys_num);
#endif
}

#undef MODULE_NAME
