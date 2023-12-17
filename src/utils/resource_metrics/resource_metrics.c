/**
 * collectd - src/utils/resource_metrics/resource_metrics.c
 * Copyright (C) 2023       Florian octo Forster
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
 **/

#include "daemon/plugin.h"
#include "utils/common/common.h"
#include "utils/resource_metrics/resource_metrics.h"

static int resource_metrics_compare(resource_metrics_t const *a,
                                    resource_metrics_t const *b) {
  return label_set_compare(a->resource, b->resource);
}

static resource_metrics_t *lookup_resource(resource_metrics_set_t *set,
                                           label_set_t resource) {
  resource_metrics_t key = {
      .resource = resource,
  };

  return bsearch(&key, set->ptr, set->num, sizeof(*set->ptr),
                 (void *)resource_metrics_compare);
}

static int insert_resource(resource_metrics_set_t *set, label_set_t resource) {
  resource_metrics_t *rm =
      realloc(set->ptr, (set->num + 1) * sizeof(*set->ptr));
  if (rm == NULL) {
    return ENOMEM;
  }
  set->ptr = rm;

  rm = set->ptr + set->num;
  memset(rm, 0, sizeof(*rm));

  int status = label_set_clone(&rm->resource, resource);
  if (status != 0) {
    return ENOMEM;
  }
  set->num++;

  qsort(set->ptr, set->num, sizeof(*set->ptr),
        (void *)resource_metrics_compare);
  return 0;
}

static resource_metrics_t *
lookup_or_insert_resource(resource_metrics_set_t *set, label_set_t resource) {
  resource_metrics_t *ret = lookup_resource(set, resource);
  if (ret != NULL) {
    return ret;
  }

  int status = insert_resource(set, resource);
  if (status != 0) {
    ERROR("resource_metrics: insert_resource failed: %s", STRERROR(status));
    return NULL;
  }

  ret = lookup_resource(set, resource);
  assert(ret != NULL);
  return ret;
}

static int compare_family_by_name(metric_family_t **a, metric_family_t **b) {
  return strcmp((*a)->name, (*b)->name);
}

static metric_family_t *lookup_family(resource_metrics_t *rm,
                                      metric_family_t const *fam) {
  metric_family_t **ret =
      bsearch(&fam, rm->families, rm->families_num, sizeof(*rm->families),
              (void *)compare_family_by_name);
  if (ret == NULL) {
    return NULL;
  }
  return *ret;
}

static int insert_family(resource_metrics_t *rm, metric_family_t const *fam) {
  metric_family_t **tmp =
      realloc(rm->families, (rm->families_num + 1) * sizeof(*rm->families));
  if (tmp == NULL) {
    return ENOMEM;
  }
  rm->families = tmp;

  /* NOTE: metric_family_clone also copies the resource attributes. This is not
   * strictly required, since we have these attributes in rm. We keep the copies
   * for now for the sake of simplicity. If memory consumption is a problem,
   * this could be de-duplicated, at the cost of more complicated memory
   * management. */
  rm->families[rm->families_num] = metric_family_clone(fam);
  if (rm->families[rm->families_num] == NULL) {
    return errno;
  }

  metric_family_metric_reset(rm->families[rm->families_num]);
  label_set_reset(&rm->families[rm->families_num]->resource);

  rm->families_num++;

  qsort(rm->families, rm->families_num, sizeof(*rm->families),
        (void *)compare_family_by_name);
  return 0;
}

static metric_family_t *lookup_or_insert_family(resource_metrics_t *rm,
                                                metric_family_t const *fam) {
  metric_family_t *ret = lookup_family(rm, fam);
  if (ret != NULL) {
    return ret;
  }

  int status = insert_family(rm, fam);
  if (status != 0) {
    ERROR("resource_metrics: insert_family failed: %s", STRERROR(status));
    return NULL;
  }

  ret = lookup_family(rm, fam);
  assert(ret != NULL);
  return ret;
}

static int compare_metrics(metric_t const *a, metric_t const *b) {
  int cmp = label_set_compare(a->label, b->label);
  if (cmp != 0) {
    return cmp;
  }

  if (a->time < b->time) {
    return -1;
  } else if (a->time > b->time) {
    return 1;
  }

  return 0;
}

static bool metric_exists(metric_family_t const *fam, metric_t const *m) {
  metric_family_t *found =
      bsearch(m, fam->metric.ptr, fam->metric.num, sizeof(*fam->metric.ptr),
              (void *)compare_metrics);
  return found != NULL;
}

static int insert_metrics(metric_family_t *fam, metric_list_t metrics) {
  int skipped = 0;
  for (size_t i = 0; i < metrics.num; i++) {
    metric_t const *m = metrics.ptr + i;

    if (metric_exists(fam, m)) {
#if COLLECT_DEBUG
      strbuf_t buf = STRBUF_CREATE;
      metric_identity(&buf, m);
      DEBUG("resource_metrics: Skipping duplicate of metric %s", buf.ptr);
      STRBUF_DESTROY(buf);
#endif
      skipped++;
      continue;
    }

    int status = metric_family_metric_append(fam, *m);
    if (status != 0) {
      ERROR("resource_metrics: metric_family_metric_append failed: %s",
            STRERROR(status));
      /* DO NOT RETURN: the metric list may be unsorted */
      skipped++;
    }
  }

  if (((size_t)skipped) != metrics.num) {
    qsort(fam->metric.ptr, fam->metric.num, sizeof(*fam->metric.ptr),
          (void *)compare_metrics);
  }

  return skipped;
}

int resource_metrics_add(resource_metrics_set_t *set,
                         metric_family_t const *fam) {
  if (set == NULL || fam == NULL) {
    return -1;
  }

  resource_metrics_t *rm = lookup_or_insert_resource(set, fam->resource);
  if (rm == NULL) {
    return -1;
  }

  metric_family_t *staged_fam = lookup_or_insert_family(rm, fam);
  if (staged_fam == NULL) {
    return -1;
  }

  return insert_metrics(staged_fam, fam->metric);
}

static void resource_reset(resource_metrics_t *rm) {
  label_set_reset(&rm->resource);

  for (size_t i = 0; i < rm->families_num; i++) {
    metric_family_free(rm->families[i]);
    rm->families[i] = NULL;
  }
  free(rm->families);
  rm->families = NULL;
  rm->families_num = 0;
}

void resource_metrics_reset(resource_metrics_set_t *set) {
  for (size_t i = 0; i < set->num; i++) {
    resource_reset(&set->ptr[i]);
  }
  free(set->ptr);
  set->ptr = NULL;
  set->num = 0;
}
