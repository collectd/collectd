/*
 * collectd - src/riemann.c
 *
 * Copyright (C) 2012  Pierre-Yves Ritschard <pyr@spootnik.org>
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
 */

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "riemann.pb-c.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <inttypes.h>
#include <pthread.h>

#define RIEMANN_DELAY		1
#define RIEMANN_PORT		"5555"
#define RIEMANN_MAX_TAGS	37
#define RIEMANN_EXTRA_TAGS	32

struct riemann_host {
#define F_CONNECT		 0x01
	uint8_t			 flags;
	pthread_mutex_t		 lock;
	int			 delay;
	char			*node;
	char			*service;
	int			 s;

	int			 reference_count;
};

static char	*riemann_tags[RIEMANN_EXTRA_TAGS];
static int	 riemann_tagcount;

static int	riemann_send(struct riemann_host *, Msg const *);
static int	riemann_notification(const notification_t *, user_data_t *);
static int	riemann_write(const data_set_t *, const value_list_t *, user_data_t *);
static int	riemann_connect(struct riemann_host *);
static int	riemann_disconnect (struct riemann_host *host);
static void	riemann_free(void *);
static int	riemann_config_host(oconfig_item_t *);
static int	riemann_config(oconfig_item_t *);
void	module_register(void);

static void riemann_event_protobuf_free (Event *event) /* {{{ */
{
	size_t i;

	if (event == NULL)
		return;

	sfree (event->state);
	sfree (event->service);
	sfree (event->host);
	sfree (event->description);

	for (i = 0; i < event->n_tags; i++)
		sfree (event->tags[i]);
	sfree (event->tags);

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
	buffer = malloc (buffer_len);
	if (buffer == NULL) {
		pthread_mutex_unlock (&host->lock);
		ERROR ("riemann plugin: malloc failed.");
		return ENOMEM;
	}
	memset (buffer, 0, buffer_len);

	msg__pack(msg, buffer);

	status = (int) swrite (host->s, buffer, buffer_len);
	if (status != 0)
	{
		char errbuf[1024];

		riemann_disconnect (host);
		pthread_mutex_unlock (&host->lock);

		ERROR ("riemann plugin: Sending to Riemann at %s:%s failed: %s",
				host->node,
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

	char **tmp;

	tmp = realloc (event->tags, (event->n_tags + 1) * sizeof (*event->tags));
	if (tmp == NULL)
		return (ENOMEM);
	event->tags = tmp;

	va_start (ap, format);
	ret = vsnprintf (buffer, sizeof (buffer), format, ap);
	if (ret >= sizeof (buffer))
		ret = sizeof (buffer) - 1;
	buffer[ret] = 0;
	va_end (ap);

	event->tags[event->n_tags] = strdup (buffer);
	if (event->tags[event->n_tags] == NULL)
		return (ENOMEM);
	event->n_tags++;
	return (0);
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
		ERROR ("riemann plugin: malloc failed.");
		return (NULL);
	}
	memset (msg, 0, sizeof (*msg));
	msg__init (msg);

	msg->events = malloc (sizeof (*msg->events));
	if (msg->events == NULL)
	{
		ERROR ("riemann plugin: malloc failed.");
		sfree (msg);
		return (NULL);
	}

	event = malloc (sizeof (*event));
	if (event == NULL)
	{
		ERROR ("riemann plugin: malloc failed.");
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

	for (i = 0; i < riemann_tagcount; i++)
		riemann_event_add_tag (event, "%s", riemann_tags[i]);

	/* TODO: Use FORMAT_VL() here. */
	ssnprintf (service_buffer, sizeof(service_buffer),
			"%s-%s-%s-%s", n->plugin, n->plugin_instance,
			n->type, n->type_instance);
	event->service = strdup (service_buffer);

	/* Pull in values from threshold */
	for (meta = n->meta; meta != NULL; meta = meta->next)
	{
		if (strcasecmp ("CurrentValue", meta->name) != 0)
			continue;

		event->metric_d = meta->nm_value.nm_double;
		event->has_metric_d = 1;
		break;
	}

	DEBUG ("riemann plugin: Successfully created protobuf for notification: "
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
	char service_buffer[6 * DATA_MAX_NAME_LEN];
	int i;

	event = malloc (sizeof (*event));
	if (event == NULL)
	{
		ERROR ("riemann plugin: malloc failed.");
		return (NULL);
	}
	memset (event, 0, sizeof (*event));
	event__init (event);

	event->host = strdup (vl->host);
	event->time = CDTIME_T_TO_TIME_T (vl->time);
	event->has_time = 1;
	event->ttl = CDTIME_T_TO_TIME_T (vl->interval) + host->delay;
	event->has_ttl = 1;

	riemann_event_add_tag (event, "plugin:%s", vl->plugin);
	if (vl->plugin_instance[0] != 0)
		riemann_event_add_tag (event, "plugin_instance:%s",
				vl->plugin_instance);

	riemann_event_add_tag (event, "type:%s", vl->type);
	if (vl->type_instance[0] != 0)
		riemann_event_add_tag (event, "type_instance:%s",
				vl->type_instance);

	riemann_event_add_tag (event, "ds_type:%s",
			DS_TYPE_TO_STRING(ds->ds[index].type));
	riemann_event_add_tag (event, "ds_name:%s", ds->ds[index].name);
	riemann_event_add_tag (event, "ds_index:%zu", index);

	for (i = 0; i < riemann_tagcount; i++)
		riemann_event_add_tag (event, "%s", riemann_tags[i]);

	if (rates != NULL)
	{
		event->has_metric_d = 1;
		event->metric_d = (double) rates[index];
	}
	else if (ds->ds[index].type == DS_TYPE_GAUGE)
	{
		event->has_metric_d = 1;
		event->metric_d = (double) vl->values[index].gauge;
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

	/* TODO: Use FORMAT_VL() here. */
	ssnprintf (service_buffer, sizeof(service_buffer),
			"%s-%s-%s-%s-%s", vl->plugin, vl->plugin_instance,
			vl->type, vl->type_instance, ds->ds[index].name);
	event->service = strdup (service_buffer);

	DEBUG ("riemann plugin: Successfully created protobuf for metric: "
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

	/* Initialize the Msg structure. */
	msg = malloc (sizeof (*msg));
	if (msg == NULL)
	{
		ERROR ("riemann plugin: malloc failed.");
		return (NULL);
	}
	memset (msg, 0, sizeof (*msg));
	msg__init (msg);

	/* Set up events. First, the list of pointers. */
	msg->n_events = (size_t) vl->values_len;
	msg->events = calloc (msg->n_events, sizeof (*msg->events));
	if (msg->events == NULL)
	{
		ERROR ("riemann plugin: calloc failed.");
		riemann_msg_protobuf_free (msg);
		return (NULL);
	}

	for (i = 0; i < msg->n_events; i++)
	{
		msg->events[i] = riemann_value_to_protobuf (host, ds, vl,
				(int) i, /* rates = */ NULL);
		if (msg->events[i] == NULL)
		{
			riemann_msg_protobuf_free (msg);
			return (NULL);
		}
	}

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
		ERROR ("riemann plugin: riemann_send failed with status %i",
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
		ERROR ("riemann plugin: riemann_send failed with status %i",
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
	char const		*service;

	if (host->flags & F_CONNECT)
		return 0;

	memset(&hints, 0, sizeof(hints));
	memset(&service, 0, sizeof(service));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	assert (host->node != NULL);
	service = (host->service != NULL) ? host->service : RIEMANN_PORT;

	if ((e = getaddrinfo(host->node, service, &hints, &res)) != 0) {
		ERROR ("riemann plugin: Unable to resolve host \"%s\": %s",
			host->node, gai_strerror(e));
		return -1;
	}

	for (ai = res; ai != NULL; ai = ai->ai_next) {
		/*
		 * check if another thread did not already succesfully connect
		 */
		if (host->flags & F_CONNECT) {
			freeaddrinfo(res);
			return 0;
		}

		if ((host->s = socket(ai->ai_family,
				      ai->ai_socktype,
				      ai->ai_protocol)) == -1) {
			WARNING("riemann_connect: could not open socket");
			freeaddrinfo(res);
			return -1;
		}

		if (connect(host->s, ai->ai_addr, ai->ai_addrlen) != 0) {
			close(host->s);
			host->flags |= ~F_CONNECT;
			freeaddrinfo(res);
			return -1;
		}
		host->flags |= F_CONNECT;
		DEBUG("riemann plugin: got a succesful connection for: %s",
				host->node);
		break;
	}

	freeaddrinfo(res);
	if (ai == NULL) {
		WARNING("riemann_connect: no suitable hosts found");
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
riemann_config_host(oconfig_item_t *ci)
{
	struct riemann_host	*host = NULL;
	int			 status = 0;
	int			 i;
	oconfig_item_t		*child;
	char			 w_cb_name[DATA_MAX_NAME_LEN];
	char			 n_cb_name[DATA_MAX_NAME_LEN];
	user_data_t		 ud;

	if (ci->values_num != 1 ||
	    ci->values[0].type != OCONFIG_TYPE_STRING) {
		WARNING("riemann hosts need one string argument");
		return -1;
	}

	if ((host = calloc(1, sizeof (*host))) == NULL) {
		WARNING("riemann host allocation failed");
		return ENOMEM;
	}
	pthread_mutex_init (&host->lock, NULL);
	host->reference_count = 1;
	host->node = NULL;
	host->service = NULL;
	host->delay = RIEMANN_DELAY;

	status = cf_util_get_string (ci, &host->node);
	if (status != 0) {
		WARNING("riemann plugin: Required host name is missing.");
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

		if (strcasecmp(child->key, "port") == 0) {
			status = cf_util_get_service (child, &host->service);
			if (status != 0) {
				ERROR ("riemann plugin: Invalid argument "
						"configured for the \"Port\" "
						"option.");
				break;
			}
		} else if (strcasecmp(child->key, "delay") == 0) {
			if ((status = cf_util_get_int(ci, &host->delay)) != 0)
				break;
		} else {
			WARNING("riemann plugin: ignoring unknown config "
				"option: \"%s\"", child->key);
		}
	}
	if (status != 0) {
		riemann_free (host);
		return status;
	}

	ssnprintf(w_cb_name, sizeof(w_cb_name), "write-riemann/%s:%s",
		  host->node,
		  (host->service != NULL) ? host->service : RIEMANN_PORT);
	ssnprintf(n_cb_name, sizeof(n_cb_name), "notification-riemann/%s:%s",
		  host->node,
		  (host->service != NULL) ? host->service : RIEMANN_PORT);
	DEBUG("riemann w_cb_name: %s", w_cb_name);
	DEBUG("riemann n_cb_name: %s", n_cb_name);
	ud.data = host;
	ud.free_func = riemann_free;

	pthread_mutex_lock (&host->lock);

	status = plugin_register_write (w_cb_name, riemann_write, &ud);
	if (status != 0)
		WARNING ("riemann plugin: plugin_register_write (\"%s\") "
				"failed with status %i.",
				w_cb_name, status);
	else /* success */
		host->reference_count++;

	status = plugin_register_notification (n_cb_name,
			riemann_notification, &ud);
	if (status != 0)
		WARNING ("riemann plugin: plugin_register_notification (\"%s\") "
				"failed with status %i.",
				n_cb_name, status);
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
	char		*newtag;
	oconfig_item_t	*child;

	for (i = 0; i < ci->children_num; i++)  {
		child = &ci->children[i];

		if (strcasecmp(child->key, "host") == 0) {
			riemann_config_host(child);
		} else if (strcasecmp(child->key, "tag") == 0) {
			if (riemann_tagcount >= RIEMANN_EXTRA_TAGS) {
				WARNING("riemann plugin: too many tags");
				return -1;
			}
			newtag = NULL;
			cf_util_get_string(child, &newtag);
			if (newtag == NULL)
				return -1;
			riemann_tags[riemann_tagcount++] = newtag;
			DEBUG("riemann_config: got tag: %s", newtag);

		} else {
			WARNING ("riemann plugin: Ignoring unknown "
				 "configuration option \"%s\" at top level.",
				 child->key);
		}
	}
	return (0);
}

void
module_register(void)
{
	DEBUG("riemann: module_register");

	plugin_register_complex_config ("riemann", riemann_config);
}

/* vim: set sw=8 sts=8 ts=8 noet : */
