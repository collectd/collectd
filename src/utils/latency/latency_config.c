/**
 * collectd - src/utils_latency_config.c
 * Copyright (C) 2013-2016   Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 *   Pavel Rochnyack <pavel2000 at ngs.ru>
 */

#include "collectd.h"
#include "utils/common/common.h"
#include "utils/latency/latency_config.h"

static int latency_config_add_percentile(latency_config_t *conf,
                                         oconfig_item_t *ci) {
  double percent;
  int status = cf_util_get_double(ci, &percent);
  if (status != 0)
    return status;

  if ((percent <= 0.0) || (percent >= 100)) {
    P_ERROR("The value for \"%s\" must be between 0 and 100, "
            "exclusively.",
            ci->key);
    return ERANGE;
  }

  double *tmp = realloc(conf->percentile,
                        sizeof(*conf->percentile) * (conf->percentile_num + 1));
  if (tmp == NULL) {
    P_ERROR("realloc failed.");
    return ENOMEM;
  }
  conf->percentile = tmp;
  conf->percentile[conf->percentile_num] = percent;
  conf->percentile_num++;

  return 0;
} /* int latency_config_add_percentile */

static int latency_config_add_bucket(latency_config_t *conf,
                                     oconfig_item_t *ci) {
  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_NUMBER) ||
      (ci->values[1].type != OCONFIG_TYPE_NUMBER)) {
    P_ERROR("\"%s\" requires exactly two numeric arguments.", ci->key);
    return EINVAL;
  }

  if (ci->values[1].value.number &&
      ci->values[1].value.number <= ci->values[0].value.number) {
    P_ERROR("MIN must be less than MAX in \"%s\".", ci->key);
    return ERANGE;
  }

  if (ci->values[0].value.number < 0) {
    P_ERROR("MIN must be greater then or equal to zero in \"%s\".", ci->key);
    return ERANGE;
  }

  latency_bucket_t *tmp =
      realloc(conf->buckets, sizeof(*conf->buckets) * (conf->buckets_num + 1));
  if (tmp == NULL) {
    P_ERROR("realloc failed.");
    return ENOMEM;
  }
  conf->buckets = tmp;
  conf->buckets[conf->buckets_num].lower_bound =
      DOUBLE_TO_CDTIME_T(ci->values[0].value.number);
  conf->buckets[conf->buckets_num].upper_bound =
      DOUBLE_TO_CDTIME_T(ci->values[1].value.number);
  conf->buckets_num++;

  return 0;
} /* int latency_config_add_bucket */

int latency_config(latency_config_t *conf, oconfig_item_t *ci) {
  int status = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Percentile", child->key) == 0)
      status = latency_config_add_percentile(conf, child);
    else if (strcasecmp("Bucket", child->key) == 0)
      status = latency_config_add_bucket(conf, child);
    else if (strcasecmp("BucketType", child->key) == 0)
      status = cf_util_get_string(child, &conf->bucket_type);
    else
      P_WARNING("\"%s\" is not a valid option within a \"%s\" block.",
                child->key, ci->key);

    if (status != 0)
      return status;
  }

  if ((status == 0) && (conf->percentile_num == 0) &&
      (conf->buckets_num == 0)) {
    P_ERROR("The \"%s\" block must contain at least one "
            "\"Percentile\" or \"Bucket\" option.",
            ci->key);
    return EINVAL;
  }

  return 0;
}

int latency_config_copy(latency_config_t *dst, const latency_config_t src) {
  *dst = (latency_config_t){
      .percentile_num = src.percentile_num, .buckets_num = src.buckets_num,
  };

  dst->percentile = calloc(dst->percentile_num, sizeof(*dst->percentile));
  dst->buckets = calloc(dst->buckets_num, sizeof(*dst->buckets));

  if ((dst->percentile == NULL) || (dst->buckets == NULL)) {
    latency_config_free(*dst);
    return ENOMEM;
  }

  if (src.bucket_type != NULL) {
    dst->bucket_type = strdup(src.bucket_type);
    if (dst->bucket_type == NULL) {
      latency_config_free(*dst);
      return ENOMEM;
    }
  }

  memmove(dst->percentile, src.percentile,
          dst->percentile_num * sizeof(*dst->percentile));
  memmove(dst->buckets, src.buckets, dst->buckets_num * sizeof(*dst->buckets));

  return 0;
} /* int latency_config_copy */

void latency_config_free(latency_config_t conf) {
  sfree(conf.percentile);
  sfree(conf.buckets);
  sfree(conf.bucket_type);
} /* void latency_config_free */
