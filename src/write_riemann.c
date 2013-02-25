/**
 * collectd - src/write_riemann.c
 *
 * Copyright (C) 2012,2013  Pierre-Yves Ritschard
 * Copyright (C) 2013       Florian octo Forster
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 *   Florian octo Forster <octo at collectd.org>
 */

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"
#include "riemann.pb-c.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <inttypes.h>
#include <pthread.h>

#define RIEMANN_HOST		"localhost"
#define RIEMANN_PORT		"5555"

struct riemann_host {
	char			*name;
#define F_CONNECT		 0x01
	uint8_t			 flags;
	pthread_mutex_t		 lock;
	_Bool			 store_rates;
	_Bool			 always_append_ds;
	char			*node;
	char			*service;
	_Bool			 use_tcp;
	int			 s;

	int			 reference_count;
};

static char	**riemann_tags;
static size_t	  riemann_tags_num;

static int	riemann_send(struct riemann_host *, Msg const *);
static int	riemann_notification(const notification_t *, user_data_t *);
static int	riemann_write(const data_set_t *, const value_list_t *, user_data_t *);
static int	riemann_connect(struct riemann_host *);
static int	riemann_disconnect (struct riemann_host *host);
static void	riemann_free(void *);
static int	riemann_config_node(oconfig_item_t *);
static int	riemann_config(oconfig_item_t *);
void	module_register(void);

static void riemann_event_protobuf_free (Event *event) /* {{{ */
{
	if (event == NULL)
		return;

	sfree (event->state);
	sfree (event->service);
	sfree (event->host);
	sfree (event->description);

	strarray_free (event->tags, event->n_tags);
	event->tags = NULL;
	event->n_tags = 0;

	sfree (event);
} /* }}} void riemann_event_protobuf_free */

static void riemann_msg_protobuf_free (Msg *msg) /* {{{ */
{
	size_t i;

	if (msg == NULL)
		return;

	for (i = 0; i < msg->n_events; i++)
	{
		riemann_event_protobuf_free (msg->events[i]);
		msg->events[i] = NULL;
	}

	sfree (msg->events);
	msg->n_events = 0;

	sfree (msg);
} /* }}} void riemann_msg_protobuf_free */

static int
riemann_send(struct riemann_host *host, Msg const *msg)
{
	u_char *buffer;
	size_t  buffer_len;
	int status;

	pthread_mutex_lock (&host->lock);

	status = riemann_connect (host);
	if (status != 0)
	{
		pthread_mutex_unlock (&host->lock);
		return status;
	}

	buffer_len = msg__get_packed_size(msg);
	if (host->use_tcp)
		buffer_len += 4;

	buffer = malloc (buffer_len);
	if (buffer == NULL) {
		pthread_mutex_unlock (&host->lock);
		ERROR ("write_riemann plugin: malloc failed.");
		return ENOMEM;
	}
	memset (buffer, 0, buffer_len);

	if (host->use_tcp)
	{
		uint32_t length = htonl ((uint32_t) (buffer_len - 4));
		memcpy (buffer, &length, 4);
		msg__pack(msg, buffer + 4);
	}
	else
	{
		msg__pack(msg, buffer);
	}

	status = (int) swrite (host->s, buffer, buffer_len);
	if (status != 0)
	{
		char errbuf[1024];

		riemann_disconnect (host);
		pthread_mutex_unlock (&host->lock);

		ERROR ("write_riemann plugin: Sending to Riemann at %s:%s failed: %s",
				(host->node != NULL) ? host->node : RIEMANN_HOST,
				(host->service != NULL) ? host->service : RIEMANN_PORT,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		sfree (buffer);
		return -1;
	}

	pthread_mutex_unlock (&host->lock);
	sfree (buffer);
	return 0;
}

static int riemann_event_add_tag (Event *event, /* {{{ */
		char const *format, ...)
{
	va_list ap;
	char buffer[1024];
	size_t ret;

	va_start (ap, format);
	ret = vsnprintf (buffer, sizeof (buffer), format, ap);
	if (ret >= sizeof (buffer))
		ret = sizeof (buffer) - 1;
	buffer[ret] = 0;
	va_end (ap);

	return (strarray_add (&event->tags, &event->n_tags, buffer));
} /* }}} int riemann_event_add_tag */

static Msg *riemann_notification_to_protobuf (struct riemann_host *host, /* {{{ */
		notification_t const *n)
{
	Msg *msg;
	Event *event;
	char service_buffer[6 * DATA_MAX_NAME_LEN];
	char const *severity;
	notification_meta_t *meta;
	int i;

	msg = malloc (sizeof (*msg));
	if (msg == NULL)
	{
		ERROR ("write_riemann plugin: malloc failed.");
		return (NULL);
	}
	memset (msg, 0, sizeof (*msg));
	msg__init (msg);

	msg->events = malloc (sizeof (*msg->events));
	if (msg->events == NULL)
	{
		ERROR ("write_riemann plugin: malloc failed.");
		sfree (msg);
		return (NULL);
	}

	event = malloc (sizeof (*event));
	if (event == NULL)
	{
		ERROR ("write_riemann plugin: malloc failed.");
		sfree (msg->events);
		sfree (msg);
		return (NULL);
	}
	memset (event, 0, sizeof (*event));
	event__init (event);

	msg->events[0] = event;
	msg->n_events = 1;

	event->host = strdup (n->host);
	event->time = CDTIME_T_TO_TIME_T (n->time);
	event->has_time = 1;

	switch (n->severity)
	{
		case NOTIF_OKAY:	severity = "okay"; break;
		case NOTIF_WARNING:	severity = "warning"; break;
		case NOTIF_FAILURE:	severity = "failure"; break;
		default:		severity = "unknown";
	}
	event->state = strdup (severity);

	riemann_event_add_tag (event, "notification");
	if (n->plugin[0] != 0)
		riemann_event_add_tag (event, "plugin:%s", n->plugin);
	if (n->plugin_instance[0] != 0)
		riemann_event_add_tag (event, "plugin_instance:%s",
				n->plugin_instance);

	if (n->type[0] != 0)
		riemann_event_add_tag (event, "type:%s", n->type);
	if (n->type_instance[0] != 0)
		riemann_event_add_tag (event, "type_instance:%s",
				n->type_instance);

	for (i = 0; i < riemann_tags_num; i++)
		riemann_event_add_tag (event, "%s", riemann_tags[i]);

	format_name (service_buffer, sizeof (service_buffer),
			/* host = */ "", n->plugin, n->plugin_instance,
			n->type, n->type_instance);
	event->service = strdup (&service_buffer[1]);

	/* Pull in values from threshold */
	for (meta = n->meta; meta != NULL; meta = meta->next)
	{
		if (strcasecmp ("CurrentValue", meta->name) != 0)
			continue;

		event->metric_d = meta->nm_value.nm_double;
		event->has_metric_d = 1;
		break;
	}

	DEBUG ("write_riemann plugin: Successfully created protobuf for notification: "
			"host = \"%s\", service = \"%s\", state = \"%s\"",
			event->host, event->service, event->state);
	return (msg);
} /* }}} Msg *riemann_notification_to_protobuf */

static Event *riemann_value_to_protobuf (struct riemann_host const *host, /* {{{ */
		data_set_t const *ds,
		value_list_t const *vl, size_t index,
		gauge_t const *rates)
{
	Event *event;
	char name_buffer[5 * DATA_MAX_NAME_LEN];
	char service_buffer[6 * DATA_MAX_NAME_LEN];
	int i;

	event = malloc (sizeof (*event));
	if (event == NULL)
	{
		ERROR ("write_riemann plugin: malloc failed.");
		return (NULL);
	}
	memset (event, 0, sizeof (*event));
	event__init (event);

	event->host = strdup (vl->host);
	event->time = CDTIME_T_TO_TIME_T (vl->time);
	event->has_time = 1;
	event->ttl = CDTIME_T_TO_TIME_T (2 * vl->interval);
	event->has_ttl = 1;

	riemann_event_add_tag (event, "plugin:%s", vl->plugin);
	if (vl->plugin_instance[0] != 0)
		riemann_event_add_tag (event, "plugin_instance:%s",
				vl->plugin_instance);

	riemann_event_add_tag (event, "type:%s", vl->type);
	if (vl->type_instance[0] != 0)
		riemann_event_add_tag (event, "type_instance:%s",
				vl->type_instance);

	if ((ds->ds[index].type != DS_TYPE_GAUGE) && (rates != NULL))
	{
		riemann_event_add_tag (event, "ds_type:%s:rate",
				DS_TYPE_TO_STRING(ds->ds[index].type));
	}
	else
	{
		riemann_event_add_tag (event, "ds_type:%s",
				DS_TYPE_TO_STRING(ds->ds[index].type));
	}
	riemann_event_add_tag (event, "ds_name:%s", ds->ds[index].name);
	riemann_event_add_tag (event, "ds_index:%zu", index);

	for (i = 0; i < riemann_tags_num; i++)
		riemann_event_add_tag (event, "%s", riemann_tags[i]);

	if (ds->ds[index].type == DS_TYPE_GAUGE)
	{
		event->has_metric_d = 1;
		event->metric_d = (double) vl->values[index].gauge;
	}
	else if (rates != NULL)
	{
		event->has_metric_d = 1;
		event->metric_d = (double) rates[index];
	}
	else
	{
		event->has_metric_sint64 = 1;
		if (ds->ds[index].type == DS_TYPE_DERIVE)
			event->metric_sint64 = (int64_t) vl->values[index].derive;
		else if (ds->ds[index].type == DS_TYPE_ABSOLUTE)
			event->metric_sint64 = (int64_t) vl->values[index].absolute;
		else
			event->metric_sint64 = (int64_t) vl->values[index].counter;
	}

	format_name (name_buffer, sizeof (name_buffer),
			/* host = */ "", vl->plugin, vl->plugin_instance,
			vl->type, vl->type_instance);
	if (host->always_append_ds || (ds->ds_num > 1))
		ssnprintf (service_buffer, sizeof (service_buffer),
				"%s/%s", &name_buffer[1], ds->ds[index].name);
	else
		sstrncpy (service_buffer, &name_buffer[1],
				sizeof (service_buffer));

	event->service = strdup (service_buffer);

	DEBUG ("write_riemann plugin: Successfully created protobuf for metric: "
			"host = \"%s\", service = \"%s\"",
			event->host, event->service);
	return (event);
} /* }}} Event *riemann_value_to_protobuf */

static Msg *riemann_value_list_to_protobuf (struct riemann_host const *host, /* {{{ */
		data_set_t const *ds,
		value_list_t const *vl)
{
	Msg *msg;
	size_t i;
	gauge_t *rates = NULL;

	/* Initialize the Msg structure. */
	msg = malloc (sizeof (*msg));
	if (msg == NULL)
	{
		ERROR ("write_riemann plugin: malloc failed.");
		return (NULL);
	}
	memset (msg, 0, sizeof (*msg));
	msg__init (msg);

	/* Set up events. First, the list of pointers. */
	msg->n_events = (size_t) vl->values_len;
	msg->events = calloc (msg->n_events, sizeof (*msg->events));
	if (msg->events == NULL)
	{
		ERROR ("write_riemann plugin: calloc failed.");
		riemann_msg_protobuf_free (msg);
		return (NULL);
	}

	if (host->store_rates)
	{
		rates = uc_get_rate (ds, vl);
		if (rates == NULL)
		{
			ERROR ("write_riemann plugin: uc_get_rate failed.");
			riemann_msg_protobuf_free (msg);
			return (NULL);
		}
	}

	for (i = 0; i < msg->n_events; i++)
	{
		msg->events[i] = riemann_value_to_protobuf (host, ds, vl,
				(int) i, rates);
		if (msg->events[i] == NULL)
		{
			riemann_msg_protobuf_free (msg);
			sfree (rates);
			return (NULL);
		}
	}

	sfree (rates);
	return (msg);
} /* }}} Msg *riemann_value_list_to_protobuf */

static int
riemann_notification(const notification_t *n, user_data_t *ud)
{
	int			 status;
	struct riemann_host	*host = ud->data;
	Msg			*msg;

	msg = riemann_notification_to_protobuf (host, n);
	if (msg == NULL)
		return (-1);

	status = riemann_send (host, msg);
	if (status != 0)
		ERROR ("write_riemann plugin: riemann_send failed with status %i",
				status);

	riemann_msg_protobuf_free (msg);
	return (status);
} /* }}} int riemann_notification */

static int
riemann_write(const data_set_t *ds,
	      const value_list_t *vl,
	      user_data_t *ud)
{
	int			 status;
	struct riemann_host	*host = ud->data;
	Msg			*msg;

	msg = riemann_value_list_to_protobuf (host, ds, vl);
	if (msg == NULL)
		return (-1);

	status = riemann_send (host, msg);
	if (status != 0)
		ERROR ("write_riemann plugin: riemann_send failed with status %i",
				status);

	riemann_msg_protobuf_free (msg);
	return status;
}

/* host->lock must be held when calling this function. */
static int
riemann_connect(struct riemann_host *host)
{
	int			 e;
	struct addrinfo		*ai, *res, hints;
	char const		*node;
	char const		*service;

	if (host->flags & F_CONNECT)
		return 0;

	memset(&hints, 0, sizeof(hints));
	memset(&service, 0, sizeof(service));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = host->use_tcp ? SOCK_STREAM : SOCK_DGRAM;
#ifdef AI_ADDRCONFIG
	hints.ai_flags |= AI_ADDRCONFIG;
#endif

	node = (host->node != NULL) ? host->node : RIEMANN_HOST;
	service = (host->service != NULL) ? host->service : RIEMANN_PORT;

	if ((e = getaddrinfo(node, service, &hints, &res)) != 0) {
		ERROR ("write_riemann plugin: Unable to resolve host \"%s\": %s",
			node, gai_strerror(e));
		return -1;
	}

	host->s = -1;
	for (ai = res; ai != NULL; ai = ai->ai_next) {
		if ((host->s = socket(ai->ai_family,
				      ai->ai_socktype,
				      ai->ai_protocol)) == -1) {
			continue;
		}

		if (connect(host->s, ai->ai_addr, ai->ai_addrlen) != 0) {
			close(host->s);
			host->s = -1;
			continue;
		}

		host->flags |= F_CONNECT;
		DEBUG("write_riemann plugin: got a succesful connection for: %s:%s",
				node, service);
		break;
	}

	freeaddrinfo(res);

	if (host->s < 0) {
		WARNING("write_riemann plugin: Unable to connect to Riemann at %s:%s",
				node, service);
		return -1;
	}
	return 0;
}

/* host->lock must be held when calling this function. */
static int
riemann_disconnect (struct riemann_host *host)
{
	if ((host->flags & F_CONNECT) == 0)
		return (0);

	close (host->s);
	host->s = -1;
	host->flags &= ~F_CONNECT;

	return (0);
}

static void
riemann_free(void *p)
{
	struct riemann_host	*host = p;

	if (host == NULL)
		return;

	pthread_mutex_lock (&host->lock);

	host->reference_count--;
	if (host->reference_count > 0)
	{
		pthread_mutex_unlock (&host->lock);
		return;
	}

	riemann_disconnect (host);

	sfree(host->service);
	pthread_mutex_destroy (&host->lock);
	sfree(host);
}

static int
riemann_config_node(oconfig_item_t *ci)
{
	struct riemann_host	*host = NULL;
	int			 status = 0;
	int			 i;
	oconfig_item_t		*child;
	char			 callback_name[DATA_MAX_NAME_LEN];
	user_data_t		 ud;

	if ((host = calloc(1, sizeof (*host))) == NULL) {
		ERROR ("write_riemann plugin: calloc failed.");
		return ENOMEM;
	}
	pthread_mutex_init (&host->lock, NULL);
	host->reference_count = 1;
	host->node = NULL;
	host->service = NULL;
	host->store_rates = 1;
	host->always_append_ds = 0;
	host->use_tcp = 0;

	status = cf_util_get_string (ci, &host->name);
	if (status != 0) {
		WARNING("write_riemann plugin: Required host name is missing.");
		riemann_free (host);
		return -1;
	}

	for (i = 0; i < ci->children_num; i++) {
		/*
		 * The code here could be simplified but makes room
		 * for easy adding of new options later on.
		 */
		child = &ci->children[i];
		status = 0;

		if (strcasecmp ("Host", child->key) == 0) {
			status = cf_util_get_string (child, &host->node);
			if (status != 0)
				break;
		} else if (strcasecmp ("Port", child->key) == 0) {
			status = cf_util_get_service (child, &host->service);
			if (status != 0) {
				ERROR ("write_riemann plugin: Invalid argument "
						"configured for the \"Port\" "
						"option.");
				break;
			}
		} else if (strcasecmp ("Protocol", child->key) == 0) {
			char tmp[16];
			status = cf_util_get_string_buffer (child,
					tmp, sizeof (tmp));
			if (status != 0)
			{
				ERROR ("write_riemann plugin: cf_util_get_"
						"string_buffer failed with "
						"status %i.", status);
				break;
			}

			if (strcasecmp ("UDP", tmp) == 0)
				host->use_tcp = 0;
			else if (strcasecmp ("TCP", tmp) == 0)
				host->use_tcp = 1;
			else
				WARNING ("write_riemann plugin: The value "
						"\"%s\" is not valid for the "
						"\"Protocol\" option. Use "
						"either \"UDP\" or \"TCP\".",
						tmp);
		} else if (strcasecmp ("StoreRates", child->key) == 0) {
			status = cf_util_get_boolean (child, &host->store_rates);
			if (status != 0)
				break;
		} else if (strcasecmp ("AlwaysAppendDS", child->key) == 0) {
			status = cf_util_get_boolean (child,
					&host->always_append_ds);
			if (status != 0)
				break;
		} else {
			WARNING("write_riemann plugin: ignoring unknown config "
				"option: \"%s\"", child->key);
		}
	}
	if (status != 0) {
		riemann_free (host);
		return status;
	}

	ssnprintf (callback_name, sizeof (callback_name), "write_riemann/%s",
			host->name);
	ud.data = host;
	ud.free_func = riemann_free;

	pthread_mutex_lock (&host->lock);

	status = plugin_register_write (callback_name, riemann_write, &ud);
	if (status != 0)
		WARNING ("write_riemann plugin: plugin_register_write (\"%s\") "
				"failed with status %i.",
				callback_name, status);
	else /* success */
		host->reference_count++;

	status = plugin_register_notification (callback_name,
			riemann_notification, &ud);
	if (status != 0)
		WARNING ("write_riemann plugin: plugin_register_notification (\"%s\") "
				"failed with status %i.",
				callback_name, status);
	else /* success */
		host->reference_count++;

	if (host->reference_count <= 1)
	{
		/* Both callbacks failed => free memory.
		 * We need to unlock here, because riemann_free() will lock.
		 * This is not a race condition, because we're the only one
		 * holding a reference. */
		pthread_mutex_unlock (&host->lock);
		riemann_free (host);
		return (-1);
	}

	host->reference_count--;
	pthread_mutex_unlock (&host->lock);

	return status;
}

static int
riemann_config(oconfig_item_t *ci)
{
	int		 i;
	oconfig_item_t	*child;
	int		 status;

	for (i = 0; i < ci->children_num; i++)  {
		child = &ci->children[i];

		if (strcasecmp("Node", child->key) == 0) {
			riemann_config_node (child);
		} else if (strcasecmp(child->key, "tag") == 0) {
			char *tmp = NULL;
			status = cf_util_get_string(child, &tmp);
			if (status != 0)
				continue;

			strarray_add (&riemann_tags, &riemann_tags_num, tmp);
			DEBUG("write_riemann plugin: Got tag: %s", tmp);
			sfree (tmp);
		} else {
			WARNING ("write_riemann plugin: Ignoring unknown "
				 "configuration option \"%s\" at top level.",
				 child->key);
		}
	}
	return (0);
}

void
module_register(void)
{
	plugin_register_complex_config ("write_riemann", riemann_config);
}

/* vim: set sw=8 sts=8 ts=8 noet : */
