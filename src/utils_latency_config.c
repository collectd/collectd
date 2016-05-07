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
#include "common.h"
#include "utils_latency_config.h"

int latency_config_add_percentile (const char *plugin, latency_config_t *cl,
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    ERROR ("%s plugin: \"%s\" requires exactly one numeric argument.",
             plugin, ci->key);
    return (-1);
  }

  double percent = ci->values[0].value.number;
  double *tmp;

  if ((percent <= 0.0) || (percent >= 100))
  {
    ERROR ("%s plugin: The value for \"%s\" must be between 0 and 100, "
        "exclusively.", plugin, ci->key);
    return (ERANGE);
  }

  tmp = realloc (cl->percentile,
      sizeof (*cl->percentile) * (cl->percentile_num + 1));
  if (tmp == NULL)
  {
    ERROR ("%s plugin: realloc failed.", plugin);
    return (ENOMEM);
  }
  cl->percentile = tmp;
  cl->percentile[cl->percentile_num] = percent;
  cl->percentile_num++;

  return (0);
} /* int latency_config_add_percentile */

int latency_config_add_rate (const char *plugin, latency_config_t *cl,
    oconfig_item_t *ci)
{

  if ((ci->values_num != 2)
    ||(ci->values[0].type != OCONFIG_TYPE_NUMBER)
    ||(ci->values[1].type != OCONFIG_TYPE_NUMBER))
  {
    ERROR ("%s plugin: \"%s\" requires exactly two numeric arguments.",
             plugin, ci->key);
    return (-1);
  }

  if (ci->values[1].value.number &&
      ci->values[1].value.number <= ci->values[0].value.number)
  {
    ERROR ("%s plugin: MIN must be less than MAX in \"%s\".",
             plugin, ci->key);
    return (-1);
  }

  if (ci->values[0].value.number < 0.001)
  {
    ERROR ("%s plugin: MIN must be greater or equal to 0.001 in \"%s\".",
             plugin, ci->key);
    return (-1);
  }

  cdtime_t lower = DOUBLE_TO_CDTIME_T(ci->values[0].value.number);
  cdtime_t upper = DOUBLE_TO_CDTIME_T(ci->values[1].value.number);
  cdtime_t *tmp;

  tmp = realloc (cl->rates,
      sizeof (*cl->rates) * (cl->rates_num + 1) * 2);
  if (tmp == NULL)
  {
    ERROR ("%s plugin: realloc failed.", plugin);
    return (ENOMEM);
  }
  cl->rates = tmp;
  cl->rates[cl->rates_num * 2] = lower;
  cl->rates[cl->rates_num * 2 + 1] = upper;
  cl->rates_num++;

  return (0);
} /* int latency_config_add_rate */


int latency_config_copy (latency_config_t *dst, const latency_config_t src)
{
  /* Copy percentiles configuration */
  dst->percentile_num = src.percentile_num;
  dst->percentile = malloc(sizeof (*dst->percentile) * (src.percentile_num));
  if (dst->percentile == NULL)
    goto nomem;

  memcpy (dst->percentile, src.percentile,
          (sizeof (*dst->percentile) * (src.percentile_num)));

  if (src.percentile_type != NULL)
  {
    dst->percentile_type = strdup(src.percentile_type);
    if (dst->percentile_type == NULL)
      goto nomem;
  }

  /* Copy rates configuration */
  dst->rates_num = src.rates_num;
  dst->rates = malloc(sizeof (*dst->rates) * (src.rates_num) * 2);
  if (dst->rates == NULL)
    goto nomem;

  memcpy (dst->rates, src.rates,
          (sizeof (*dst->rates) * (src.rates_num) * 2));

  if (src.rates_type != NULL)
  {
    dst->rates_type = strdup(src.rates_type);
    if (dst->rates_type == NULL)
      goto nomem;
  }

  return (0);
nomem:
  free (dst->rates);
  free (dst->rates_type);
  free (dst->percentile);
  free (dst->percentile_type);
  return (-1);
} /* int latency_config_copy */

void latency_config_free (latency_config_t lc)
{
  sfree (lc.rates);
  sfree (lc.rates_type);
  sfree (lc.percentile);
  sfree (lc.percentile_type);
} /* void latency_config_free */
