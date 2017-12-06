/**
 * collectd - src/zfs_arc.c
 * Copyright (C) 2009  Anthony Dewhurst
 * Copyright (C) 2012  Aurelien Rougemont
 * Copyright (C) 2013  Xin Li
 * Copyright (C) 2014  Marc Fournier
 * Copyright (C) 2014  Wilfried Goesgens
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
 *   Anthony Dewhurst <dewhurst at gmail>
 *   Aurelien Rougemont <beorn at gandi.net>
 *   Xin Li <delphij at FreeBSD.org>
 *   Marc Fournier <marc.fournier at camptocamp.com>
 *   Wilfried Goesgens <dothebart at citadel.org>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

/*
 * Global variables
 */
static value_to_rate_state_t arc_hits_state;
static value_to_rate_state_t arc_misses_state;
static value_to_rate_state_t l2_hits_state;
static value_to_rate_state_t l2_misses_state;

#if defined(KERNEL_LINUX)
#include "utils_llist.h"
#define ZOL_ARCSTATS_FILE "/proc/spl/kstat/zfs/arcstats"

typedef llist_t kstat_t;

static int put_zfs_value(kstat_t *ksp, char const *k, value_t v) {
  llentry_t *e;
  char *k_copy;
  value_t *v_copy;

  k_copy = strdup(k);
  if (k_copy == NULL)
    return ENOMEM;

  v_copy = malloc(sizeof(*v_copy));
  if (v_copy == NULL) {
    sfree(k_copy);
    return ENOMEM;
  }
  *v_copy = v;

  e = llentry_create(k_copy, v_copy);
  if (e == NULL) {
    sfree(v_copy);
    sfree(k_copy);
    return ENOMEM;
  }

  llist_append(ksp, e);
  return 0;
}

static long long get_zfs_value(kstat_t *ksp, const char *key) {
  llentry_t *e;
  value_t *v;

  e = llist_search(ksp, key);
  if (e == NULL) {
    return -1;
  }

  v = e->value;
  return (long long)v->derive;
}

static void free_zfs_values(kstat_t *ksp) {
  if (ksp == NULL)
    return;

  for (llentry_t *e = llist_head(ksp); e != NULL; e = e->next) {
    sfree(e->key);
    sfree(e->value);
  }

  llist_destroy(ksp);
}

#elif defined(KERNEL_SOLARIS)
extern kstat_ctl_t *kc;

static long long get_zfs_value(kstat_t *ksp, char *name) {

  return get_kstat_value(ksp, name);
}
#elif defined(KERNEL_FREEBSD)
#include <sys/sysctl.h>
#include <sys/types.h>

const char zfs_arcstat[] = "kstat.zfs.misc.arcstats.";

#if !defined(kstat_t)
typedef void kstat_t;
#endif

static long long get_zfs_value(kstat_t *dummy __attribute__((unused)),
                               char const *name) {
  char buffer[256];
  long long value;
  size_t valuelen = sizeof(value);
  int rv;

  snprintf(buffer, sizeof(buffer), "%s%s", zfs_arcstat, name);
  rv = sysctlbyname(buffer, (void *)&value, &valuelen,
                    /* new value = */ NULL, /* new length = */ (size_t)0);
  if (rv == 0)
    return value;

  return -1;
}
#endif

static void za_submit(const char *type, const char *type_instance,
                      value_t *values, size_t values_len) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = values_len;

  sstrncpy(vl.plugin, "zfs_arc", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void za_submit_gauge(const char *type, const char *type_instance,
                            gauge_t value) {
  za_submit(type, type_instance, &(value_t){.gauge = value}, 1);
}

static int za_read_derive(kstat_t *ksp, const char *kstat_value,
                          const char *type, const char *type_instance) {
  long long tmp = get_zfs_value(ksp, (char *)kstat_value);
  if (tmp == -1LL) {
    DEBUG("zfs_arc plugin: Reading kstat value \"%s\" failed.", kstat_value);
    return -1;
  }

  za_submit(type, type_instance, &(value_t){.derive = (derive_t)tmp},
            /* values_num = */ 1);
  return 0;
}

static int za_read_gauge(kstat_t *ksp, const char *kstat_value,
                         const char *type, const char *type_instance) {
  long long tmp = get_zfs_value(ksp, (char *)kstat_value);
  if (tmp == -1LL) {
    DEBUG("zfs_arc plugin: Reading kstat value \"%s\" failed.", kstat_value);
    return -1;
  }

  za_submit(type, type_instance, &(value_t){.gauge = (gauge_t)tmp},
            /* values_num = */ 1);
  return 0;
}

static void za_submit_ratio(const char *type_instance, gauge_t hits,
                            gauge_t misses) {
  gauge_t ratio = NAN;

  if (!isfinite(hits) || (hits < 0.0))
    hits = 0.0;
  if (!isfinite(misses) || (misses < 0.0))
    misses = 0.0;

  if ((hits != 0.0) || (misses != 0.0))
    ratio = hits / (hits + misses);

  za_submit_gauge("cache_ratio", type_instance, ratio);
}

static int za_read(void) {
  gauge_t arc_hits, arc_misses, l2_hits, l2_misses;
  kstat_t *ksp = NULL;

#if defined(KERNEL_LINUX)
  FILE *fh;
  char buffer[1024];

  fh = fopen(ZOL_ARCSTATS_FILE, "r");
  if (fh == NULL) {
    ERROR("zfs_arc plugin: Opening \"%s\" failed: %s", ZOL_ARCSTATS_FILE,
          STRERRNO);
    return -1;
  }

  /* Ignore the first two lines because they contain information about the rest
   * of the file.
   * See kstat_seq_show_headers module/spl/spl-kstat.c of the spl kernel module.
   */
  if ((fgets(buffer, sizeof(buffer), fh) == NULL) ||
      (fgets(buffer, sizeof(buffer), fh) == NULL)) {
    ERROR("zfs_arc plugin: \"%s\" does not contain at least two lines.",
          ZOL_ARCSTATS_FILE);
    fclose(fh);
    return -1;
  }

  ksp = llist_create();
  if (ksp == NULL) {
    ERROR("zfs_arc plugin: `llist_create' failed.");
    fclose(fh);
    return -1;
  }

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    char *fields[3];
    value_t v;
    int status;

    status = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
    if (status != 3)
      continue;

    status = parse_value(fields[2], &v, DS_TYPE_DERIVE);
    if (status != 0)
      continue;

    put_zfs_value(ksp, fields[0], v);
  }

  fclose(fh);

#elif defined(KERNEL_SOLARIS)
  get_kstat(&ksp, "zfs", 0, "arcstats");
  if (ksp == NULL) {
    ERROR("zfs_arc plugin: Cannot find zfs:0:arcstats kstat.");
    return -1;
  }
#endif

  /* Sizes */
  za_read_gauge(ksp, "anon_size", "cache_size", "anon_size");
  za_read_gauge(ksp, "c", "cache_size", "c");
  za_read_gauge(ksp, "c_max", "cache_size", "c_max");
  za_read_gauge(ksp, "c_min", "cache_size", "c_min");
  za_read_gauge(ksp, "hdr_size", "cache_size", "hdr_size");
  za_read_gauge(ksp, "metadata_size", "cache_size", "metadata_size");
  za_read_gauge(ksp, "mfu_ghost_size", "cache_size", "mfu_ghost_size");
  za_read_gauge(ksp, "mfu_size", "cache_size", "mfu_size");
  za_read_gauge(ksp, "mru_ghost_size", "cache_size", "mru_ghost_size");
  za_read_gauge(ksp, "mru_size", "cache_size", "mru_size");
  za_read_gauge(ksp, "other_size", "cache_size", "other_size");
  za_read_gauge(ksp, "p", "cache_size", "p");
  za_read_gauge(ksp, "size", "cache_size", "arc");

  /* The "l2_size" value has disappeared from Solaris some time in
   * early 2013, and has only reappeared recently in Solaris 11.2.
   * Stop trying if we ever fail to read it, so we don't spam the log.
   */
  static int l2_size_avail = 1;
  if (l2_size_avail && za_read_gauge(ksp, "l2_size", "cache_size", "L2") != 0)
    l2_size_avail = 0;

  /* Operations */
  za_read_derive(ksp, "deleted", "cache_operation", "deleted");
#if defined(KERNEL_FREEBSD)
  za_read_derive(ksp, "allocated", "cache_operation", "allocated");
#endif

  /* Issue indicators */
  za_read_derive(ksp, "mutex_miss", "mutex_operations", "miss");
  za_read_derive(ksp, "hash_collisions", "hash_collisions", "");
  za_read_derive(ksp, "memory_throttle_count", "memory_throttle_count", "");

  /* Evictions */
  za_read_derive(ksp, "evict_l2_cached", "cache_eviction", "cached");
  za_read_derive(ksp, "evict_l2_eligible", "cache_eviction", "eligible");
  za_read_derive(ksp, "evict_l2_ineligible", "cache_eviction", "ineligible");

  /* Hits / misses */
  za_read_derive(ksp, "demand_data_hits", "cache_result", "demand_data-hit");
  za_read_derive(ksp, "demand_metadata_hits", "cache_result",
                 "demand_metadata-hit");
  za_read_derive(ksp, "prefetch_data_hits", "cache_result",
                 "prefetch_data-hit");
  za_read_derive(ksp, "prefetch_metadata_hits", "cache_result",
                 "prefetch_metadata-hit");
  za_read_derive(ksp, "demand_data_misses", "cache_result", "demand_data-miss");
  za_read_derive(ksp, "demand_metadata_misses", "cache_result",
                 "demand_metadata-miss");
  za_read_derive(ksp, "prefetch_data_misses", "cache_result",
                 "prefetch_data-miss");
  za_read_derive(ksp, "prefetch_metadata_misses", "cache_result",
                 "prefetch_metadata-miss");
  za_read_derive(ksp, "mfu_hits", "cache_result", "mfu-hit");
  za_read_derive(ksp, "mfu_ghost_hits", "cache_result", "mfu_ghost-hit");
  za_read_derive(ksp, "mru_hits", "cache_result", "mru-hit");
  za_read_derive(ksp, "mru_ghost_hits", "cache_result", "mru_ghost-hit");

  cdtime_t now = cdtime();

  /* Ratios */
  if ((value_to_rate(&arc_hits, (value_t){.derive = get_zfs_value(ksp, "hits")},
                     DS_TYPE_DERIVE, now, &arc_hits_state) == 0) &&
      (value_to_rate(&arc_misses,
                     (value_t){.derive = get_zfs_value(ksp, "misses")},
                     DS_TYPE_DERIVE, now, &arc_misses_state) == 0)) {
    za_submit_ratio("arc", arc_hits, arc_misses);
  }

  if ((value_to_rate(&l2_hits,
                     (value_t){.derive = get_zfs_value(ksp, "l2_hits")},
                     DS_TYPE_DERIVE, now, &l2_hits_state) == 0) &&
      (value_to_rate(&l2_misses,
                     (value_t){.derive = get_zfs_value(ksp, "l2_misses")},
                     DS_TYPE_DERIVE, now, &l2_misses_state) == 0)) {
    za_submit_ratio("L2", l2_hits, l2_misses);
  }

  /* I/O */
  value_t l2_io[] = {
      {.derive = (derive_t)get_zfs_value(ksp, "l2_read_bytes")},
      {.derive = (derive_t)get_zfs_value(ksp, "l2_write_bytes")},
  };
  za_submit("io_octets", "L2", l2_io, STATIC_ARRAY_SIZE(l2_io));

#if defined(KERNEL_LINUX)
  free_zfs_values(ksp);
#endif

  return 0;
} /* int za_read */

static int za_init(void) /* {{{ */
{
#if defined(KERNEL_SOLARIS)
  /* kstats chain already opened by update_kstat (using *kc), verify everything
   * went fine. */
  if (kc == NULL) {
    ERROR("zfs_arc plugin: kstat chain control structure not available.");
    return -1;
  }
#endif

  return 0;
} /* }}} int za_init */

void module_register(void) {
  plugin_register_init("zfs_arc", za_init);
  plugin_register_read("zfs_arc", za_read);
} /* void module_register */

/* vmi: set sw=8 noexpandtab fdm=marker : */
