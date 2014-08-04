/**
 * collectd - src/xmms.c
 * Copyright (C) 2007       Florian octo Forster
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
