/**
 * collectd - src/traffic.c
 * Copyright (C) 2005  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "traffic"

#if defined(KERNEL_LINUX) || defined(HAVE_LIBKSTAT) || defined(HAVE_LIBSTATGRAB)
# define TRAFFIC_HAVE_READ 1
#else
# define TRAFFIC_HAVE_READ 0
#endif

static char *traffic_filename_template = "traffic-%s.rrd";

static char *ds_def[] =
{
	"DS:incoming:COUNTER:25:0:U",
	"DS:outgoing:COUNTER:25:0:U",
	NULL
};
static int ds_num = 2;

#ifdef HAVE_LIBKSTAT
#define MAX_NUMIF 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMIF];
static int numif = 0;
#endif /* HAVE_LIBKSTAT */

void traffic_init (void)
{
#ifdef HAVE_LIBKSTAT
	kstat_t *ksp_chain;
	kstat_named_t *kn;
	unsigned long long val;

	numif = 0;

	if (kc == NULL)
		return;

	for (numif = 0, ksp_chain = kc->kc_chain;
			(numif < MAX_NUMIF) && (ksp_chain != NULL);
			ksp_chain = ksp_chain->ks_next)
	{
		if (strncmp (ksp_chain->ks_class, "net", 3))
			continue;
		if (ksp_chain->ks_type != KSTAT_TYPE_NAMED)
			continue;
		if (kstat_read (kc, ksp_chain, NULL) == -1)
			continue;
		if ((val = get_kstat_value (ksp_chain, "obytes")) == -1LL)
			continue;
		ksp[numif++] = ksp_chain;
	}
#endif /* HAVE_LIBKSTAT */
}

void traffic_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, traffic_filename_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#define BUFSIZE 512
void traffic_submit (char *device,
		unsigned long long incoming,
		unsigned long long outgoing)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%lld:%lld", (unsigned int) curtime, incoming, outgoing) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, device, buf);
}
#undef BUFSIZE

#if TRAFFIC_HAVE_READ
void traffic_read (void)
{
#ifdef KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	unsigned long long incoming, outgoing;
	char *device;
	
	char *dummy;
	char *fields[16];
	int numfields;

	if ((fh = fopen ("/proc/net/dev", "r")) == NULL)
	{
		syslog (LOG_WARNING, "traffic: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		if (buffer[6] != ':')
			continue;
		buffer[6] = '\0';

		device = buffer;
		while (device[0] == ' ')
			device++;

		if (device[0] == '\0')
			continue;
		
		dummy = buffer + 7;
		numfields = strsplit (dummy, fields, 16);

		if (numfields < 9)
			continue;

		incoming = atoll (fields[0]);
		outgoing = atoll (fields[8]);

		traffic_submit (device, incoming, outgoing);
	}

	fclose (fh);
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
	int i;
	unsigned long long incoming, outgoing;

	if (kc == NULL)
		return;

	for (i = 0; i < numif; i++)
	{
		if (kstat_read (kc, ksp[i], NULL) == -1)
			continue;

		if ((incoming = get_kstat_value (ksp[i], "rbytes")) == -1LL)
			continue;
		if ((outgoing = get_kstat_value (ksp[i], "obytes")) == -1LL)
			continue;

		traffic_submit (ksp[i]->ks_name, incoming, outgoing);
	}
/* #endif HAVE_LIBKSTAT */

#elif defined(HAVE_LIBSTATGRAB)
	sg_network_io_stats *ios;
	int i, num;

	ios = sg_get_network_io_stats (&num);

	for (i = 0; i < num; i++)
		traffic_submit (ios[i].interface_name, ios[i].rx, ios[i].tx);
#endif /* HAVE_LIBSTATGRAB */
}
#else
#define traffic_read NULL
#endif /* TRAFFIC_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, traffic_init, traffic_read, traffic_write);
}

#undef MODULE_NAME
