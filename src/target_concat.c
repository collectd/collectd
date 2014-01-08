/**
 * collectd - src/target_concat.c
 * Copyright (C) 2013  Savoir-faire Linux Inc.
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
 *   Philippe Proulx <philippe.proulx@savoirfairelinux.com>
 **/
#include "collectd.h"
#include "common.h"
#include "filter_chain.h"

static int tr_destroy (void **user_data) /* {{{ */
{
  return (0);
} /* }}} int tr_destroy */

static int tr_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  return 0;
} /* }}} int tr_create */

static int tr_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  if ((ds == NULL) || (vl == NULL))
    return (-EINVAL);

  if (strlen(vl->plugin_instance) > 0) {
    /* concatenate plugin instance to type instance */
    strcat(vl->type, "-");
    strcat(vl->type, vl->plugin_instance);

    /* normalize plugin instance */
    strcpy(vl->plugin_instance, "");
  }

  return (FC_TARGET_CONTINUE);
} /* }}} int tr_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = tr_create;
	tproc.destroy = tr_destroy;
	tproc.invoke  = tr_invoke;
	fc_register_target ("concat", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
