/**
 * collectd - src/fastpath.c
 * Copyright 2016 6WIND S.A.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Authors:
 *   Amine Kherbouche <amine.kherbouche at 6wind.com>
 **/

#include "collectd.h"
#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */

#include <stdio.h>
#include <jansson.h>

#define PLUGIN_NAME "fastpath"
#define PLUGIN_VALUE_TYPE_CPU "fastpath_cpu_busy"

static int fp_init (void)
{
	int status;
	char cmd[256];

	status = ssnprintf (cmd, sizeof(cmd),
			    "/usr/local/bin/fp-cpu-usage --json");
	if ((status < 1) || ((unsigned int)status >= sizeof (cmd))
	    || (!access (cmd, R_OK))) {
		ERROR ("fastpath plugin: not started/installed, missing fp-cpu-usage");
		return (-1);
	}
	return (0);
} /* int fp_init */

static void fp_submit (int type_inst, char *type, int value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;
	vl.values = values;
	vl.values_len = 1;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, PLUGIN_NAME, sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	ssnprintf (vl.type_instance, sizeof (vl.type_instance),
		   "%i", type_inst);

	plugin_dispatch_values (&vl);

} /* void fp_submit */

static int fp_read (void)
{
	FILE * cpu_usage_json = NULL;
	int i;
	json_t *root, *cpus_info,
	       *data, *core_id, *busy;
	json_error_t error;

	cpu_usage_json = popen("/usr/local/bin/fp-cpu-usage --json", "r");
	if (cpu_usage_json  == NULL) {
		ERROR ("fastpath plugin: popen failed");
		return (-1);
	}

	root = json_loadf (cpu_usage_json, 0, &error);

	if (!root) {
		ERROR ("fastpath plugin: error on line %d: %s",
		       error.line, error.text);
		return (-1);
	}

	if (!json_is_object(root)) {
		ERROR ("fastpath plugin: root is not an object");
		json_decref (root);
		return (-1);
	}

	cpus_info = json_object_get (root, "cpus");

	for (i = 0; i < json_array_size(cpus_info); i++) {

		data = json_array_get (cpus_info, i);

		if (!json_is_object(data)) {
			ERROR ("fastpath plugin: error data %d "
			       "is not an object", (i + 1));
			json_decref (root);
			return (-1);
		}

		core_id = json_object_get (data, "cpu");

		if (!json_is_integer(core_id)) {
			ERROR ("fastpath plugin: error while getting "
			       "cpu id %i", (i + 1));
			json_decref (root);
			return (-1);
		}

		busy = json_object_get (data, "busy");

		if (!json_is_integer(busy)) {
			ERROR ("fastpath plugin: error while getting "
			       "cpu usage of core id %i", (i + 1));
			json_decref (root);
			return (-1);
		}
		fp_submit (json_integer_value(core_id), PLUGIN_VALUE_TYPE_CPU,
			   json_integer_value(busy));
	}

	json_decref (root);
	pclose (cpu_usage_json);
	return (0);

} /* int fp_read */

void module_register (void)
{
	plugin_register_init ("fastpath", fp_init);
	plugin_register_read ("fastpath", fp_read);

} /* void module_register */
