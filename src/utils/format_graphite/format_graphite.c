/**
 * collectd - src/utils_format_graphite.c
 * Copyright (C) 2012  Thomas Meson
 * Copyright (C) 2012  Florian octo Forster
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
 *   Thomas Meson <zllak at hycik.org>
 *   Florian octo Forster <octo at collectd.org>
 *   Manoj Srivastava <srivasta at google.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils/format_graphite/format_graphite.h"
#include "utils_cache.h"

#define GRAPHITE_FORBIDDEN ". \t\"\\:!,/()\n\r"

/* Utils functions to format data sets in graphite format.
 * Largely taken from write_graphite.c as it remains the same formatting */

static int gr_format_values(strbuf_t *buf, metric_t const *m, gauge_t rate,
                            bool store_rate) {
  if (!store_rate && ((m->family->type == METRIC_TYPE_GAUGE) ||
                      (m->family->type == METRIC_TYPE_UNTYPED))) {
    rate = m->value.gauge;
    store_rate = true;
  }

  if (store_rate) {
    if (isnan(rate)) {
      return strbuf_print(buf, "nan");
    } else {
      return strbuf_printf(buf, GAUGE_FORMAT, m->value.gauge);
    }
  } else if (m->family->type == METRIC_TYPE_COUNTER) {
    return strbuf_printf(buf, "%" PRIu64, (uint64_t)m->value.counter);
  }

  P_ERROR("gr_format_values: Unknown data source type: %d", m->family->type);
  return EINVAL;
}

static int graphite_print_escaped(strbuf_t *buf, char const *s,
                                  char escape_char) {
  if ((buf == NULL) || (s == NULL)) {
    return EINVAL;
  }

  size_t s_len = strlen(s);
  while (s_len > 0) {
    size_t valid_len = strcspn(s, GRAPHITE_FORBIDDEN);
    if (valid_len == s_len) {
      return strbuf_print(buf, s);
    }
    if (valid_len != 0) {
      char tmp[valid_len + 1];
      strncpy(tmp, s, valid_len);
      tmp[valid_len] = 0;
      int status = strbuf_print(buf, tmp);
      if (status != 0) {
        return status;
      }

      s += valid_len;
      s_len -= valid_len;
      continue;
    }

    char tmp[2] = {escape_char, 0};
    int status = strbuf_print(buf, tmp);
    if (status != 0) {
      return status;
    }

    s++;
    s_len--;
  }

  return 0;
}

static void gr_format_label_set(strbuf_t *buf, label_set_t const *labels,
                                char const escape_char, unsigned int flags) {
  for (size_t i = 0; i < labels->num; i++) {
    label_pair_t *l = labels->ptr + i;
    strbuf_print(buf, ".");
    graphite_print_escaped(buf, l->name, escape_char);
    strbuf_print(buf, (flags & GRAPHITE_SEPARATE_INSTANCES) ? "." : "=");
    graphite_print_escaped(buf, l->value, escape_char);
  }
}

static void gr_format_name(strbuf_t *buf, metric_t const *m, char const *prefix,
                           char const *suffix, char const escape_char,
                           unsigned int flags) {
  if (prefix != NULL) {
    strbuf_print(buf, prefix);
  }
  graphite_print_escaped(buf, m->family->name, escape_char);
  if (suffix != NULL) {
    strbuf_print(buf, suffix);
  }

  gr_format_label_set(buf, &m->family->resource, escape_char, flags);
  gr_format_label_set(buf, &m->label, escape_char, flags);
}

int format_graphite(strbuf_t *buf, metric_t const *m, char const *prefix,
                    char const *postfix, char const escape_char,
                    unsigned int flags) {
  gauge_t rate = NAN;
  bool store_rate = false;
  if (flags & GRAPHITE_STORE_RATES) {
    int status = uc_get_rate(m, &rate);
    if (status != 0) {
      P_ERROR("format_graphite: error with uc_get_rate");
      return -1;
    }
    store_rate = true;
  }

  /* format:
   * <name> <value> <time> '\r' '\n'
   */

  gr_format_name(buf, m, prefix, postfix, escape_char, flags);

  strbuf_print(buf, " ");

  /* Convert the values to an ASCII representation and put that into
   * `values'. */
  int status = gr_format_values(buf, m, rate, store_rate);
  if (status != 0) {
    P_ERROR("format_graphite: error with gr_format_values");
    return status;
  }

  return strbuf_printf(buf, " %lu\r\n",
                       (unsigned long)CDTIME_T_TO_TIME_T(m->time));
} /* int format_graphite */
