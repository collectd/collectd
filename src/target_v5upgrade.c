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
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  if (strcmp ("interface", vl->plugin) == 0)
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

