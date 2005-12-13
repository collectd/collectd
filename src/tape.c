/**
 * collectd - src/tape.c
 * Copyright (C) 2005  Scott Garrett
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
 *   Scott Garrett <sgarrett at technomancer.com>
 **/

#include "tape.h"

#if COLLECT_TAPE
#define MODULE_NAME "tape"

#include "plugin.h"
#include "common.h"

#if defined(HAVE_LIBKSTAT)
#define MAX_NUMTAPE 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMTAPE];
static int numtape = 0;
#endif /* HAVE_LIBKSTAT */

static char *tape_filename_template = "tape-%s.rrd";

/* 104857600 == 100 MB */
static char *tape_ds_def[] =
{
	"DS:rcount:COUNTER:25:0:U",
	"DS:rmerged:COUNTER:25:0:U",
	"DS:rbytes:COUNTER:25:0:U",
	"DS:rtime:COUNTER:25:0:U",
	"DS:wcount:COUNTER:25:0:U",
	"DS:wmerged:COUNTER:25:0:U",
	"DS:wbytes:COUNTER:25:0:U",
	"DS:wtime:COUNTER:25:0:U",
	NULL
};
static int tape_ds_num = 8;

extern time_t curtime;

void tape_init (void)
{
#ifdef HAVE_LIBKSTAT
	kstat_t *ksp_chain;

	numtape = 0;

	if (kc == NULL)
		return;

	for (numtape = 0, ksp_chain = kc->kc_chain;
			(numtape < MAX_NUMTAPE) && (ksp_chain != NULL);
			ksp_chain = ksp_chain->ks_next)
	{
		if (strncmp (ksp_chain->ks_class, "tape", 4) )
			continue;
		if (ksp_chain->ks_type != KSTAT_TYPE_IO)
			continue;
		ksp[numtape++] = ksp_chain;
	}
#endif

	return;
}

void tape_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, tape_filename_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, tape_ds_def, tape_ds_num);
}


#define BUFSIZE 512
void tape_submit (char *tape_name,
		unsigned long long read_count,
		unsigned long long read_merged,
		unsigned long long read_bytes,
		unsigned long long read_time,
		unsigned long long write_count,
		unsigned long long write_merged,
		unsigned long long write_bytes,
		unsigned long long write_time)

{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu",
				(unsigned int) curtime,
				read_count, read_merged, read_bytes, read_time,
				write_count, write_merged, write_bytes,
				write_time) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, tape_name, buf);
}

#undef BUFSIZE

void tape_read (void)
{

#if defined(HAVE_LIBKSTAT)
	static kstat_io_t kio;
	int i;

	if (kc == NULL)
		return;

	for (i = 0; i < numtape; i++)
	{
		if (kstat_read (kc, ksp[i], &kio) == -1)
			continue;

		if (strncmp (ksp[i]->ks_class, "tape", 4) == 0)
			tape_submit (ksp[i]->ks_name,
					kio.reads,  0LL, kio.nread,    kio.rtime,
					kio.writes, 0LL, kio.nwritten, kio.wtime);
	}
#endif /* defined(HAVE_LIBKSTAT) */
}

void module_register (void)
{
	plugin_register (MODULE_NAME, tape_init, tape_read, tape_write);
}

#undef MODULE_NAME
#endif /* COLLECT_TAPE */
