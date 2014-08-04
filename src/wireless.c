/**
 * collectd - src/wireless.c
 * Copyright (C) 2006,2007  Florian octo Forster
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
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define WIRELESS_PROC_FILE "/proc/net/wireless"

#if 0
static double wireless_dbm_to_watt (double dbm)
{
	double watt;

	/*
	 * dbm = 10 * log_{10} (1000 * power / W)
	 * power = 10^(dbm/10) * W/1000 
	 */

	watt = pow (10.0, (dbm / 10.0)) / 1000.0;

	return (watt);
}
#endif

static void wireless_submit (const char *plugin_instance, const char *type,
		double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "wireless", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void wireless_submit */

#define POWER_MIN -90.0
#define POWER_MAX -50.0
static double wireless_percent_to_power (double quality)
{
	assert ((quality >= 0.0) && (quality <= 100.0));

	return ((quality * (POWER_MAX - POWER_MIN)) + POWER_MIN);
} /* double wireless_percent_to_power */

static int wireless_read (void)
{
#ifdef KERNEL_LINUX
	FILE *fh;
	char buffer[1024];

	char   *device;
	double  quality;
	double  power;
	double  noise;
	
	char *fields[8];
	int   numfields;

	int devices_found;
	int len;

	/* there are a variety of names for the wireless device */
	if ((fh = fopen (WIRELESS_PROC_FILE, "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("wireless: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	devices_found = 0;
	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *endptr;

		numfields = strsplit (buffer, fields, 8);

		if (numfields < 5)
			continue;

		len = strlen (fields[0]) - 1;
		if (len < 1)
			continue;
		if (fields[0][len] != ':')
			continue;
		fields[0][len] = '\0';

		device  = fields[0];

		quality = strtod (fields[2], &endptr);
		if (fields[2] == endptr)
			quality = -1.0; /* invalid */

		/* power [dBm] < 0.0 */
		power = strtod (fields[3], &endptr);
		if (fields[3] == endptr)
			power = 1.0; /* invalid */
		else if ((power >= 0.0) && (power <= 100.0))
			power = wireless_percent_to_power (power);
		else if ((power > 100.0) && (power <= 256.0))
			power = power - 256.0;
		else if (power > 0.0)
			power = 1.0; /* invalid */

		/* noise [dBm] < 0.0 */
		noise = strtod (fields[4], &endptr);
		if (fields[4] == endptr)
			noise = 1.0; /* invalid */
		else if ((noise >= 0.0) && (noise <= 100.0))
			noise = wireless_percent_to_power (noise);
		else if ((noise > 100.0) && (noise <= 256.0))
			noise = noise - 256.0;
		else if (noise > 0.0)
			noise = 1.0; /* invalid */

		wireless_submit (device, "signal_quality", quality);
		wireless_submit (device, "signal_power", power);
		wireless_submit (device, "signal_noise", noise);

		devices_found++;
	}

	fclose (fh);

	/* If no wireless devices are present return an error, so the plugin
	 * code delays our read function. */
	if (devices_found == 0)
		return (-1);
#endif /* KERNEL_LINUX */

	return (0);
} /* int wireless_read */

void module_register (void)
{
	plugin_register_read ("wireless", wireless_read);
} /* void module_register */
