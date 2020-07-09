/**
 * collectd - src/utils_format_kairosdb.c
 * Copyright (C) 2016       Aurelien beorn Rougemont
 * Copyright (C) 2020       Florian Forster
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
 *   Aurelien beorn Rougemont <beorn at gandi dot net>
 *   Manoj Srivastava <srivasta at google.com>
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "utils/format_kairosdb/format_kairosdb.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils_cache.h"

#if HAVE_LIBYAJL
#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif
#endif

/* This is the KAIROSDB format for write_http output
 *
 * Target format
 * [
 *   {
 *     "name":"cpu_usage"
 *     "timestamp": 1453897164060,
 *     "value": 97.1,
 *     "ttl": 300,
 *     "tags": {
 *          "instance": "example.com",
 *          "cpu":      "0",
 *          "state":    "idle"
 *     }
 *   }
 * ]
 */
static int json_add_string(yajl_gen g, char const *str) /* {{{ */
{
  if (str == NULL)
    return (int)yajl_gen_null(g);

  return (int)yajl_gen_string(g, (const unsigned char *)str,
                              (unsigned int)strlen(str));
} /* }}} int json_add_string */

#define CHECK(f)                                                               \
  do {                                                                         \
    int status = (f);                                                          \
    if (status != 0) {                                                         \
      ERROR("format_kairosdb: %s failed with status %d", #f, status);                           \
      return status;                                                           \
    }                                                                          \
  } while (0)

static int json_add_value(yajl_gen g, metric_t const *m,
                          format_kairosdb_opts_t const *opts) {
  if ((m->family->type == METRIC_TYPE_GAUGE) ||
      (m->family->type == METRIC_TYPE_UNTYPED)) {
    double v = m->value.gauge;
    if (isfinite(v)) {
      CHECK(yajl_gen_double(g, v));
    } else {
      CHECK(yajl_gen_null(g));
    }
  } else if ((opts != NULL) && opts->store_rates) {
    gauge_t rate = NAN;
    uc_get_rate(m, &rate);

    if (isfinite(rate)) {
      CHECK(yajl_gen_double(g, rate));
    } else {
      CHECK(yajl_gen_null(g));
    }
  } else if (m->family->type == METRIC_TYPE_COUNTER) {
    CHECK(yajl_gen_integer(g, (long long int)m->value.counter));
  } else {
    ERROR("format_kairosdb: Unknown data source type: %d", m->family->type);
    CHECK(yajl_gen_null(g));
  }

  return 0;
}

static int json_add_metric(yajl_gen g, metric_t const *m,
                           format_kairosdb_opts_t const *opts) {
  CHECK(yajl_gen_map_open(g));

  CHECK(json_add_string(g, "name"));
  if ((opts != NULL) && (opts->metrics_prefix != NULL)) {
    strbuf_t buf = STRBUF_CREATE;
    strbuf_print(&buf, opts->metrics_prefix);
    strbuf_print(&buf, m->family->name);
    CHECK(json_add_string(g, buf.ptr));
    STRBUF_DESTROY(buf);
  } else {
    CHECK(json_add_string(g, m->family->name));
  }

  CHECK(json_add_string(g, "timestamp"));
  CHECK(yajl_gen_integer(g, (long long int)CDTIME_T_TO_MS(m->time)));

  CHECK(json_add_string(g, "value"));
  CHECK(json_add_value(g, m, opts));

  if ((opts != NULL) && (opts->ttl_secs != 0)) {
    CHECK(json_add_string(g, "ttl"));
    CHECK(yajl_gen_integer(g, (long long int)opts->ttl_secs));
  }

  if (m->label.num != 0) {
    CHECK(json_add_string(g, "tags"));
    CHECK(yajl_gen_map_open(g));

    for (size_t i = 0; i < m->label.num; i++) {
      label_pair_t *l = m->label.ptr + i;
      CHECK(json_add_string(g, l->name));
      CHECK(json_add_string(g, l->value));
    }

    CHECK(yajl_gen_map_close(g));
  }

  CHECK(yajl_gen_map_close(g));
  return 0;
}

static int json_metric_family(yajl_gen g, metric_family_t const *fam,
                              format_kairosdb_opts_t const *opts) {
  CHECK(yajl_gen_array_open(g));

  for (size_t i = 0; i < fam->metric.num; i++) {
    metric_t const *m = fam->metric.ptr + i;
    CHECK(json_add_metric(g, m, opts));
  }

  CHECK(yajl_gen_array_close(g));
  return 0;
}

int format_kairosdb_metric_family(strbuf_t *buf, metric_family_t const *fam,
                                  format_kairosdb_opts_t const *opts) {
  if ((buf == NULL) || (fam == NULL))
    return EINVAL;

#if HAVE_YAJL_V2
  yajl_gen g = yajl_gen_alloc(NULL);
  if (g == NULL)
    return ENOMEM;
#if COLLECT_DEBUG
  yajl_gen_config(g, yajl_gen_validate_utf8, 1);
#endif

#else /* !HAVE_YAJL_V2 */
  yajl_gen_config conf = {0};
  yajl_gen g = yajl_gen_alloc(&conf, NULL);
  if (g == NULL)
    return ENOMEM;
#endif

  int status = json_metric_family(g, fam, opts);
  if (status != 0) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return status;
  }

  /* copy to output buffer */
  unsigned char const *out = NULL;
#if HAVE_YAJL_V2
  size_t out_len = 0;
#else
  unsigned int out_len = 0;
#endif
  yajl_gen_status yajl_status = yajl_gen_get_buf(g, &out, &out_len);
  if (yajl_status != yajl_gen_status_ok) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return (int)yajl_status;
  }

  if (buf->fixed) {
    size_t avail = (buf->size == 0) ? 0 : buf->size - (buf->pos + 1);
    if (avail < out_len) {
      yajl_gen_clear(g);
      yajl_gen_free(g);
      return ENOBUFS;
    }
  }

  /* If the buffer is not empty, append by converting the closing ']' of "buf"
   * to a comma and skip the opening '[' of "out". */
  if (buf->pos != 0) {
    assert(buf->ptr[buf->pos - 1] == ']');
    buf->ptr[buf->pos - 1] = ',';

    assert(out[0] == '[');
    out++;
  }

  status = strbuf_print(buf, (void *)out);

  yajl_gen_clear(g);
  yajl_gen_free(g);
  return status;
} /* }}} format_kairosdb_metric_family */
