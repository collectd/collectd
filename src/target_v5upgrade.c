/**
 * collectd - src/target_set.c
 * Copyright (C) 2008-2010  Florian Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "filter_chain.h"

static void v5_swap_instances (value_list_t *vl) /* {{{ */
{
  char tmp[DATA_MAX_NAME_LEN];

  assert (sizeof (tmp) == sizeof (vl->plugin_instance));
  assert (sizeof (tmp) == sizeof (vl->type_instance));

  memcpy (tmp, vl->plugin_instance, sizeof (tmp));
  memcpy (vl->plugin_instance, vl->type_instance, sizeof (tmp));
  memcpy (vl->type_instance, tmp, sizeof (tmp));
} /* }}} void v5_swap_instances */

/*
 * Df type
 *
 * By default, the "df" plugin of version 4.* uses the "df" type and puts the
 * mount point in the type instance. Detect this behavior and convert the type
 * to "df_complex". This can be selected in versions 4.9 and 4.10 by setting
 * the "ReportReserved" option of the "df" plugin.
 */
static int v5_df (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;

  /* Can't upgrade if both instances have been set. */
  if ((vl->plugin_instance[0] != 0)
      && (vl->type_instance[0] != 0))
    return (FC_TARGET_CONTINUE);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  /* Move the mount point name to the plugin instance */
  if (new_vl.plugin_instance[0] == 0)
    v5_swap_instances (&new_vl);

  /* Change the type to "df_complex" */
  sstrncpy (new_vl.type, "df_complex", sizeof (new_vl.type));

  /* Dispatch two new value lists instead of this one */
  new_vl.values[0].gauge = vl->values[0].gauge;
  sstrncpy (new_vl.type_instance, "used", sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].gauge = vl->values[1].gauge;
  sstrncpy (new_vl.type_instance, "free", sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_df */

/*
 * Interface plugin
 *
 * 4.* stores the interface in the type instance and leaves the plugin
 * instance empty. If this is the case, put the interface name into the plugin
 * instance and clear the type instance.
 */
static int v5_interface (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  if ((vl->plugin_instance[0] != 0) || (vl->type_instance[0] == 0))
    return (FC_TARGET_CONTINUE);

  v5_swap_instances (vl);
  return (FC_TARGET_CONTINUE);
} /* }}} int v5_interface */

/*
 * MySQL query cache
 *
 * 4.* uses the "mysql_qcache" type which mixes different types of
 * information. In 5.* this has been broken up.
 */
static int v5_mysql_qcache (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;

  if (vl->values_len != 5)
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  /* Change the type to "cache_result" */
  sstrncpy (new_vl.type, "cache_result", sizeof (new_vl.type));

  /* Dispatch new value lists instead of this one */
  new_vl.values[0].derive = (derive_t) vl->values[0].counter;
  sstrncpy (new_vl.type_instance, "qcache-hits",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].derive = (derive_t) vl->values[1].counter;
  sstrncpy (new_vl.type_instance, "qcache-inserts",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].derive = (derive_t) vl->values[2].counter;
  sstrncpy (new_vl.type_instance, "qcache-not_cached",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].derive = (derive_t) vl->values[3].counter;
  sstrncpy (new_vl.type_instance, "qcache-prunes",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  /* The last data source is a gauge value, so we have to use a different type
   * here. */
  new_vl.values[0].gauge = vl->values[4].gauge;
  sstrncpy (new_vl.type, "cache_size", sizeof (new_vl.type));
  sstrncpy (new_vl.type_instance, "qcache",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_mysql_qcache */

/*
 * MySQL thread count
 *
 * 4.* uses the "mysql_threads" type which mixes different types of
 * information. In 5.* this has been broken up.
 */
static int v5_mysql_threads (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;

  if (vl->values_len != 4)
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  /* Change the type to "threads" */
  sstrncpy (new_vl.type, "threads", sizeof (new_vl.type));

  /* Dispatch new value lists instead of this one */
  new_vl.values[0].gauge = vl->values[0].gauge;
  sstrncpy (new_vl.type_instance, "running",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].gauge = vl->values[1].gauge;
  sstrncpy (new_vl.type_instance, "connected",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].gauge = vl->values[2].gauge;
  sstrncpy (new_vl.type_instance, "cached",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  /* The last data source is a counter value, so we have to use a different
   * type here. */
  new_vl.values[0].derive = (derive_t) vl->values[3].counter;
  sstrncpy (new_vl.type, "total_threads", sizeof (new_vl.type));
  sstrncpy (new_vl.type_instance, "created",
      sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_mysql_threads */

/*
 * ZFS ARC hit and miss counters
 *
 * 4.* uses the flawed "arc_counts" type. In 5.* this has been replaced by the
 * more generic "cache_result" type.
 */
static int v5_zfs_arc_counts (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;
  _Bool is_hits;

  if (vl->values_len != 4)
    return (FC_TARGET_STOP);

  if (strcmp ("hits", vl->type_instance) == 0)
    is_hits = 1;
  else if (strcmp ("misses", vl->type_instance) == 0)
    is_hits = 0;
  else
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  /* Change the type to "cache_result" */
  sstrncpy (new_vl.type, "cache_result", sizeof (new_vl.type));

  /* Dispatch new value lists instead of this one */
  new_vl.values[0].derive = (derive_t) vl->values[0].counter;
  ssnprintf (new_vl.type_instance, sizeof (new_vl.type_instance),
      "demand_data-%s",
      is_hits ? "hit" : "miss");
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].derive = (derive_t) vl->values[1].counter;
  ssnprintf (new_vl.type_instance, sizeof (new_vl.type_instance),
      "demand_metadata-%s",
      is_hits ? "hit" : "miss");
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].derive = (derive_t) vl->values[2].counter;
  ssnprintf (new_vl.type_instance, sizeof (new_vl.type_instance),
      "prefetch_data-%s",
      is_hits ? "hit" : "miss");
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].derive = (derive_t) vl->values[3].counter;
  ssnprintf (new_vl.type_instance, sizeof (new_vl.type_instance),
      "prefetch_metadata-%s",
      is_hits ? "hit" : "miss");
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_zfs_arc_counts */

/*
 * ZFS ARC L2 bytes
 *
 * "arc_l2_bytes" -> "io_octets-L2".
 */
static int v5_zfs_arc_l2_bytes (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_values[2];

  if (vl->values_len != 2)
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = new_values;
  new_vl.values_len = 2;
  new_vl.meta = NULL;

  /* Change the type/-instance to "io_octets-L2" */
  sstrncpy (new_vl.type, "io_octets", sizeof (new_vl.type));
  sstrncpy (new_vl.type_instance, "L2", sizeof (new_vl.type_instance));

  /* Copy the actual values. */
  new_vl.values[0].derive = (derive_t) vl->values[0].counter;
  new_vl.values[1].derive = (derive_t) vl->values[1].counter;

  /* Dispatch new value lists instead of this one */
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_zfs_arc_l2_bytes */

/*
 * ZFS ARC L2 cache size
 *
 * 4.* uses a separate type for this. 5.* uses the generic "cache_size" type
 * instead.
 */
static int v5_zfs_arc_l2_size (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;

  if (vl->values_len != 1)
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  new_vl.values[0].gauge = (gauge_t) vl->values[0].gauge;

  /* Change the type to "cache_size" */
  sstrncpy (new_vl.type, "cache_size", sizeof (new_vl.type));

  /* Adapt the type instance */
  sstrncpy (new_vl.type_instance, "L2", sizeof (new_vl.type_instance));

  /* Dispatch new value lists instead of this one */
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_zfs_arc_l2_size */

/*
 * ZFS ARC ratio
 *
 * "arc_ratio-L1" -> "cache_ratio-arc"
 * "arc_ratio-L2" -> "cache_ratio-L2"
 */
static int v5_zfs_arc_ratio (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;

  if (vl->values_len != 1)
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  new_vl.values[0].gauge = (gauge_t) vl->values[0].gauge;

  /* Change the type to "cache_ratio" */
  sstrncpy (new_vl.type, "cache_ratio", sizeof (new_vl.type));

  /* Adapt the type instance */
  if (strcmp ("L1", vl->type_instance) == 0)
    sstrncpy (new_vl.type_instance, "arc", sizeof (new_vl.type_instance));

  /* Dispatch new value lists instead of this one */
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_zfs_arc_ratio */

/*
 * ZFS ARC size
 *
 * 4.* uses the "arc_size" type with four data sources. In 5.* this has been
 * replaces with the "cache_size" type and static data has been removed.
 */
static int v5_zfs_arc_size (const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  value_list_t new_vl;
  value_t new_value;

  if (vl->values_len != 4)
    return (FC_TARGET_STOP);

  /* Copy everything: Time, interval, host, ... */
  memcpy (&new_vl, vl, sizeof (new_vl));

  /* Reset data we can't simply copy */
  new_vl.values = &new_value;
  new_vl.values_len = 1;
  new_vl.meta = NULL;

  /* Change the type to "cache_size" */
  sstrncpy (new_vl.type, "cache_size", sizeof (new_vl.type));

  /* Dispatch new value lists instead of this one */
  new_vl.values[0].derive = (derive_t) vl->values[0].counter;
  sstrncpy (new_vl.type_instance, "arc", sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  /* Abort processing */
  return (FC_TARGET_STOP);
} /* }}} int v5_zfs_arc_size */

static int v5_destroy (void **user_data) /* {{{ */
{
  return (0);
} /* }}} int v5_destroy */

static int v5_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  *user_data = NULL;
  return (0);
} /* }}} int v5_create */

static int v5_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta,
    void __attribute__((unused)) **user_data)
{
  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  if (strcmp ("df", vl->type) == 0)
    return (v5_df (ds, vl));
  else if (strcmp ("interface", vl->plugin) == 0)
    return (v5_interface (ds, vl));
  else if (strcmp ("mysql_qcache", vl->type) == 0)
    return (v5_mysql_qcache (ds, vl));
  else if (strcmp ("mysql_threads", vl->type) == 0)
    return (v5_mysql_threads (ds, vl));
  else if (strcmp ("arc_counts", vl->type) == 0)
    return (v5_zfs_arc_counts (ds, vl));
  else if (strcmp ("arc_l2_bytes", vl->type) == 0)
    return (v5_zfs_arc_l2_bytes (ds, vl));
  else if (strcmp ("arc_l2_size", vl->type) == 0)
    return (v5_zfs_arc_l2_size (ds, vl));
  else if (strcmp ("arc_ratio", vl->type) == 0)
    return (v5_zfs_arc_ratio (ds, vl));
  else if (strcmp ("arc_size", vl->type) == 0)
    return (v5_zfs_arc_size (ds, vl));

  return (FC_TARGET_CONTINUE);
} /* }}} int v5_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = v5_create;
	tproc.destroy = v5_destroy;
	tproc.invoke  = v5_invoke;
	fc_register_target ("v5upgrade", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

