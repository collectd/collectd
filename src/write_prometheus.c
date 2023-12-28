/**
 * collectd - src/write_prometheus.c
 * Copyright (C) 2016       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

#include "collectd.h"

#include "daemon/plugin.h"
#include "daemon/resource.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils_complain.h"
#include "utils_time.h"

#include <microhttpd.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef PROMETHEUS_DEFAULT_STALENESS_DELTA
#define PROMETHEUS_DEFAULT_STALENESS_DELTA TIME_T_TO_CDTIME_T_STATIC(300)
#endif

#define CONTENT_TYPE_TEXT "text/plain; version=0.0.4"

#if MHD_VERSION >= 0x00097002
#define MHD_RESULT enum MHD_Result
#else
#define MHD_RESULT int
#endif

/* Label names must match the regex `[a-zA-Z_][a-zA-Z0-9_]*`. Label names
 * beginning with __ are reserved for internal use.
 *
 * Source:
 * https://prometheus.io/docs/concepts/data_model/#metric-names-and-labels */
#define VALID_LABEL_CHARS                                                      \
  "abcdefghijklmnopqrstuvwxyz"                                                 \
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                                                 \
  "0123456789_"

/* Metric names must match the regex `[a-zA-Z_:][a-zA-Z0-9_:]*` */
// instrument-name = ALPHA 0*254 ("_" / "." / "-" / "/" / ALPHA / DIGIT)
#define VALID_NAME_CHARS VALID_LABEL_CHARS ":"

#define RESOURCE_LABEL_PREFIX "resource_"

static c_avl_tree_t *prom_metrics;
static pthread_mutex_t prom_metrics_lock = PTHREAD_MUTEX_INITIALIZER;

static char *httpd_host = NULL;
static unsigned short httpd_port = 9103;
static struct MHD_Daemon *httpd;

static cdtime_t staleness_delta = PROMETHEUS_DEFAULT_STALENESS_DELTA;

/* Visible for testing */
int format_label_name(strbuf_t *buf, char const *name) {
  int status = 0;

  strbuf_t namebuf = STRBUF_CREATE;
  status =
      status || strbuf_print_restricted(&namebuf, name, VALID_LABEL_CHARS, '_');

  if (strncmp("__", namebuf.ptr, 2) == 0) {
    /* no prefix */
  } else if (namebuf.ptr[0] == '_') {
    status = status || strbuf_print(buf, "key");
  } else if (isdigit(namebuf.ptr[0])) {
    status = status || strbuf_print(buf, "key_");
  }

  status = status || strbuf_print(buf, namebuf.ptr);

  STRBUF_DESTROY(namebuf);
  return status;
}

static int format_label_pair(strbuf_t *buf, label_pair_t l, bool *first_label) {
  int status = 0;

  if (!*first_label) {
    status = status || strbuf_print(buf, ",");
  }
  status = status || format_label_name(buf, l.name);
  status = status || strbuf_print(buf, "=\"");
  status = status || strbuf_print_escaped(buf, l.value, "\\\"\n\r\t", '\\');
  status = status || strbuf_print(buf, "\"");
  *first_label = false;

  return status;
}

static int format_label_set(strbuf_t *buf, label_set_t labels, char const *job,
                            char const *instance) {
  bool first_label = true;
  int status = 0;

  if (job != NULL) {
    status =
        status || format_label_pair(buf, (label_pair_t){"job", (char *)job},
                                    &first_label);
  }
  if (instance != NULL) {
    status = status || format_label_pair(
                           buf, (label_pair_t){"instance", (char *)instance},
                           &first_label);
  }

  for (size_t i = 0; i < labels.num; i++) {
    status = status || format_label_pair(buf, labels.ptr[i], &first_label);
  }

  return status;
}

static int format_metric(strbuf_t *buf, metric_t const *m,
                         char const *metric_family_name, char const *job,
                         char const *instance) {
  if ((buf == NULL) || (m == NULL)) {
    return EINVAL;
  }

  /* metric_family_name is already escaped, so strbuf_print_restricted should
   * not replace any characters. */
  int status =
      strbuf_print_restricted(buf, metric_family_name, VALID_NAME_CHARS, '_');
  if (m->label.num == 0) {
    return status;
  }

  status = status || strbuf_print(buf, "{");
  status = status || format_label_set(buf, m->label, job, instance);
  status = status || strbuf_print(buf, "}");

  return status;
}

/* format_metric_family_name creates a Prometheus compatible metric name by
 * replacing all characters that are invalid in Prometheus with underscores,
 * drop any leading and trailing underscores, and collapses a sequence of
 * multiple underscores into one underscore.
 *
 * Visible for testing */
void format_metric_family_name(strbuf_t *buf, metric_family_t const *fam) {
  size_t name_len = strlen(fam->name);
  char name[name_len + 1];
  memset(name, 0, sizeof(name));

  strbuf_t namebuf = STRBUF_CREATE_FIXED(name, sizeof(name));
  strbuf_print_restricted(&namebuf, fam->name, VALID_NAME_CHARS, '_');
  STRBUF_DESTROY(namebuf);

  bool skip_underscore = true;
  size_t out = 0;
  for (size_t in = 0; in < name_len; in++) {
    if (skip_underscore && name[in] == '_') {
      continue;
    }
    skip_underscore = (name[in] == '_');
    name[out] = name[in];
    out++;
  }
  name_len = out;
  name[name_len] = 0;

  while (name_len > 0 && name[name_len - 1] == '_') {
    name_len--;
    name[name_len] = 0;
  }

  strbuf_print(buf, name);

  if (fam->type == METRIC_TYPE_COUNTER) {
    strbuf_print(buf, "_total");
  }
}

/* visible for testing */
void format_metric_family(strbuf_t *buf, metric_family_t const *prom_fam) {
  if (prom_fam->metric.num == 0)
    return;

  char *type = NULL;
  switch (prom_fam->type) {
  case METRIC_TYPE_GAUGE:
    type = "gauge";
    break;
  case METRIC_TYPE_COUNTER:
    type = "counter";
    break;
  case METRIC_TYPE_UNTYPED:
    type = "untyped";
    break;
  }
  if (type == NULL) {
    return;
  }

  strbuf_t family_name = STRBUF_CREATE;
  format_metric_family_name(&family_name, prom_fam);

  if (prom_fam->help == NULL)
    strbuf_printf(buf, "# HELP %s\n", family_name.ptr);
  else
    strbuf_printf(buf, "# HELP %s %s\n", family_name.ptr, prom_fam->help);
  strbuf_printf(buf, "# TYPE %s %s\n", family_name.ptr, type);

  char const *job = label_set_get(prom_fam->resource, "service.name");
  char const *instance =
      label_set_get(prom_fam->resource, "service.instance.id");

  for (size_t i = 0; i < prom_fam->metric.num; i++) {
    metric_t *m = &prom_fam->metric.ptr[i];

    format_metric(buf, m, family_name.ptr, job, instance);

    if (prom_fam->type == METRIC_TYPE_COUNTER)
      strbuf_printf(buf, " %" PRIu64, m->value.counter);
    else
      strbuf_printf(buf, " " GAUGE_FORMAT, m->value.gauge);

    if (m->time > 0) {
      strbuf_printf(buf, " %" PRIi64 "\n", CDTIME_T_TO_MS(m->time));
    } else {
      strbuf_printf(buf, "\n");
    }
  }
  STRBUF_DESTROY(family_name);

  strbuf_printf(buf, "\n");
}

typedef struct {
  label_set_t *resources;
  size_t resources_num;
} target_info_t;

static int target_info_compare(void const *a, void const *b) {
  label_set_t const *lsa = a;
  label_set_t const *lsb = b;

  return label_set_compare(*lsa, *lsb);
}

static int target_info_add(target_info_t *ti, label_set_t resource) {
  label_set_t *found = bsearch(&resource, ti->resources, ti->resources_num,
                               sizeof(*ti->resources), target_info_compare);
  if (found != NULL) {
    return 0;
  }

  label_set_t *ls =
      realloc(ti->resources, sizeof(*ti->resources) * (ti->resources_num + 1));
  if (ls == NULL) {
    ERROR("write_prometheus plugin: realloc failed.");
    return ENOMEM;
  }
  ti->resources = ls;

  ls = ti->resources + ti->resources_num;
  memset(ls, 0, sizeof(*ls));
  int status = label_set_clone(ls, resource);
  if (status != 0) {
    ERROR("write_prometheus plugin: label_set_clone failed.");
    return status;
  }

  ti->resources_num++;
  qsort(ti->resources, ti->resources_num, sizeof(*ti->resources),
        target_info_compare);

  return 0;
}

static void target_info_reset(target_info_t *ti) {
  for (size_t i = 0; i < ti->resources_num; i++) {
    label_set_reset(ti->resources + i);
  }
  free(ti->resources);
  ti->resources = NULL;
  ti->resources_num = 0;
}

/* target_info prints a special "info" metric that contains all the "target
 * labels" aka. resource attributes.
 * See
 * https://github.com/OpenObservability/OpenMetrics/blob/main/specification/OpenMetrics.md#supporting-target-metadata-in-both-push-based-and-pull-based-systems
 * for more details. */
/* visible for testing */
void target_info(strbuf_t *buf, metric_family_t const **families,
                 size_t families_num) {
  target_info_t ti = {0};

  for (size_t i = 0; i < families_num; i++) {
    metric_family_t const *fam = families[i];
    target_info_add(&ti, fam->resource);
  }

  if (ti.resources_num == 0) {
    return;
  }

#ifdef EXPOSE_OPEN_METRICS
  strbuf_print(buf, "# TYPE target info\n");
  strbuf_print(buf, "# HELP target Target metadata\n");
#else
  strbuf_print(buf, "# HELP target_info Target metadata\n");
  strbuf_print(buf, "# TYPE target_info gauge\n");
#endif

  for (size_t i = 0; i < ti.resources_num; i++) {
    label_set_t *resource = ti.resources + i;

    char *job = NULL;
    char *instance = NULL;

    char const *v;
    if ((v = label_set_get(*resource, "service.name")) != NULL) {
      job = strdup(v);
      label_set_update(resource, "service.name", NULL);
    }

    if ((v = label_set_get(*resource, "service.instance.id")) != NULL) {
      instance = strdup(v);
      label_set_update(resource, "service.instance.id", NULL);
    }

    strbuf_print(buf, "target_info{");
    format_label_set(buf, *resource, job, instance);
    strbuf_print(buf, "} 1\n");

    free(job);
    free(instance);
  }

  strbuf_print(buf, "\n");
  target_info_reset(&ti);
}

static void format_metric_families(strbuf_t *buf,
                                   metric_family_t const **families,
                                   size_t families_num) {
  target_info(buf, families, families_num);

  for (size_t i = 0; i < families_num; i++) {
    metric_family_t const *fam = families[i];
    format_metric_family(buf, fam);
  }
}

static void format_text(strbuf_t *buf) {
  pthread_mutex_lock(&prom_metrics_lock);

  size_t families_num = (size_t)c_avl_size(prom_metrics);
  metric_family_t const *families[families_num];
  memset(families, 0, sizeof(families));

  char *unused = NULL;
  metric_family_t *prom_fam = NULL;
  c_avl_iterator_t *iter = c_avl_get_iterator(prom_metrics);
  for (size_t i = 0;
       c_avl_iterator_next(iter, (void *)&unused, (void *)&prom_fam) == 0;
       i++) {
    assert(i < families_num);
    families[i] = prom_fam;
  }
  c_avl_iterator_destroy(iter);

  format_metric_families(buf, families, families_num);

  strbuf_printf(buf, "# collectd/write_prometheus %s at %s\n", PACKAGE_VERSION,
                hostname_g);

  pthread_mutex_unlock(&prom_metrics_lock);
}

/* http_handler is the callback called by the microhttpd library. It essentially
 * handles all HTTP request aspects and creates an HTTP response. */
static MHD_RESULT http_handler(__attribute__((unused)) void *cls,
                               struct MHD_Connection *connection,
                               __attribute__((unused)) const char *url,
                               const char *method,
                               __attribute__((unused)) const char *version,
                               __attribute__((unused)) const char *upload_data,
                               __attribute__((unused)) size_t *upload_data_size,
                               void **connection_state) {
  if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) {
    return MHD_NO;
  }

  /* According to documentation, first call for each connection is after headers
   * have been parsed, and should be used only for reporting errors */
  if (*connection_state == NULL) {
    /* keep track of connection state */
    *connection_state = &"called";
    return MHD_YES;
  }

  strbuf_t buf = STRBUF_CREATE;
  format_text(&buf);
#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090500
  struct MHD_Response *res =
      MHD_create_response_from_buffer(buf.pos, buf.ptr, MHD_RESPMEM_MUST_COPY);
#else
  struct MHD_Response *res = MHD_create_response_from_data(
      buf.pos, buf.ptr, /* must_free = */ 0, /* must_copy = */ 1);
#endif
  STRBUF_DESTROY(buf);

  MHD_add_response_header(res, MHD_HTTP_HEADER_CONTENT_TYPE, CONTENT_TYPE_TEXT);

  MHD_RESULT status = MHD_queue_response(connection, MHD_HTTP_OK, res);

  MHD_destroy_response(res);
  return status;
}

/* metric_cmp compares two metrics. It's prototype makes it easy to use with
 * qsort(3) and bsearch(3). */
static int prom_metric_cmp(void const *a, void const *b) {
  metric_t const *m_a = (metric_t *)a;
  metric_t const *m_b = (metric_t *)b;

  if (m_a->label.num < m_b->label.num)
    return -1;
  else if (m_a->label.num > m_b->label.num)
    return 1;

  for (size_t i = 0; i < m_a->label.num; i++) {
    int status = strcmp(m_a->label.ptr[i].value, m_b->label.ptr[i].value);
    if (status != 0)
      return status;

#if COLLECT_DEBUG
    assert(strcmp(m_a->label.ptr[i].name, m_b->label.ptr[i].name) == 0);
#endif
  }
  return 0;
}

static int prom_metric_family_metric_delete(metric_family_t *fam,
                                            metric_t const *m) {
  if ((fam == NULL) || (m == NULL)) {
    return EINVAL;
  }
  size_t i;
  for (i = 0; i < fam->metric.num; i++) {
    if (prom_metric_cmp(m, &fam->metric.ptr[i]) == 0)
      break;
  }

  if (i >= fam->metric.num)
    return ENOENT;

  metric_reset(&fam->metric.ptr[i]);

  if ((fam->metric.num - 1) > i)
    memmove(&fam->metric.ptr[i], &fam->metric.ptr[i + 1],
            ((fam->metric.num - 1) - i) * sizeof(fam->metric.ptr[i]));

  fam->metric.num--;

  if (fam->metric.num == 0) {
    sfree(fam->metric.ptr);
    fam->metric.ptr = NULL;
    return 0;
  }

  metric_t *tmp =
      realloc(fam->metric.ptr, fam->metric.num * sizeof(*fam->metric.ptr));
  if (tmp != NULL)
    fam->metric.ptr = tmp;

  return 0;
}

static void prom_logger(__attribute__((unused)) void *arg, char const *fmt,
                        va_list ap) {
  /* {{{ */
  char errbuf[1024];
  vsnprintf(errbuf, sizeof(errbuf), fmt, ap);

  ERROR("write_prometheus plugin: %s", errbuf);
} /* }}} prom_logger */

#if MHD_VERSION >= 0x00090000
static int prom_open_socket(int addrfamily, const char **failed) {
  /* {{{ */
  char service[NI_MAXSERV];
  ssnprintf(service, sizeof(service), "%hu", httpd_port);

  struct addrinfo *res;
  int status = getaddrinfo(httpd_host, service,
                           &(struct addrinfo){
                               .ai_flags = AI_PASSIVE | AI_ADDRCONFIG,
                               .ai_family = addrfamily,
                               .ai_socktype = SOCK_STREAM,
                           },
                           &res);
  if (status != 0) {
    *failed = "getaddrinfo()";
    return -1;
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
    int flags = ai->ai_socktype;
#ifdef SOCK_CLOEXEC
    flags |= SOCK_CLOEXEC;
#endif

    fd = socket(ai->ai_family, flags, 0);
    if (fd == -1) {
      *failed = "socket()";
      continue;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
      *failed = "setsockopt(SO_REUSEADDR)";
      close(fd);
      fd = -1;
      continue;
    }

    if (bind(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
      *failed = "bind()";
      close(fd);
      fd = -1;
      continue;
    }

    if (listen(fd, /* backlog = */ 16) != 0) {
      *failed = "listen()";
      close(fd);
      fd = -1;
      continue;
    }

    char str_node[NI_MAXHOST];
    char str_service[NI_MAXSERV];

    getnameinfo(ai->ai_addr, ai->ai_addrlen, str_node, sizeof(str_node),
                str_service, sizeof(str_service),
                NI_NUMERICHOST | NI_NUMERICSERV);

    INFO("write_prometheus plugin: Listening on [%s]:%s.", str_node,
         str_service);
    break;
  }

  freeaddrinfo(res);

  return fd;
} /* }}} int prom_open_socket */

static struct MHD_Daemon *prom_start_daemon() {
  /* {{{ */
  const char *failed = "(unknown)";
  int fd = prom_open_socket(PF_INET6, &failed);
  if (fd == -1)
    fd = prom_open_socket(PF_INET, &failed);
  if (fd == -1) {
    ERROR("write_prometheus plugin: Opening a listening socket for [%s]:%hu "
          "failed in %s.",
          (httpd_host != NULL) ? httpd_host : "::", httpd_port, failed);
    return NULL;
  }

  unsigned int flags = MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG;
#if MHD_VERSION >= 0x00095300
  flags |= MHD_USE_INTERNAL_POLLING_THREAD;
#endif

  struct MHD_Daemon *d = MHD_start_daemon(
      flags, httpd_port,
      /* MHD_AcceptPolicyCallback = */ NULL,
      /* MHD_AcceptPolicyCallback arg = */ NULL, http_handler, NULL,
      MHD_OPTION_LISTEN_SOCKET, fd, MHD_OPTION_EXTERNAL_LOGGER, prom_logger,
      NULL, MHD_OPTION_END);
  if (d == NULL) {
    ERROR("write_prometheus plugin: MHD_start_daemon() failed.");
    close(fd);
    return NULL;
  }

  return d;
} /* }}} struct MHD_Daemon *prom_start_daemon */
#else /* if MHD_VERSION < 0x00090000 */
static struct MHD_Daemon *prom_start_daemon() {
  /* {{{ */
  struct MHD_Daemon *d = MHD_start_daemon(
      MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG, httpd_port,
      /* MHD_AcceptPolicyCallback = */ NULL,
      /* MHD_AcceptPolicyCallback arg = */ NULL, http_handler, NULL,
      MHD_OPTION_EXTERNAL_LOGGER, prom_logger, NULL, MHD_OPTION_END);
  if (d == NULL) {
    ERROR("write_prometheus plugin: MHD_start_daemon() failed.");
    return NULL;
  }

  return d;
} /* }}} struct MHD_Daemon *prom_start_daemon */
#endif

/*
 * collectd callbacks
 */
static int prom_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0) {
#if MHD_VERSION >= 0x00090000
      cf_util_get_string(child, &httpd_host);
#else
      ERROR("write_prometheus plugin: Option `Host' not supported. Please "
            "upgrade libmicrohttpd to at least 0.9.0");
      return -1;
#endif
    } else if (strcasecmp("Port", child->key) == 0) {
      int status = cf_util_get_port_number(child);
      if (status > 0)
        httpd_port = (unsigned short)status;
    } else if (strcasecmp("StalenessDelta", child->key) == 0) {
      cf_util_get_cdtime(child, &staleness_delta);
    } else {
      WARNING("write_prometheus plugin: Ignoring unknown configuration option "
              "\"%s\".",
              child->key);
    }
  }

  return 0;
}

static int prom_init() {
  if (prom_metrics == NULL) {
    prom_metrics = c_avl_create((void *)strcmp);
    if (prom_metrics == NULL) {
      ERROR("write_prometheus plugin: c_avl_create() failed.");
      return -1;
    }
  }

  if (httpd == NULL) {
    httpd = prom_start_daemon();
    if (httpd == NULL) {
      return -1;
    }
    DEBUG("write_prometheus plugin: Successfully started microhttpd %s",
          MHD_get_version());
  }

  return 0;
}

static int prom_write(metric_family_t const *fam,
                      __attribute__((unused)) user_data_t *ud) {
  pthread_mutex_lock(&prom_metrics_lock);

  metric_family_t *prom_fam = NULL;
  if (c_avl_get(prom_metrics, fam->name, (void *)&prom_fam) != 0) {
    prom_fam = metric_family_clone(fam);
    if (prom_fam == NULL) {
      ERROR("write_prometheus plugin: Clone metric \"%s\" failed.", fam->name);
      pthread_mutex_unlock(&prom_metrics_lock);
      return -1;
    }
    /* Sort the metrics so that lookup is fast. */
    qsort(prom_fam->metric.ptr, prom_fam->metric.num,
          sizeof(*prom_fam->metric.ptr), prom_metric_cmp);

    int status = c_avl_insert(prom_metrics, prom_fam->name, prom_fam);
    if (status != 0) {
      ERROR("write_prometheus plugin: Adding \"%s\" failed.", prom_fam->name);
      metric_family_free(prom_fam);
      pthread_mutex_unlock(&prom_metrics_lock);
      return -1;
    }

    pthread_mutex_unlock(&prom_metrics_lock);
    return 0;
  }

  for (size_t i = 0; i < fam->metric.num; i++) {
    metric_t const *m = &fam->metric.ptr[i];

    metric_t *mmatch = bsearch(m, prom_fam->metric.ptr, prom_fam->metric.num,
                               sizeof(*prom_fam->metric.ptr), prom_metric_cmp);
    if (mmatch == NULL) {
      metric_family_metric_append(prom_fam, *m);
      /* Sort the metrics so that lookup is fast. */
      qsort(prom_fam->metric.ptr, prom_fam->metric.num,
            sizeof(*prom_fam->metric.ptr), prom_metric_cmp);
      continue;
    }

    mmatch->value = m->value;

    /* Prometheus has a globally configured timeout after which metrics are
     * considered stale. This causes problems when metrics have an interval
     * exceeding that limit. We emulate the behavior of "pushgateway" and *not*
     * send a timestamp value – Prometheus will fill in the current time. */
    if (m->interval > staleness_delta) {
      static c_complain_t long_metric = C_COMPLAIN_INIT_STATIC;
      c_complain(LOG_NOTICE, &long_metric,
                 "write_prometheus plugin: You have metrics with an interval "
                 "exceeding \"StalenessDelta\" setting (%.3fs). This is "
                 "suboptimal, please check the collectd.conf(5) manual page to "
                 "understand what's going on.",
                 CDTIME_T_TO_DOUBLE(staleness_delta));

      mmatch->time = 0;
    } else {
      mmatch->time = m->time;
    }
  }

  pthread_mutex_unlock(&prom_metrics_lock);
  return 0;
}

static int prom_missing(metric_family_t const *fam,
                        __attribute__((unused)) user_data_t *ud) {

  pthread_mutex_lock(&prom_metrics_lock);

  metric_family_t *prom_fam = NULL;
  if (c_avl_get(prom_metrics, fam->name, (void *)&prom_fam) != 0)
    return 0;

  for (size_t i = 0; i < fam->metric.num; i++) {
    metric_t const *m = &fam->metric.ptr[i];

    metric_t *mmatch = bsearch(m, prom_fam->metric.ptr, prom_fam->metric.num,
                               sizeof(*prom_fam->metric.ptr), prom_metric_cmp);
    if (mmatch == NULL)
      continue;

    int status = prom_metric_family_metric_delete(prom_fam, m);
    if (status != 0) {
      ERROR("write_prometheus plugin: Deleting a metric in family \"%s\" "
            "failed with status %d",
            fam->name, status);
      continue;
    }

    if (prom_fam->metric.num == 0) {
      int status = c_avl_remove(prom_metrics, prom_fam->name, NULL, NULL);
      if (status != 0) {
        ERROR("write_prometheus plugin: Deleting metric family \"%s\" failed "
              "with status %d",
              prom_fam->name, status);
        continue;
      }
      metric_family_free(prom_fam);
      break;
    }
  }

  pthread_mutex_unlock(&prom_metrics_lock);
  return 0;
}

static int prom_shutdown() {
  if (httpd != NULL) {
    MHD_stop_daemon(httpd);
    httpd = NULL;
  }

  pthread_mutex_lock(&prom_metrics_lock);
  if (prom_metrics != NULL) {
    char *name;
    metric_family_t *prom_fam;
    while (c_avl_pick(prom_metrics, (void *)&name, (void *)&prom_fam) == 0) {
      assert(name == prom_fam->name);
      name = NULL;
      metric_family_free(prom_fam);
    }
    c_avl_destroy(prom_metrics);
    prom_metrics = NULL;
  }
  pthread_mutex_unlock(&prom_metrics_lock);

  sfree(httpd_host);
  return 0;
}

void module_register() {
  plugin_register_complex_config("write_prometheus", prom_config);
  plugin_register_init("write_prometheus", prom_init);
  plugin_register_write("write_prometheus", prom_write, /* user data = */ NULL);
  plugin_register_missing("write_prometheus", prom_missing,
                          /* user data = */ NULL);
  plugin_register_shutdown("write_prometheus", prom_shutdown);
}
