/**
 * collectd - src/dns.c
 * Copyright (C) 2006,2007  Florian octo Forster
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

#include "utils_dns.h"
#include <pthread.h>
#include <pcap.h>
#include <poll.h>

/*
 * Private data types
 */
struct counter_list_s
{
	unsigned int key;
	unsigned int value;
	struct counter_list_s *next;
};
typedef struct counter_list_s counter_list_t;

/*
 * Private variables
 */
static const char *config_keys[] =
{
	"Interface",
	"IgnoreSource",
	NULL
};
static int config_keys_num = 2;

#define PCAP_SNAPLEN 1460
static char   *pcap_device = NULL;

static counter_t       tr_queries;
static counter_t       tr_responses;
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

/*
 * Private functions
 */
static counter_list_t *counter_list_search (counter_list_t **list, unsigned int key)
{
	counter_list_t *entry;

	DEBUG ("counter_list_search (list = %p, key = %u)",
			(void *) *list, key);

	for (entry = *list; entry != NULL; entry = entry->next)
		if (entry->key == key)
			break;

	DEBUG ("return (%p)", (void *) entry);
	return (entry);
}

static counter_list_t *counter_list_create (counter_list_t **list,
		unsigned int key, unsigned int value)
{
	counter_list_t *entry;

	DEBUG ("counter_list_create (list = %p, key = %u, value = %u)",
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

	DEBUG ("return (%p)", (void *) entry);
	return (entry);
}

static void counter_list_add (counter_list_t **list,
		unsigned int key, unsigned int increment)
{
	counter_list_t *entry;

	DEBUG ("counter_list_add (list = %p, key = %u, increment = %u)",
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
	DEBUG ("return ()");
}

static int dns_config (const char *key, const char *value)
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

static void *dns_child_loop (void __attribute__((unused)) *dummy)
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
	DEBUG ("Creating PCAP object..");
	pcap_obj = pcap_open_live ((pcap_device != NULL) ? pcap_device : "any",
			PCAP_SNAPLEN,
			0 /* Not promiscuous */,
			interval_g,
			pcap_error);
	if (pcap_obj == NULL)
	{
		ERROR ("dns plugin: Opening interface `%s' "
				"failed: %s",
				(pcap_device != NULL) ? pcap_device : "any",
				pcap_error);
		return (NULL);
	}

	memset (&fp, 0, sizeof (fp));
	if (pcap_compile (pcap_obj, &fp, "udp port 53", 1, 0) < 0)
	{
		ERROR ("dns plugin: pcap_compile failed");
		return (NULL);
	}
	if (pcap_setfilter (pcap_obj, &fp) < 0)
	{
		ERROR ("dns plugin: pcap_setfilter failed");
		return (NULL);
	}

	DEBUG ("PCAP object created.");

	dnstop_set_pcap_obj (pcap_obj);
	dnstop_set_callback (dns_child_callback);

	status = pcap_loop (pcap_obj,
			-1 /* loop forever */,
			handle_pcap /* callback */,
			NULL /* Whatever this means.. */);
	if (status < 0)
		ERROR ("dns plugin: Listener thread is exiting "
				"abnormally: %s", pcap_geterr (pcap_obj));

	DEBUG ("child is exiting");

	pcap_close (pcap_obj);
	listen_thread_init = 0;
	pthread_exit (NULL);

	return (NULL);
} /* static void dns_child_loop (void) */

static int dns_init (void)
{
	/* clean up an old thread */
	int status;

	pthread_mutex_lock (&traffic_mutex);
	tr_queries   = 0;
	tr_responses = 0;
	pthread_mutex_unlock (&traffic_mutex);

	if (listen_thread_init != 0)
		return (-1);

	status = pthread_create (&listen_thread, NULL, dns_child_loop,
			(void *) 0);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("dns plugin: pthread_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	listen_thread_init = 1;

	return (0);
} /* int dns_init */

static void submit_counter (const char *type, const char *type_instance,
		counter_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "dns", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void submit_counter */

static void submit_octets (counter_t queries, counter_t responses)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = queries;
	values[1].counter = responses;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "dns", sizeof (vl.plugin));
	sstrncpy (vl.type, "dns_octets", sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void submit_counter */

static int dns_read (void)
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

	if ((values[0] != 0) || (values[1] != 0))
		submit_octets (values[0], values[1]);

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
		DEBUG ("qtype = %u; counter = %u;", keys[i], values[i]);
		submit_counter ("dns_qtype", qtype_str (keys[i]), values[i]);
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
		DEBUG ("opcode = %u; counter = %u;", keys[i], values[i]);
		submit_counter ("dns_opcode", opcode_str (keys[i]), values[i]);
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
		DEBUG ("rcode = %u; counter = %u;", keys[i], values[i]);
		submit_counter ("dns_rcode", rcode_str (keys[i]), values[i]);
	}

	return (0);
} /* int dns_read */

void module_register (void)
{
	plugin_register_config ("dns", dns_config, config_keys, config_keys_num);
	plugin_register_init ("dns", dns_init);
	plugin_register_read ("dns", dns_read);
} /* void module_register */
