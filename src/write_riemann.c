/**
 * collectd - src/write_riemann.c
 * Copyright (C) 2012,2013  Pierre-Yves Ritschard
 * Copyright (C) 2013       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
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
#define RIEMANN_TTL_FACTOR      2.0
#define RIEMANN_BATCH_MAX      8192

int write_riemann_threshold_check(const data_set_t *, const value_list_t *, int *);

struct riemann_host {
	char			*name;
	char			*event_service_prefix;
#define F_CONNECT	 0x01
	uint8_t			 flags;
	pthread_mutex_t	 lock;
    _Bool            batch_mode;
	_Bool            notifications;
	_Bool            check_thresholds;
	_Bool			 store_rates;
	_Bool			 always_append_ds;
	char			*node;
	char			*service;
	_Bool			 use_tcp;
	int			     s;
	double			 ttl_factor;
    Msg             *batch_msg;
    cdtime_t         batch_init;
    int              batch_max;
	int			     reference_count;
};

static char	**riemann_tags;
static size_t	  riemann_tags_num;
static char	**riemann_attrs;
static size_t     riemann_attrs_num;

static void riemann_event_protobuf_free (Event *event) /* {{{ */
{
	size_t i;

	if (event == NULL)
		return;

	sfree (event->state);
	sfree (event->service);
	sfree (event->host);
	sfree (event->description);

	strarray_free (event->tags, event->n_tags);
	event->tags = NULL;
	event->n_tags = 0;

	for (i = 0; i < event->n_attributes; i++)
	{
		sfree (event->attributes[i]->key);
		sfree (event->attributes[i]->value);
		sfree (event->attributes[i]);
	}
	sfree (event->attributes);
	event->n_attributes = 0;

	sfree (event);
} /* }}} void riemann_event_protobuf_free */

static void riemann_msg_protobuf_free(Msg *msg) /* {{{ */
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

/* host->lock must be held when calling this function. */
static int riemann_connect(struct riemann_host *host) /* {{{ */
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
		DEBUG("write_riemann plugin: got a successful connection for: %s:%s",
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
} /* }}} int riemann_connect */

/* host->lock must be held when calling this function. */
static int riemann_disconnect (struct riemann_host *host) /* {{{ */
{
	if ((host->flags & F_CONNECT) == 0)
		return (0);

	close (host->s);
	host->s = -1;
	host->flags &= ~F_CONNECT;

	return (0);
} /* }}} int riemann_disconnect */

static int riemann_send_msg (struct riemann_host *host, const Msg *msg) /* {{{ */
{
	int status = 0;
	u_char *buffer = NULL;
	size_t  buffer_len;

	status = riemann_connect (host);
	if (status != 0)
		return status;

	buffer_len = msg__get_packed_size(msg);

	if (host->use_tcp)
		buffer_len += 4;

	buffer = malloc (buffer_len);
	if (buffer == NULL) {
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
		ERROR ("write_riemann plugin: Sending to Riemann at %s:%s failed: %s",
				(host->node != NULL) ? host->node : RIEMANN_HOST,
				(host->service != NULL) ? host->service : RIEMANN_PORT,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		sfree (buffer);
		return -1;
	}

	sfree (buffer);
	return 0;
} /* }}} int riemann_send_msg */

static int riemann_recv_ack(struct riemann_host *host) /* {{{ */
{
	int status = 0;
	Msg *msg = NULL;
	uint32_t header;

	status = (int) sread (host->s, &header, 4);

	if (status != 0)
		return -1;

	size_t size = ntohl(header);

	// Buffer on the stack since acknowledges are typically small.
	u_char buffer[size];
	memset (buffer, 0, size);

	status = (int) sread (host->s, buffer, size);

	if (status != 0)
		return status;

	msg = msg__unpack (NULL, size, buffer);

	if (msg == NULL)
		return -1;

	if (!msg->ok)
	{
		ERROR ("write_riemann plugin: Sending to Riemann at %s:%s acknowledgement message reported error: %s",
				(host->node != NULL) ? host->node : RIEMANN_HOST,
				(host->service != NULL) ? host->service : RIEMANN_PORT,
				msg->error);

		msg__free_unpacked(msg, NULL);
		return -1;
	}

	msg__free_unpacked (msg, NULL);
	return 0;
} /* }}} int riemann_recv_ack */

/**
 * Function to send messages (Msg) to riemann.
 *
 * Acquires the host lock, disconnects on errors.
 */
static int riemann_send(struct riemann_host *host, Msg const *msg) /* {{{ */
{
	int status = 0;
	pthread_mutex_lock (&host->lock);

	status = riemann_send_msg(host, msg);
	if (status != 0) {
		riemann_disconnect (host);
		pthread_mutex_unlock (&host->lock);
		return status;
	}

	/*
	 * For TCP we need to receive message acknowledgemenent.
	 */
	if (host->use_tcp)
	{
		status = riemann_recv_ack(host);

		if (status != 0)
		{
			riemann_disconnect (host);
			pthread_mutex_unlock (&host->lock);
			return status;
		}
	}

	pthread_mutex_unlock (&host->lock);
	return 0;
} /* }}} int riemann_send */

static int riemann_event_add_tag (Event *event, char const *tag) /* {{{ */
{
	return (strarray_add (&event->tags, &event->n_tags, tag));
} /* }}} int riemann_event_add_tag */

static int riemann_event_add_attribute(Event *event, /* {{{ */
		char const *key, char const *value)
{
	Attribute **new_attributes;
	Attribute *a;

	new_attributes = realloc (event->attributes,
			sizeof (*event->attributes) * (event->n_attributes + 1));
	if (new_attributes == NULL)
	{
		ERROR ("write_riemann plugin: realloc failed.");
		return (ENOMEM);
	}
	event->attributes = new_attributes;

	a = malloc (sizeof (*a));
	if (a == NULL)
	{
		ERROR ("write_riemann plugin: malloc failed.");
		return (ENOMEM);
	}
	attribute__init (a);

	a->key = strdup (key);
	if (value != NULL)
		a->value = strdup (value);

	event->attributes[event->n_attributes] = a;
	event->n_attributes++;

	return (0);
} /* }}} int riemann_event_add_attribute */

static Msg *riemann_notification_to_protobuf(struct riemann_host *host, /* {{{ */
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
		case NOTIF_OKAY:	severity = "ok"; break;
		case NOTIF_WARNING:	severity = "warning"; break;
		case NOTIF_FAILURE:	severity = "critical"; break;
		default:		severity = "unknown";
	}
	event->state = strdup (severity);

	riemann_event_add_tag (event, "notification");
	if (n->host[0] != 0)
		riemann_event_add_attribute (event, "host", n->host);
	if (n->plugin[0] != 0)
		riemann_event_add_attribute (event, "plugin", n->plugin);
	if (n->plugin_instance[0] != 0)
		riemann_event_add_attribute (event, "plugin_instance",
				n->plugin_instance);

	if (n->type[0] != 0)
		riemann_event_add_attribute (event, "type", n->type);
	if (n->type_instance[0] != 0)
		riemann_event_add_attribute (event, "type_instance",
				n->type_instance);

	for (i = 0; i < riemann_attrs_num; i += 2)
		riemann_event_add_attribute(event,
					    riemann_attrs[i],
					    riemann_attrs[i +1]);

	for (i = 0; i < riemann_tags_num; i++)
		riemann_event_add_tag (event, riemann_tags[i]);

	format_name (service_buffer, sizeof (service_buffer),
			/* host = */ "", n->plugin, n->plugin_instance,
			n->type, n->type_instance);
	event->service = strdup (&service_buffer[1]);

	if (n->message[0] != 0)
		riemann_event_add_attribute (event, "description", n->message);

	/* Pull in values from threshold and add extra attributes */
	for (meta = n->meta; meta != NULL; meta = meta->next)
	{
		if (strcasecmp ("CurrentValue", meta->name) == 0 && meta->type == NM_TYPE_DOUBLE)
		{
			event->metric_d = meta->nm_value.nm_double;
			event->has_metric_d = 1;
			continue;
		}

		if (meta->type == NM_TYPE_STRING) {
			riemann_event_add_attribute (event, meta->name, meta->nm_value.nm_string);
			continue;
		}
	}

	DEBUG ("write_riemann plugin: Successfully created protobuf for notification: "
			"host = \"%s\", service = \"%s\", state = \"%s\"",
			event->host, event->service, event->state);
	return (msg);
} /* }}} Msg *riemann_notification_to_protobuf */

static Event *riemann_value_to_protobuf(struct riemann_host const *host, /* {{{ */
		data_set_t const *ds,
		value_list_t const *vl, size_t index,
					 gauge_t const *rates,
					 int status)
{
	Event *event;
	char name_buffer[5 * DATA_MAX_NAME_LEN];
	char service_buffer[6 * DATA_MAX_NAME_LEN];
	double ttl;
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

	if (host->check_thresholds) {
		switch (status) {
			case STATE_OKAY:
				event->state = strdup("ok");
				break;
			case STATE_ERROR:
				event->state = strdup("critical");
				break;
			case STATE_WARNING:
				event->state = strdup("warning");
				break;
			case STATE_MISSING:
				event->state = strdup("unknown");
				break;
		}
	}

	ttl = CDTIME_T_TO_DOUBLE (vl->interval) * host->ttl_factor;
	event->ttl = (float) ttl;
	event->has_ttl = 1;

	riemann_event_add_attribute (event, "plugin", vl->plugin);
	if (vl->plugin_instance[0] != 0)
		riemann_event_add_attribute (event, "plugin_instance",
				vl->plugin_instance);

	riemann_event_add_attribute (event, "type", vl->type);
	if (vl->type_instance[0] != 0)
		riemann_event_add_attribute (event, "type_instance",
				vl->type_instance);

	if ((ds->ds[index].type != DS_TYPE_GAUGE) && (rates != NULL))
	{
		char ds_type[DATA_MAX_NAME_LEN];

		ssnprintf (ds_type, sizeof (ds_type), "%s:rate",
				DS_TYPE_TO_STRING(ds->ds[index].type));
		riemann_event_add_attribute (event, "ds_type", ds_type);
	}
	else
	{
		riemann_event_add_attribute (event, "ds_type",
				DS_TYPE_TO_STRING(ds->ds[index].type));
	}
	riemann_event_add_attribute (event, "ds_name", ds->ds[index].name);
	{
		char ds_index[DATA_MAX_NAME_LEN];

		ssnprintf (ds_index, sizeof (ds_index), "%zu", index);
		riemann_event_add_attribute (event, "ds_index", ds_index);
	}

	for (i = 0; i < riemann_attrs_num; i += 2)
		riemann_event_add_attribute(event,
					    riemann_attrs[i],
					    riemann_attrs[i +1]);

	for (i = 0; i < riemann_tags_num; i++)
		riemann_event_add_tag (event, riemann_tags[i]);

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
	{
		if (host->event_service_prefix == NULL)
			ssnprintf (service_buffer, sizeof (service_buffer), "%s/%s",
					&name_buffer[1], ds->ds[index].name);
		else
			ssnprintf (service_buffer, sizeof (service_buffer), "%s%s/%s",
					host->event_service_prefix, &name_buffer[1], ds->ds[index].name);
	}
	else
	{
		if (host->event_service_prefix == NULL)
			sstrncpy (service_buffer, &name_buffer[1], sizeof (service_buffer));
		else
			ssnprintf (service_buffer, sizeof (service_buffer), "%s%s",
					host->event_service_prefix, &name_buffer[1]);
	}

	event->service = strdup (service_buffer);

	DEBUG ("write_riemann plugin: Successfully created protobuf for metric: "
			"host = \"%s\", service = \"%s\"",
			event->host, event->service);
	return (event);
} /* }}} Event *riemann_value_to_protobuf */

static Msg *riemann_value_list_to_protobuf (struct riemann_host const *host, /* {{{ */
					    data_set_t const *ds,
					    value_list_t const *vl,
					    int *statuses)
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
							    (int) i, rates, statuses[i]);
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


/*
 * Always call while holding host->lock !
 */
static int riemann_batch_flush_nolock (cdtime_t timeout,
                                       struct riemann_host *host)
{
    cdtime_t    now;
    int         status = 0;

    if (timeout > 0) {
        now = cdtime ();
        if ((host->batch_init + timeout) > now)
            return status;
    }
    riemann_send_msg(host, host->batch_msg);
    riemann_msg_protobuf_free(host->batch_msg);

	if (host->use_tcp && ((status = riemann_recv_ack(host)) != 0))
        riemann_disconnect (host);

    host->batch_init = cdtime();
    host->batch_msg = NULL;
    return status;
}

static int riemann_batch_flush (cdtime_t timeout,
        const char *identifier __attribute__((unused)),
        user_data_t *user_data)
{
    struct riemann_host *host;
    int status;

    if (user_data == NULL)
        return (-EINVAL);

    host = user_data->data;
    pthread_mutex_lock (&host->lock);
    status = riemann_batch_flush_nolock (timeout, host);
    if (status != 0)
        ERROR ("write_riemann plugin: riemann_send failed with status %i",
               status);

    pthread_mutex_unlock(&host->lock);
    return status;
}

static int riemann_batch_add_value_list (struct riemann_host *host, /* {{{ */
                                         data_set_t const *ds,
                                         value_list_t const *vl,
                                         int *statuses)
{
	size_t i;
    Event **events;
    Msg *msg;
    size_t len;
    int ret;

    msg = riemann_value_list_to_protobuf (host, ds, vl, statuses);
    if (msg == NULL)
        return -1;

    pthread_mutex_lock(&host->lock);

    if (host->batch_msg == NULL) {
        host->batch_msg = msg;
    } else {
        len = msg->n_events + host->batch_msg->n_events;
        events = realloc(host->batch_msg->events,
                         (len * sizeof(*host->batch_msg->events)));
        if (events == NULL) {
            pthread_mutex_unlock(&host->lock);
            ERROR ("write_riemann plugin: out of memory");
            riemann_msg_protobuf_free (msg);
            return -1;
        }
        host->batch_msg->events = events;

        for (i = host->batch_msg->n_events; i < len; i++)
            host->batch_msg->events[i] = msg->events[i - host->batch_msg->n_events];

        host->batch_msg->n_events = len;
        sfree (msg->events);
        msg->n_events = 0;
        sfree (msg);
    }

	len = msg__get_packed_size(host->batch_msg);
    ret = 0;
    if (len >= host->batch_max) {
        ret = riemann_batch_flush_nolock(0, host);
    }

    pthread_mutex_unlock(&host->lock);
    return ret;
} /* }}} Msg *riemann_batch_add_value_list */

static int riemann_notification(const notification_t *n, user_data_t *ud) /* {{{ */
{
	int			 status;
	struct riemann_host	*host = ud->data;
	Msg			*msg;

	if (!host->notifications)
		return 0;

    /*
     * Never batch for notifications, send them ASAP
     */
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

static int riemann_write(const data_set_t *ds, /* {{{ */
	      const value_list_t *vl,
	      user_data_t *ud)
{
	int			 status = 0;
	int			 statuses[vl->values_len];
	struct riemann_host	*host = ud->data;
	Msg			*msg;

	if (host->check_thresholds)
		write_riemann_threshold_check(ds, vl, statuses);

    if (host->use_tcp == 1 && host->batch_mode) {

        riemann_batch_add_value_list (host, ds, vl, statuses);


    } else {

        msg = riemann_value_list_to_protobuf (host, ds, vl, statuses);
        if (msg == NULL)
            return (-1);

        status = riemann_send (host, msg);
        if (status != 0)
            ERROR ("write_riemann plugin: riemann_send failed with status %i",
                   status);

        riemann_msg_protobuf_free (msg);
    }
	return status;
} /* }}} int riemann_write */

static void riemann_free(void *p) /* {{{ */
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
} /* }}} void riemann_free */

static int riemann_config_node(oconfig_item_t *ci) /* {{{ */
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
	host->notifications = 1;
	host->check_thresholds = 0;
	host->store_rates = 1;
	host->always_append_ds = 0;
	host->use_tcp = 1;
	host->batch_mode = 1;
	host->batch_max = RIEMANN_BATCH_MAX; /* typical MSS */
	host->batch_init = cdtime();
	host->ttl_factor = RIEMANN_TTL_FACTOR;

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
		} else if (strcasecmp ("Notifications", child->key) == 0) {
			status = cf_util_get_boolean(child, &host->notifications);
			if (status != 0)
				break;
		} else if (strcasecmp ("EventServicePrefix", child->key) == 0) {
			status = cf_util_get_string (child, &host->event_service_prefix);
			if (status != 0)
				break;
		} else if (strcasecmp ("CheckThresholds", child->key) == 0) {
			status = cf_util_get_boolean(child, &host->check_thresholds);
			if (status != 0)
				break;
        } else if (strcasecmp ("Batch", child->key) == 0) {
            status = cf_util_get_boolean(child, &host->batch_mode);
            if (status != 0)
                break;
        } else if (strcasecmp("BatchMaxSize", child->key) == 0) {
            status = cf_util_get_int(child, &host->batch_max);
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
		} else if (strcasecmp ("TTLFactor", child->key) == 0) {
			double tmp = NAN;
			status = cf_util_get_double (child, &tmp);
			if (status != 0)
				break;
			if (tmp >= 2.0) {
				host->ttl_factor = tmp;
			} else if (tmp >= 1.0) {
				NOTICE ("write_riemann plugin: The configured "
						"TTLFactor is very small "
						"(%.1f). A value of 2.0 or "
						"greater is recommended.",
						tmp);
				host->ttl_factor = tmp;
			} else if (tmp > 0.0) {
				WARNING ("write_riemann plugin: The configured "
						"TTLFactor is too small to be "
						"useful (%.1f). I'll use it "
						"since the user knows best, "
						"but under protest.",
						tmp);
				host->ttl_factor = tmp;
			} else { /* zero, negative and NAN */
				ERROR ("write_riemann plugin: The configured "
						"TTLFactor is invalid (%.1f).",
						tmp);
			}
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

    if (host->use_tcp == 1 && host->batch_mode) {
        ud.free_func = NULL;
        plugin_register_flush(callback_name, riemann_batch_flush, &ud);
    }
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
} /* }}} int riemann_config_node */

static int riemann_config(oconfig_item_t *ci) /* {{{ */
{
	int		 i;
	oconfig_item_t	*child;
	int		 status;

	for (i = 0; i < ci->children_num; i++)  {
		child = &ci->children[i];

		if (strcasecmp("Node", child->key) == 0) {
			riemann_config_node (child);
		} else if (strcasecmp(child->key, "attribute") == 0) {
			char *key = NULL;
			char *val = NULL;

			if (child->values_num != 2) {
				WARNING("riemann attributes need both a key and a value.");
				return (-1);
			}
			if (child->values[0].type != OCONFIG_TYPE_STRING ||
			    child->values[1].type != OCONFIG_TYPE_STRING) {
				WARNING("riemann attribute needs string arguments.");
				return (-1);
			}
			if ((key = strdup(child->values[0].value.string)) == NULL) {
				WARNING("cannot allocate memory for attribute key.");
				return (-1);
			}
			if ((val = strdup(child->values[1].value.string)) == NULL) {
				WARNING("cannot allocate memory for attribute value.");
				return (-1);
			}
			strarray_add(&riemann_attrs, &riemann_attrs_num, key);
			strarray_add(&riemann_attrs, &riemann_attrs_num, val);
			DEBUG("write_riemann: got attr: %s => %s", key, val);
			sfree(key);
			sfree(val);
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
} /* }}} int riemann_config */

void module_register(void)
{
	plugin_register_complex_config ("write_riemann", riemann_config);
}

/* vim: set sw=8 sts=8 ts=8 noet : */
