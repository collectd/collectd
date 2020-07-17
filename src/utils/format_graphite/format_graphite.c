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
#include "utils/common/common.h"
#include "utils/avltree/avltree.h"

#include "utils/format_graphite/format_graphite.h"
#include "utils_cache.h"

#define GRAPHITE_FORBIDDEN " \t\"\\:!,/()\n\r"

/* Utils functions to format data sets in graphite format.
 * Largely taken from write_graphite.c as it remains the same formatting */

static int gr_format_values(char *ret, size_t ret_len, const metric_t *metric_p,
                            gauge_t rate) {
  size_t offset = 0;
  int status;

  memset(ret, 0, ret_len);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(ret + offset, ret_len - offset, __VA_ARGS__);            \
    if (status < 1) {                                                          \
      return -1;                                                               \
    } else if (((size_t)status) >= (ret_len - offset)) {                       \
      return -1;                                                               \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

  if (metric_p->value_type == DS_TYPE_GAUGE)
    BUFFER_ADD(GAUGE_FORMAT, metric_p->value.gauge);
  else if (rate != -1)
    BUFFER_ADD("%f", rate);
  else if (metric_p->value_type == DS_TYPE_COUNTER)
    BUFFER_ADD("%" PRIu64, (uint64_t)metric_p->value.counter);
  else if (metric_p->value_type == DS_TYPE_DERIVE)
    BUFFER_ADD("%" PRIi64, metric_p->value.derive);
  else {
    P_ERROR("gr_format_values: Unknown data source type: %i",
            metric_p->value_type);
    return -1;
  }

#undef BUFFER_ADD

  return 0;
}

static void gr_copy_escape_part(char *dst, const char *src, size_t dst_len,
                                char escape_char, bool preserve_separator) {
  if (src == NULL)
    return;

  for (size_t i = 0; i < dst_len; i++) {
    if (src[i] == 0) {
      dst[i] = 0;
      break;
    }

    if ((!preserve_separator && (src[i] == '.')) || ((src[i] == '/')) ||
        isspace((int)src[i]) || iscntrl((int)src[i]))
      dst[i] = escape_char;
    else
      dst[i] = src[i];
  }
}

static int gr_format_name_tagged(char *ret, int ret_len,
                                 metric_t const *metric_p, char const *prefix,
                                 char const *postfix, char const escape_char,
                                 unsigned int flags) {
  int starting_len = ret_len;
  if (prefix == NULL)
    prefix = "";

  if (postfix == NULL)
    postfix = "";

  memset(ret, 0, ret_len);

  int tmp_str_len = 0;
  tmp_str_len = strlen(prefix);
  if (tmp_str_len < ret_len) {
    snprintf(ret, tmp_str_len + 1, "%s", prefix);
    ret += tmp_str_len; /* This is the location of the trailing nul */
    ret_len -= tmp_str_len;
  } else {
    snprintf(ret, ret_len, "%s", prefix);
    return starting_len;
  }

  tmp_str_len = strlen(metric_p->identity->name);
  if (tmp_str_len < ret_len) {
    gr_copy_escape_part(ret, metric_p->identity->name, tmp_str_len + 1,
                        escape_char, 1);
    ret += tmp_str_len;
    ret_len -= tmp_str_len;
  } else {
    gr_copy_escape_part(ret, metric_p->identity->name, ret_len, escape_char, 1);
    return starting_len;
  }

  tmp_str_len = strlen(postfix);
  if (tmp_str_len < ret_len) {
    snprintf(ret, tmp_str_len + 1, "%s", postfix);
    ret += tmp_str_len; /* This is the location of the trailing nul */
    ret_len -= tmp_str_len;
  } else {
    snprintf(ret, ret_len, "%s", postfix);
    return starting_len;
  }

  if (metric_p->identity->root_p != NULL) {
    c_avl_iterator_t *iter_p = c_avl_get_iterator(metric_p->identity->root_p);
    if (iter_p != NULL) {
      char *key_p = NULL;
      char *value_p = NULL;
      while ((c_avl_iterator_next(iter_p, (void **)&key_p,
                                  (void **)&value_p)) == 0) {
        if ((key_p != NULL) && (value_p != NULL)) {
          tmp_str_len = strlen(key_p) + strlen(value_p) + 2;
          if (tmp_str_len < ret_len) {
            snprintf(ret, tmp_str_len + 1, ";%s=%s", key_p, value_p);
            ret += tmp_str_len;
            ret_len -= tmp_str_len;
          } else {
            snprintf(ret, ret_len, ";%s=%s", key_p, value_p);
            return starting_len;
          }
        }
      }
      c_avl_iterator_destroy(iter_p);
    }
  }

  return starting_len - ret_len; /* Characters appended */
}

static int gr_format_name(char *ret, int ret_len, metric_t const *metric_p,
                          char const *prefix, char const *postfix,
                          char const escape_char, unsigned int flags) {
  int starting_len = ret_len;
  if (prefix == NULL)
    prefix = "";

  if (postfix == NULL)
    postfix = "";

  memset(ret, 0, ret_len);

  int tmp_str_len = 0;
  tmp_str_len = strlen(prefix);
  if (tmp_str_len < ret_len) {
    snprintf(ret, tmp_str_len + 1, "%s", prefix);
    ret += tmp_str_len; /* This is the location of the trailing nul */
    ret_len -= tmp_str_len;
  } else {
    snprintf(ret, ret_len, "%s", prefix);
    return starting_len;
  }

  tmp_str_len = strlen(metric_p->identity->name);
  if (tmp_str_len < ret_len) {
    gr_copy_escape_part(ret, metric_p->identity->name, tmp_str_len + 1,
                        escape_char, 1);
    ret += tmp_str_len;
    ret_len -= tmp_str_len;
  } else {
    gr_copy_escape_part(ret, metric_p->identity->name, ret_len, escape_char, 1);
    return starting_len;
  }

  tmp_str_len = strlen(postfix);
  if (tmp_str_len < ret_len) {
    snprintf(ret, tmp_str_len + 1, "%s", postfix);
    ret += tmp_str_len; /* This is the location of the trailing nul */
    ret_len -= tmp_str_len;
  } else {
    snprintf(ret, ret_len, "%s", postfix);
    return starting_len;
  }

  if (metric_p->identity->root_p != NULL) {
    c_avl_iterator_t *iter_p = c_avl_get_iterator(metric_p->identity->root_p);
    if (iter_p != NULL) {
      char *key_p = NULL;
      char *value_p = NULL;
      while ((c_avl_iterator_next(iter_p, (void **)&key_p,
                                  (void **)&value_p)) == 0) {
        if ((key_p != NULL) && (value_p != NULL)) {
          tmp_str_len = strlen(value_p) + 1;
          if (tmp_str_len < ret_len) {
            snprintf(ret, tmp_str_len + 1, ";%s", value_p);
            ret += tmp_str_len;
            ret_len -= tmp_str_len;
          } else {
            snprintf(ret, ret_len, ";%s", value_p);
            return starting_len;
          }
        }
      }
      c_avl_iterator_destroy(iter_p);
    }
  }

  return starting_len - ret_len; /* Characters appended */
}

static void escape_graphite_string(char *buffer, char escape_char) {
  assert(strchr(GRAPHITE_FORBIDDEN, escape_char) == NULL);

  for (char *head = buffer + strcspn(buffer, GRAPHITE_FORBIDDEN); *head != '\0';
       head += strcspn(head, GRAPHITE_FORBIDDEN))
    *head = escape_char;
}

int format_graphite(char *buffer, size_t buffer_size, metric_t const *metric_p,
                    char const *prefix, char const *postfix,
                    char const escape_char, unsigned int flags) {
  int status = 0;
  int buffer_pos = 0;

  gauge_t rate = -1;
  if (flags & GRAPHITE_STORE_RATES) {
    status = uc_get_rate(metric_p, &rate);
    if (status != 0) {
      P_ERROR("format_graphite: error with uc_get_rate");
      return -1;
    }
  }

  char key[10 * DATA_MAX_NAME_LEN];
  char values[512];
  size_t message_len;
  char message[1024];

  /* Copy the identifier to `key' and escape it. */
  if (flags & GRAPHITE_USE_TAGS) {
    status = gr_format_name_tagged(key, sizeof(key), metric_p, prefix, postfix,
                                   escape_char, flags);
    if (status != 0) {
      P_ERROR("format_graphite: error with gr_format_name_tagged");
      return status;
    }
  } else {
    status = gr_format_name(key, sizeof(key), metric_p, prefix, postfix,
                            escape_char, flags);
    if (status != 0) {
      P_ERROR("format_graphite: error with gr_format_name");
      return status;
    }
  }

  escape_graphite_string(key, escape_char);

  /* Convert the values to an ASCII representation and put that into
   * `values'. */
  status = gr_format_values(values, sizeof(values), metric_p, rate);
  if (status != 0) {
    P_ERROR("format_graphite: error with gr_format_values");
    return status;
  }

  /* Compute the graphite command */
  message_len =
      (size_t)snprintf(message, sizeof(message), "%s %s %u\r\n", key, values,
                       (unsigned int)CDTIME_T_TO_TIME_T(metric_p->time));
  if (message_len >= sizeof(message)) {
    P_ERROR("format_graphite: message buffer too small: "
            "Need %" PRIsz " bytes.",
            message_len + 1);
    return -ENOMEM;
  }

  /* Append it in case we got multiple data set */
  if ((buffer_pos + message_len) >= buffer_size) {
    P_ERROR("format_graphite: target buffer too small");
    return -ENOMEM;
  }
  memcpy((void *)(buffer + buffer_pos), message, message_len);
  buffer_pos += message_len;
  buffer[buffer_pos] = '\0';

  return status;
} /* int format_graphite */
