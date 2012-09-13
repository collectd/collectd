/**
 * collectd - src/xmms.c
 * Copyright (C) 2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include <xmms/xmmsctrl.h>

static gint xmms_session;

static void cxmms_submit (const char *type, gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "xmms", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void cxmms_submit */

int cxmms_read (void)
{
  gint rate;
  gint freq;
  gint nch;

  if (!xmms_remote_is_running (xmms_session))
    return (0);

  xmms_remote_get_info (xmms_session, &rate, &freq, &nch);

  if ((freq == 0) || (nch == 0))
    return (-1);

  cxmms_submit ("bitrate", rate);
  cxmms_submit ("frequency", freq);

  return (0);
} /* int read */

void module_register (void)
{
  plugin_register_read ("xmms", cxmms_read);
} /* void module_register */

/*
 * vim: shiftwidth=2:softtabstop=2:textwidth=78
 */
