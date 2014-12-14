/**
 * collectd - src/load.c
 * Copyright (C) 2005-2008  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2013       Vedran Bartonicek
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
 *   Florian octo Forster <octo at collectd.org>
 *   Manuel Sanmartin
 *   Vedran Bartonicek <vbartoni at gmail.com>
 **/
#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <unistd.h>

static void zmq_submit(char const *type, gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "zmq", sizeof (vl.plugin));
	sstrncpy (vl.type, "zmq", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int zmq_read (void) 
{
	zmq_submit("what", 100);
	zmq_submit("what1", 200);
	return 0;
}

void module_register (void)
{
	printf("Registering module\n");
	plugin_register_read ("zmq", zmq_read);
}
