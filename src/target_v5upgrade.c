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
  memcpy (new_vl.type, "df_complex", sizeof (new_vl.type));

  /* Dispatch two new value lists instead of this one */
  new_vl.values[0].gauge = vl->values[0].gauge;
  memcpy (new_vl.type_instance, "used", sizeof (new_vl.type_instance));
  plugin_dispatch_values (&new_vl);

  new_vl.values[0].gauge = vl->values[1].gauge;
  memcpy (new_vl.type_instance, "free", sizeof (new_vl.type_instance));
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

