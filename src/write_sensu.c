/**
 * collectd - src/write_sensu.c
 * Copyright (C) 2015 Fabrice A. Marie
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
 *   Fabrice A. Marie <fabrice at kibinlabs.com>
 */

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>

#include <stdlib.h>
#ifndef HAVE_ASPRINTF
/*
 * Uses asprintf() portable implementation from
 * https://github.com/littlstar/asprintf.c/blob/master/
 * copyright (c) 2014 joseph werle <joseph.werle@gmail.com> under MIT license.
 */
#include <stdio.h>
#include <stdarg.h>

int vasprintf(char **str, const char *fmt, va_list args) {
	int size = 0;
	va_list tmpa;
	// copy
	va_copy(tmpa, args);
	// apply variadic arguments to
	// sprintf with format to get size
	size = vsnprintf(NULL, size, fmt, tmpa);
	// toss args
	va_end(tmpa);
	// return -1 to be compliant if
	// size is less than 0
	if (size < 0) { return -1; }
	// alloc with size plus 1 for `\0'
	*str = (char *) malloc(size + 1);
	// return -1 to be compliant
	// if pointer is `NULL'
	if (NULL == *str) { return -1; }
	// format string with original
	// variadic arguments and set new size
	size = vsprintf(*str, fmt, args);
	return size;
}

int asprintf(char **str, const char *fmt, ...) {
	int size = 0;
	va_list args;
	// init variadic argumens
	va_start(args, fmt);
	// format and get size
	size = vasprintf(str, fmt, args);
	// toss args
	va_end(args);
	return size;
}

#endif

#define SENSU_HOST		"localhost"
#define SENSU_PORT		"3030"

struct str_list {
	int nb_strs;
	char **strs;
};

struct sensu_host {
	char			*name;
	char			*event_service_prefix;
	struct str_list metric_handlers;
	struct str_list notification_handlers;
#define F_READY      0x01
	uint8_t			 flags;
	pthread_mutex_t	 lock;
	_Bool            notifications;
	_Bool            metrics;
	_Bool			 store_rates;
	_Bool			 always_append_ds;
	char			*separator;
	char			*node;
	char			*service;
	int              s;
	struct addrinfo *res;
	int			     reference_count;
};

static char	*sensu_tags;
static char	**sensu_attrs;
static size_t sensu_attrs_num;

static int add_str_to_list(struct str_list *strs,
		const char *str_to_add) /* {{{ */
{
	char **old_strs_ptr = strs->strs;
	char *newstr = strdup(str_to_add);
	if (newstr == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return -1;
	}
	strs->strs = realloc(strs->strs, sizeof(char *) *(strs->nb_strs + 1));
	if (strs->strs == NULL) {
		strs->strs = old_strs_ptr;
		free(newstr);
		ERROR("write_sensu plugin: Unable to alloc memory");
		return -1;
	}
	strs->strs[strs->nb_strs] = newstr;
	strs->nb_strs++;
	return 0;
}
/* }}} int add_str_to_list */

static void free_str_list(struct str_list *strs) /* {{{ */
{
	int i;
	for (i=0; i<strs->nb_strs; i++)
		free(strs->strs[i]);
	free(strs->strs);
}
/* }}} void free_str_list */

static int sensu_connect(struct sensu_host *host) /* {{{ */
{
	int			 e;
	struct addrinfo		*ai, hints;
	char const		*node;
	char const		*service;

	// Resolve the target if we haven't done already
	if (!(host->flags & F_READY)) {
		memset(&hints, 0, sizeof(hints));
		memset(&service, 0, sizeof(service));
		host->res = NULL;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
		hints.ai_flags |= AI_ADDRCONFIG;
#endif

		node = (host->node != NULL) ? host->node : SENSU_HOST;
		service = (host->service != NULL) ? host->service : SENSU_PORT;

		if ((e = getaddrinfo(node, service, &hints, &(host->res))) != 0) {
			ERROR("write_sensu plugin: Unable to resolve host \"%s\": %s",
					node, gai_strerror(e));
			return -1;
		}
		DEBUG("write_sensu plugin: successfully resolved host/port: %s/%s",
				node, service);
		host->flags |= F_READY;
	}

	struct linger so_linger;
	host->s = -1;
	for (ai = host->res; ai != NULL; ai = ai->ai_next) {
		// create the socket
		if ((host->s = socket(ai->ai_family,
				      ai->ai_socktype,
				      ai->ai_protocol)) == -1) {
			continue;
		}

		// Set very low close() lingering
		so_linger.l_onoff = 1;
		so_linger.l_linger = 3;
		if (setsockopt(host->s, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger) != 0)
			WARNING("write_sensu plugin: failed to set socket close() lingering");

		// connect the socket
		if (connect(host->s, ai->ai_addr, ai->ai_addrlen) != 0) {
			close(host->s);
			host->s = -1;
			continue;
		}
		DEBUG("write_sensu plugin: connected");
		break;
	}

	if (host->s < 0) {
		WARNING("write_sensu plugin: Unable to connect to sensu client");
		return -1;
	}
	return 0;
} /* }}} int sensu_connect */

static void sensu_close_socket(struct sensu_host *host) /* {{{ */
{
	if (host->s != -1)
		close(host->s);
	host->s = -1;

} /* }}} void sensu_close_socket */

static char *build_json_str_list(const char *tag, struct str_list const *list) /* {{{ */
{
	int res;
	char *ret_str;
	char *temp_str;
	int i;
	if (list->nb_strs == 0) {
		ret_str = malloc(sizeof(char));
		if (ret_str == NULL) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str[0] = '\0';
	}

	res = asprintf(&temp_str, "\"%s\": [\"%s\"", tag, list->strs[0]);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	for (i=1; i<list->nb_strs; i++) {
		res = asprintf(&ret_str, "%s, \"%s\"", temp_str, list->strs[i]);
		free(temp_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		temp_str = ret_str;
	}
	res = asprintf(&ret_str, "%s]", temp_str);
	free(temp_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}

	return ret_str;
} /* }}} char *build_json_str_list*/

int sensu_format_name2(char *ret, int ret_len,
		const char *hostname,
		const char *plugin, const char *plugin_instance,
		const char *type, const char *type_instance,
		const char *separator)
{
	char *buffer;
	size_t buffer_size;

	buffer = ret;
	buffer_size = (size_t) ret_len;

#define APPEND(str) do {          \
	size_t l = strlen (str);        \
	if (l >= buffer_size)           \
		return (ENOBUFS);             \
	memcpy (buffer, (str), l);      \
	buffer += l; buffer_size -= l;  \
} while (0)

	assert (plugin != NULL);
	assert (type != NULL);

	APPEND (hostname);
	APPEND (separator);
	APPEND (plugin);
	if ((plugin_instance != NULL) && (plugin_instance[0] != 0))
	{
		APPEND ("-");
		APPEND (plugin_instance);
	}
	APPEND (separator);
	APPEND (type);
	if ((type_instance != NULL) && (type_instance[0] != 0))
	{
		APPEND ("-");
		APPEND (type_instance);
	}
	assert (buffer_size > 0);
	buffer[0] = 0;

#undef APPEND
	return (0);
} /* int sensu_format_name2 */

static void in_place_replace_sensu_name_reserved(char *orig_name) /* {{{ */
{
	int i;
	int len=strlen(orig_name);
	for (i=0; i<len; i++) {
		// some plugins like ipmi generate special characters in metric name
		switch(orig_name[i]) {
			case '(': orig_name[i] = '_'; break;
			case ')': orig_name[i] = '_'; break;
			case ' ': orig_name[i] = '_'; break;
			case '"': orig_name[i] = '_'; break;
			case '\'': orig_name[i] = '_'; break;
			case '+': orig_name[i] = '_'; break;
		}
	}
} /* }}} char *replace_sensu_name_reserved */

static char *sensu_value_to_json(struct sensu_host const *host, /* {{{ */
		data_set_t const *ds,
		value_list_t const *vl, size_t index,
		gauge_t const *rates,
		int status)
{
	char name_buffer[5 * DATA_MAX_NAME_LEN];
	char service_buffer[6 * DATA_MAX_NAME_LEN];
	int i;
	char *ret_str;
	char *temp_str;
	char *value_str;
	int res;
	// First part of the JSON string
	const char *part1 = "{\"name\": \"collectd\", \"type\": \"metric\"";

	char *handlers_str = build_json_str_list("handlers", &(host->metric_handlers));
	if (handlers_str == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}

	// incorporate the handlers
	if (strlen(handlers_str) == 0) {
		free(handlers_str);
		ret_str = strdup(part1);
		if (ret_str == NULL) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
	}
	else {
		res = asprintf(&ret_str, "%s, %s", part1, handlers_str);
		free(handlers_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
	}

	// incorporate the plugin name information
	res = asprintf(&temp_str, "%s, \"collectd_plugin\": \"%s\"", ret_str, vl->plugin);
	free(ret_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	// incorporate the plugin type
	res = asprintf(&temp_str, "%s, \"collectd_plugin_type\": \"%s\"", ret_str, vl->type);
	free(ret_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	// incorporate the plugin instance if any
	if (vl->plugin_instance[0] != 0) {
		res = asprintf(&temp_str, "%s, \"collectd_plugin_instance\": \"%s\"", ret_str, vl->plugin_instance);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the plugin type instance if any
	if (vl->type_instance[0] != 0) {
		res = asprintf(&temp_str, "%s, \"collectd_plugin_type_instance\": \"%s\"", ret_str, vl->type_instance);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the data source type
	if ((ds->ds[index].type != DS_TYPE_GAUGE) && (rates != NULL)) {
		char ds_type[DATA_MAX_NAME_LEN];
		ssnprintf (ds_type, sizeof (ds_type), "%s:rate", DS_TYPE_TO_STRING(ds->ds[index].type));
		res = asprintf(&temp_str, "%s, \"collectd_data_source_type\": \"%s\"", ret_str, ds_type);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	} else {
		res = asprintf(&temp_str, "%s, \"collectd_data_source_type\": \"%s\"", ret_str, DS_TYPE_TO_STRING(ds->ds[index].type));
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the data source name
	res = asprintf(&temp_str, "%s, \"collectd_data_source_name\": \"%s\"", ret_str, ds->ds[index].name);
	free(ret_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	// incorporate the data source index
	{
		char ds_index[DATA_MAX_NAME_LEN];
		ssnprintf (ds_index, sizeof (ds_index), "%zu", index);
		res = asprintf(&temp_str, "%s, \"collectd_data_source_index\": %s", ret_str, ds_index);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// add key value attributes from config if any
	for (i = 0; i < sensu_attrs_num; i += 2) {
		res = asprintf(&temp_str, "%s, \"%s\": \"%s\"", ret_str, sensu_attrs[i], sensu_attrs[i+1]);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate sensu tags from config if any
	if (strlen(sensu_tags) != 0) {
		res = asprintf(&temp_str, "%s, %s", ret_str, sensu_tags);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// calculate the value and set to a string
	if (ds->ds[index].type == DS_TYPE_GAUGE) {
		res = asprintf(&value_str, GAUGE_FORMAT, vl->values[index].gauge);
		if (res == -1) {
			free(ret_str);
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
	} else if (rates != NULL) {
		res = asprintf(&value_str, GAUGE_FORMAT, rates[index]);
		if (res == -1) {
			free(ret_str);
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
	} else {
		if (ds->ds[index].type == DS_TYPE_DERIVE) {
			res = asprintf(&value_str, "%"PRIi64, vl->values[index].derive);
			if (res == -1) {
				free(ret_str);
				ERROR("write_sensu plugin: Unable to alloc memory");
				return NULL;
			}
		}
		else if (ds->ds[index].type == DS_TYPE_ABSOLUTE) {
			res = asprintf(&value_str, "%"PRIu64, vl->values[index].absolute);
			if (res == -1) {
				free(ret_str);
				ERROR("write_sensu plugin: Unable to alloc memory");
				return NULL;
			}
		}
		else {
			res = asprintf(&value_str, "%llu", vl->values[index].counter);
			if (res == -1) {
				free(ret_str);
				ERROR("write_sensu plugin: Unable to alloc memory");
				return NULL;
			}
		}
	}

	// Generate the full service name
	sensu_format_name2(name_buffer, sizeof(name_buffer),
		vl->host, vl->plugin, vl->plugin_instance,
		vl->type, vl->type_instance, host->separator);
	if (host->always_append_ds || (ds->ds_num > 1)) {
		if (host->event_service_prefix == NULL)
			ssnprintf(service_buffer, sizeof(service_buffer), "%s.%s",
					name_buffer, ds->ds[index].name);
		else
			ssnprintf(service_buffer, sizeof(service_buffer), "%s%s.%s",
					host->event_service_prefix, name_buffer, ds->ds[index].name);
	} else {
		if (host->event_service_prefix == NULL)
			sstrncpy(service_buffer, name_buffer, sizeof(service_buffer));
		else
			ssnprintf(service_buffer, sizeof(service_buffer), "%s%s",
					host->event_service_prefix, name_buffer);
	}

	// Replace collectd sensor name reserved characters so that time series DB is happy
	in_place_replace_sensu_name_reserved(service_buffer);

	// finalize the buffer by setting the output and closing curly bracket
	res = asprintf(&temp_str, "%s, \"output\": \"%s %s %ld\"}\n", ret_str, service_buffer, value_str, CDTIME_T_TO_TIME_T(vl->time));
	free(ret_str);
	free(value_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	DEBUG("write_sensu plugin: Successfully created json for metric: "
			"host = \"%s\", service = \"%s\"",
			vl->host, service_buffer);
	return ret_str;
} /* }}} char *sensu_value_to_json */

/*
 * Uses replace_str2() implementation from
 * http://creativeandcritical.net/str-replace-c/
 * copyright (c) Laird Shaw, under public domain.
 */
char *replace_str(const char *str, const char *old, /* {{{ */
		const char *new)
{
	char *ret, *r;
	const char *p, *q;
	size_t oldlen = strlen(old);
	size_t count = strlen(new);
	size_t retlen = count;
	size_t newlen = count;
	int samesize = (oldlen == newlen);

	if (!samesize) {
		for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
			count++;
		/* This is undefined if p - str > PTRDIFF_MAX */
		retlen = p - str + strlen(p) + count * (newlen - oldlen);
	} else
		retlen = strlen(str);

	ret = malloc(retlen + 1);
	if (ret == NULL)
		return NULL;
	// added to original: not optimized, but keeps valgrind happy.
	memset(ret, 0, retlen + 1);

	r = ret;
	p = str;
	while (1) {
		/* If the old and new strings are different lengths - in other
		 * words we have already iterated through with strstr above,
		 * and thus we know how many times we need to call it - then we
		 * can avoid the final (potentially lengthy) call to strstr,
		 * which we already know is going to return NULL, by
		 * decrementing and checking count.
		 */
		if (!samesize && !count--)
			break;
		/* Otherwise i.e. when the old and new strings are the same
		 * length, and we don't know how many times to call strstr,
		 * we must check for a NULL return here (we check it in any
		 * event, to avoid further conditions, and because there's
		 * no harm done with the check even when the old and new
		 * strings are different lengths).
		 */
		if ((q = strstr(p, old)) == NULL)
			break;
		/* This is undefined if q - p > PTRDIFF_MAX */
		ptrdiff_t l = q - p;
		memcpy(r, p, l);
		r += l;
		memcpy(r, new, newlen);
		r += newlen;
		p = q + oldlen;
	}
	strncpy(r, p, strlen(p));

	return ret;
} /* }}} char *replace_str */

static char *replace_json_reserved(const char *message) /* {{{ */
{
	char *msg = replace_str(message, "\\", "\\\\");
	if (msg == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	char *tmp = replace_str(msg, "\"", "\\\"");
	free(msg);
	if (tmp == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	msg = replace_str(tmp, "\n", "\\\n");
	free(tmp);
	if (msg == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	return msg;
} /* }}} char *replace_json_reserved */

static char *sensu_notification_to_json(struct sensu_host *host, /* {{{ */
		notification_t const *n)
{
	char service_buffer[6 * DATA_MAX_NAME_LEN];
	char const *severity;
	notification_meta_t *meta;
	char *ret_str;
	char *temp_str;
	int status;
	int i;
	int res;
	// add the severity/status
	switch (n->severity) {
		case NOTIF_OKAY:
			severity = "OK";
			status = 0;
			break;
		case NOTIF_WARNING:
			severity = "WARNING";
			status = 1;
			break;
		case NOTIF_FAILURE:
			severity = "CRITICAL";
			status = 2;
			break;
		default:
			severity = "UNKNOWN";
			status = 3;
	}
	res = asprintf(&temp_str, "{\"status\": %d", status);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	// incorporate the timestamp
	res = asprintf(&temp_str, "%s, \"timestamp\": %ld", ret_str, CDTIME_T_TO_TIME_T(n->time));
	free(ret_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	char *handlers_str = build_json_str_list("handlers", &(host->notification_handlers));
	if (handlers_str == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	// incorporate the handlers
	if (strlen(handlers_str) != 0) {
		res = asprintf(&temp_str, "%s, %s", ret_str, handlers_str);
		free(ret_str);
		free(handlers_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	} else {
		free(handlers_str);
	}

	// incorporate the plugin name information if any
	if (n->plugin[0] != 0) {
		res = asprintf(&temp_str, "%s, \"collectd_plugin\": \"%s\"", ret_str, n->plugin);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the plugin type if any
	if (n->type[0] != 0) {
		res = asprintf(&temp_str, "%s, \"collectd_plugin_type\": \"%s\"", ret_str, n->type);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the plugin instance if any
	if (n->plugin_instance[0] != 0) {
		res = asprintf(&temp_str, "%s, \"collectd_plugin_instance\": \"%s\"", ret_str, n->plugin_instance);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the plugin type instance if any
	if (n->type_instance[0] != 0) {
		res = asprintf(&temp_str, "%s, \"collectd_plugin_type_instance\": \"%s\"", ret_str, n->type_instance);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// add key value attributes from config if any
	for (i = 0; i < sensu_attrs_num; i += 2) {
		res = asprintf(&temp_str, "%s, \"%s\": \"%s\"", ret_str, sensu_attrs[i], sensu_attrs[i+1]);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate sensu tags from config if any
	if (strlen(sensu_tags) != 0) {
		res = asprintf(&temp_str, "%s, %s", ret_str, sensu_tags);
		free(ret_str);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// incorporate the service name
	sensu_format_name2(service_buffer, sizeof(service_buffer),
				/* host */ "", n->plugin, n->plugin_instance,
				n->type, n->type_instance, host->separator);
	// replace sensu event name chars that are considered illegal
	in_place_replace_sensu_name_reserved(service_buffer);
	res = asprintf(&temp_str, "%s, \"name\": \"%s\"", ret_str, &service_buffer[1]);
	free(ret_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	// incorporate the check output
	if (n->message[0] != 0) {
		char *msg = replace_json_reserved(n->message);
		if (msg == NULL) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		res = asprintf(&temp_str, "%s, \"output\": \"%s - %s\"", ret_str, severity, msg);
		free(ret_str);
		free(msg);
		if (res == -1) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return NULL;
		}
		ret_str = temp_str;
	}

	// Pull in values from threshold and add extra attributes
	for (meta = n->meta; meta != NULL; meta = meta->next) {
		if (strcasecmp("CurrentValue", meta->name) == 0 && meta->type == NM_TYPE_DOUBLE) {
			res = asprintf(&temp_str, "%s, \"current_value\": \"%.8f\"", ret_str, meta->nm_value.nm_double);
			free(ret_str);
			if (res == -1) {
				ERROR("write_sensu plugin: Unable to alloc memory");
				return NULL;
			}
			ret_str = temp_str;
		}
		if (meta->type == NM_TYPE_STRING) {
			res = asprintf(&temp_str, "%s, \"%s\": \"%s\"", ret_str, meta->name, meta->nm_value.nm_string);
			free(ret_str);
			if (res == -1) {
				ERROR("write_sensu plugin: Unable to alloc memory");
				return NULL;
			}
			ret_str = temp_str;
		}
	}

	// close the curly bracket
	res = asprintf(&temp_str, "%s}\n", ret_str);
	free(ret_str);
	if (res == -1) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return NULL;
	}
	ret_str = temp_str;

	DEBUG("write_sensu plugin: Successfully created JSON for notification: "
				"host = \"%s\", service = \"%s\", state = \"%s\"",
				n->host, service_buffer, severity);
	return ret_str;
} /* }}} char *sensu_notification_to_json */

static int sensu_send_msg(struct sensu_host *host, const char *msg) /* {{{ */
{
	int status = 0;
	size_t  buffer_len;

	status = sensu_connect(host);
	if (status != 0)
		return status;

	buffer_len = strlen(msg);

	status = (int) swrite(host->s, msg, buffer_len);
	sensu_close_socket(host);

	if (status != 0) {
		char errbuf[1024];
		ERROR("write_sensu plugin: Sending to Sensu at %s:%s failed: %s",
				(host->node != NULL) ? host->node : SENSU_HOST,
				(host->service != NULL) ? host->service : SENSU_PORT,
				sstrerror(errno, errbuf, sizeof(errbuf)));
		return -1;
	}

	return 0;
} /* }}} int sensu_send_msg */


static int sensu_send(struct sensu_host *host, char const *msg) /* {{{ */
{
	int status = 0;

	status = sensu_send_msg(host, msg);
	if (status != 0) {
		host->flags &= ~F_READY;
		if (host->res != NULL) {
			freeaddrinfo(host->res);
			host->res = NULL;
		}
		return status;
	}

	return 0;
} /* }}} int sensu_send */


static int sensu_write(const data_set_t *ds, /* {{{ */
	      const value_list_t *vl,
	      user_data_t *ud)
{
	int status = 0;
	int statuses[vl->values_len];
	struct sensu_host	*host = ud->data;
	gauge_t *rates = NULL;
	int i;
	char *msg;

	pthread_mutex_lock(&host->lock);
	memset(statuses, 0, vl->values_len * sizeof(*statuses));

	if (host->store_rates) {
		rates = uc_get_rate(ds, vl);
		if (rates == NULL) {
			ERROR("write_sensu plugin: uc_get_rate failed.");
			pthread_mutex_unlock(&host->lock);
			return -1;
		}
	}
	for (i = 0; i < (size_t) vl->values_len; i++) {
		msg = sensu_value_to_json(host, ds, vl, (int) i, rates, statuses[i]);
		if (msg == NULL) {
			sfree(rates);
			pthread_mutex_unlock(&host->lock);
			return -1;
		}
		status = sensu_send(host, msg);
		free(msg);
		if (status != 0) {
			ERROR("write_sensu plugin: sensu_send failed with status %i", status);
			pthread_mutex_unlock(&host->lock);
			sfree(rates);
			return status;
		}
	}
	sfree(rates);
	pthread_mutex_unlock(&host->lock);
	return status;
} /* }}} int sensu_write */

static int sensu_notification(const notification_t *n, user_data_t *ud) /* {{{ */
{
	int	status;
	struct sensu_host *host = ud->data;
	char *msg;

	pthread_mutex_lock(&host->lock);

	msg = sensu_notification_to_json(host, n);
	if (msg == NULL) {
		pthread_mutex_unlock(&host->lock);
		return -1;
	}

	status = sensu_send(host, msg);
	free(msg);
	if (status != 0)
		ERROR("write_sensu plugin: sensu_send failed with status %i", status);
	pthread_mutex_unlock(&host->lock);

	return status;
} /* }}} int sensu_notification */

static void sensu_free(void *p) /* {{{ */
{
	struct sensu_host *host = p;

	if (host == NULL)
		return;

	pthread_mutex_lock(&host->lock);

	host->reference_count--;
	if (host->reference_count > 0) {
		pthread_mutex_unlock(&host->lock);
		return;
	}

	sensu_close_socket(host);
	if (host->res != NULL) {
		freeaddrinfo(host->res);
		host->res = NULL;
	}
	sfree(host->service);
	sfree(host->event_service_prefix);
	sfree(host->name);
	sfree(host->node);
	sfree(host->separator);
	free_str_list(&(host->metric_handlers));
	free_str_list(&(host->notification_handlers));
	pthread_mutex_destroy(&host->lock);
	sfree(host);
} /* }}} void sensu_free */


static int sensu_config_node(oconfig_item_t *ci) /* {{{ */
{
	struct sensu_host	*host = NULL;
	int					status = 0;
	int					i;
	oconfig_item_t		*child;
	char				callback_name[DATA_MAX_NAME_LEN];
	user_data_t			ud;

	if ((host = calloc(1, sizeof(*host))) == NULL) {
		ERROR("write_sensu plugin: calloc failed.");
		return ENOMEM;
	}
	pthread_mutex_init(&host->lock, NULL);
	host->reference_count = 1;
	host->node = NULL;
	host->service = NULL;
	host->notifications = 0;
	host->metrics = 0;
	host->store_rates = 1;
	host->always_append_ds = 0;
	host->metric_handlers.nb_strs = 0;
	host->metric_handlers.strs = NULL;
	host->notification_handlers.nb_strs = 0;
	host->notification_handlers.strs = NULL;
	host->separator = strdup("/");
	if (host->separator == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		sensu_free(host);
		return -1;
	}

	status = cf_util_get_string(ci, &host->name);
	if (status != 0) {
		WARNING("write_sensu plugin: Required host name is missing.");
		sensu_free(host);
		return -1;
	}

	for (i = 0; i < ci->children_num; i++) {
		child = &ci->children[i];
		status = 0;

		if (strcasecmp("Host", child->key) == 0) {
			status = cf_util_get_string(child, &host->node);
			if (status != 0)
				break;
		} else if (strcasecmp("Notifications", child->key) == 0) {
			status = cf_util_get_boolean(child, &host->notifications);
			if (status != 0)
				break;
		} else if (strcasecmp("Metrics", child->key) == 0) {
					status = cf_util_get_boolean(child, &host->metrics);
					if (status != 0)
						break;
		} else if (strcasecmp("EventServicePrefix", child->key) == 0) {
			status = cf_util_get_string(child, &host->event_service_prefix);
			if (status != 0)
				break;
		} else if (strcasecmp("Separator", child->key) == 0) {
				status = cf_util_get_string(child, &host->separator);
				if (status != 0)
					break;
		} else if (strcasecmp("MetricHandler", child->key) == 0) {
			char *temp_str = NULL;
			status = cf_util_get_string(child, &temp_str);
			if (status != 0)
				break;
			status = add_str_to_list(&(host->metric_handlers), temp_str);
			free(temp_str);
			if (status != 0)
				break;
		} else if (strcasecmp("NotificationHandler", child->key) == 0) {
			char *temp_str = NULL;
			status = cf_util_get_string(child, &temp_str);
			if (status != 0)
				break;
			status = add_str_to_list(&(host->notification_handlers), temp_str);
			free(temp_str);
			if (status != 0)
				break;
		} else if (strcasecmp("Port", child->key) == 0) {
			status = cf_util_get_service(child, &host->service);
			if (status != 0) {
				ERROR("write_sensu plugin: Invalid argument "
						"configured for the \"Port\" "
						"option.");
				break;
			}
		} else if (strcasecmp("StoreRates", child->key) == 0) {
			status = cf_util_get_boolean(child, &host->store_rates);
			if (status != 0)
				break;
		} else if (strcasecmp("AlwaysAppendDS", child->key) == 0) {
			status = cf_util_get_boolean(child,
					&host->always_append_ds);
			if (status != 0)
				break;
		} else {
			WARNING("write_sensu plugin: ignoring unknown config "
				"option: \"%s\"", child->key);
		}
	}
	if (status != 0) {
		sensu_free(host);
		return status;
	}

	if (host->metrics && (host->metric_handlers.nb_strs == 0)) {
			sensu_free(host);
			WARNING("write_sensu plugin: metrics enabled but no MetricHandler defined. Giving up.");
			return -1;
		}

	if (host->notifications && (host->notification_handlers.nb_strs == 0)) {
		sensu_free(host);
		WARNING("write_sensu plugin: notifications enabled but no NotificationHandler defined. Giving up.");
		return -1;
	}

	if ((host->notification_handlers.nb_strs > 0) && (host->notifications == 0)) {
		WARNING("write_sensu plugin: NotificationHandler given so forcing notifications to be enabled");
		host->notifications = 1;
	}

	if ((host->metric_handlers.nb_strs > 0) && (host->metrics == 0)) {
		WARNING("write_sensu plugin: MetricHandler given so forcing metrics to be enabled");
		host->metrics = 1;
	}

	if (!(host->notifications || host->metrics)) {
		WARNING("write_sensu plugin: neither metrics nor notifications enabled. Giving up.");
		sensu_free(host);
		return -1;
	}

	ssnprintf(callback_name, sizeof(callback_name), "write_sensu/%s", host->name);
	ud.data = host;
	ud.free_func = sensu_free;

	pthread_mutex_lock(&host->lock);

	if (host->metrics) {
		status = plugin_register_write(callback_name, sensu_write, &ud);
		if (status != 0)
			WARNING("write_sensu plugin: plugin_register_write (\"%s\") "
					"failed with status %i.",
					callback_name, status);
		else /* success */
			host->reference_count++;
	}

	if (host->notifications) {
		status = plugin_register_notification(callback_name, sensu_notification, &ud);
		if (status != 0)
			WARNING("write_sensu plugin: plugin_register_notification (\"%s\") "
					"failed with status %i.",
					callback_name, status);
		else
			host->reference_count++;
	}

	if (host->reference_count <= 1) {
		/* Both callbacks failed => free memory.
		 * We need to unlock here, because sensu_free() will lock.
		 * This is not a race condition, because we're the only one
		 * holding a reference. */
		pthread_mutex_unlock(&host->lock);
		sensu_free(host);
		return -1;
	}

	host->reference_count--;
	pthread_mutex_unlock(&host->lock);

	return status;
} /* }}} int sensu_config_node */

static int sensu_config(oconfig_item_t *ci) /* {{{ */
{
	int		 i;
	oconfig_item_t	*child;
	int		 status;
	struct str_list sensu_tags_arr;

	sensu_tags_arr.nb_strs = 0;
	sensu_tags_arr.strs = NULL;
	sensu_tags = malloc(sizeof(char));
	if (sensu_tags == NULL) {
		ERROR("write_sensu plugin: Unable to alloc memory");
		return -1;
	}
	sensu_tags[0] = '\0';

	for (i = 0; i < ci->children_num; i++)  {
		child = &ci->children[i];

		if (strcasecmp("Node", child->key) == 0) {
			sensu_config_node(child);
		} else if (strcasecmp(child->key, "attribute") == 0) {
			char *key = NULL;
			char *val = NULL;

			if (child->values_num != 2) {
				WARNING("sensu attributes need both a key and a value.");
				free(sensu_tags);
				return -1;
			}
			if (child->values[0].type != OCONFIG_TYPE_STRING ||
			    child->values[1].type != OCONFIG_TYPE_STRING) {
				WARNING("sensu attribute needs string arguments.");
				free(sensu_tags);
				return -1;
			}
			if ((key = strdup(child->values[0].value.string)) == NULL) {
				ERROR("write_sensu plugin: Unable to alloc memory");
				free(sensu_tags);
				return -1;
			}
			if ((val = strdup(child->values[1].value.string)) == NULL) {
				free(sensu_tags);
				free(key);
				ERROR("write_sensu plugin: Unable to alloc memory");
				return -1;
			}
			strarray_add(&sensu_attrs, &sensu_attrs_num, key);
			strarray_add(&sensu_attrs, &sensu_attrs_num, val);
			DEBUG("write_sensu: got attr: %s => %s", key, val);
			sfree(key);
			sfree(val);
		} else if (strcasecmp(child->key, "tag") == 0) {
			char *tmp = NULL;
			status = cf_util_get_string(child, &tmp);
			if (status != 0)
				continue;

			status = add_str_to_list(&sensu_tags_arr, tmp);
			sfree(tmp);
			if (status != 0)
				continue;
			DEBUG("write_sensu plugin: Got tag: %s", tmp);
		} else {
			WARNING("write_sensu plugin: Ignoring unknown "
				 "configuration option \"%s\" at top level.",
				 child->key);
		}
	}
	if (sensu_tags_arr.nb_strs > 0) {
		free(sensu_tags);
		sensu_tags = build_json_str_list("tags", &sensu_tags_arr);
		free_str_list(&sensu_tags_arr);
		if (sensu_tags == NULL) {
			ERROR("write_sensu plugin: Unable to alloc memory");
			return -1;
		}
	}
	return 0;
} /* }}} int sensu_config */

void module_register(void)
{
	plugin_register_complex_config("write_sensu", sensu_config);
}

/* vim: set sw=8 sts=8 ts=8 noet : */
