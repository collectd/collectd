/**
 * collectd - src/disk.c
 * Copyright (C) 2005,2006  Florian octo Forster
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

#define MODULE_NAME "disk"

#if KERNEL_LINUX || HAVE_LIBKSTAT
# define DISK_HAVE_READ 1
#else
# define DISK_HAVE_READ 0
#endif

static char *disk_filename_template = "disk-%s.rrd";
static char *part_filename_template = "partition-%s.rrd";

/* 104857600 == 100 MB */
static char *disk_ds_def[] =
{
	"DS:rcount:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rmerged:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rbytes:COUNTER:"COLLECTD_HEARTBEAT":0:104857600",
	"DS:rtime:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:wcount:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:wmerged:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:wbytes:COUNTER:"COLLECTD_HEARTBEAT":0:104857600",
	"DS:wtime:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int disk_ds_num = 8;

static char *part_ds_def[] =
{
	"DS:rcount:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rbytes:COUNTER:"COLLECTD_HEARTBEAT":0:104857600",
	"DS:wcount:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:wbytes:COUNTER:"COLLECTD_HEARTBEAT":0:104857600",
	NULL
};
static int part_ds_num = 4;

#if KERNEL_LINUX
typedef struct diskstats
{
	char *name;

	/* This overflows in roughly 1361 year */
	unsigned int poll_count;

	unsigned int read_sectors;
	unsigned int write_sectors;

	unsigned long long read_bytes;
	unsigned long long write_bytes;

	struct diskstats *next;
} diskstats_t;

static diskstats_t *disklist;
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
#define MAX_NUMDISK 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMDISK];
static int numdisk = 0;
#endif /* HAVE_LIBKSTAT */

static void disk_init (void)
{
#if KERNEL_LINUX
	/* No init needed */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	kstat_t *ksp_chain;

	numdisk = 0;

	if (kc == NULL)
		return;

	for (numdisk = 0, ksp_chain = kc->kc_chain;
			(numdisk < MAX_NUMDISK) && (ksp_chain != NULL);
			ksp_chain = ksp_chain->ks_next)
	{
		if (strncmp (ksp_chain->ks_class, "disk", 4)
				&& strncmp (ksp_chain->ks_class, "partition", 9))
			continue;
		if (ksp_chain->ks_type != KSTAT_TYPE_IO)
			continue;
		ksp[numdisk++] = ksp_chain;
	}
#endif /* HAVE_LIBKSTAT */

	return;
}

static void disk_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, disk_filename_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, disk_ds_def, disk_ds_num);
}

static void partition_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, part_filename_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, part_ds_def, part_ds_num);
}

#if DISK_HAVE_READ
#define BUFSIZE 512
static void disk_submit (char *disk_name,
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

	plugin_submit (MODULE_NAME, disk_name, buf);
}

static void partition_submit (char *part_name,
		unsigned long long read_count,
		unsigned long long read_bytes,
		unsigned long long write_count,
		unsigned long long write_bytes)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%llu:%llu:%llu:%llu",
				(unsigned int) curtime,
				read_count, read_bytes, write_count,
				write_bytes) >= BUFSIZE)
		return;

	plugin_submit ("partition", part_name, buf);
}
#undef BUFSIZE

static void disk_read (void)
{
#if KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	char disk_name[128];
	
	char *fields[32];
	int numfields;
	int fieldshift = 0;

	int major = 0;
	int minor = 0;

	unsigned int read_sectors;
	unsigned int write_sectors;

	unsigned long long read_count    = 0;
	unsigned long long read_merged   = 0;
	unsigned long long read_bytes    = 0;
	unsigned long long read_time     = 0;
	unsigned long long write_count   = 0;
	unsigned long long write_merged  = 0;
	unsigned long long write_bytes   = 0;
	unsigned long long write_time    = 0;
	int is_disk = 0;

	diskstats_t *ds, *pre_ds;

	if ((fh = fopen ("/proc/diskstats", "r")) == NULL)
	{
		if ((fh = fopen ("/proc/partitions", "r")) == NULL)
			return;

		/* Kernel is 2.4.* */
		fieldshift = 1;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		numfields = strsplit (buffer, fields, 32);

		if ((numfields != (14 + fieldshift)) && (numfields != 7))
			continue;

		major = atoll (fields[0]);
		minor = atoll (fields[1]);

		if (snprintf (disk_name, 128, "%i-%i", major, minor) < 1)
			continue;
		disk_name[127] = '\0';

		for (ds = disklist, pre_ds = disklist; ds != NULL; pre_ds = ds, ds = ds->next)
			if (strcmp (disk_name, ds->name) == 0)
				break;

		if (ds == NULL)
		{
			if ((ds = (diskstats_t *) calloc (1, sizeof (diskstats_t))) == NULL)
				continue;

			if ((ds->name = strdup (disk_name)) == NULL)
			{
				free (ds);
				continue;
			}

			if (pre_ds == NULL)
				disklist = ds;
			else
				pre_ds->next = ds;
		}

		is_disk = 0;
		if (numfields == 7)
		{
			/* Kernel 2.6, Partition */
			read_count    = atoll (fields[3]);
			read_sectors  = atoi  (fields[4]);
			write_count   = atoll (fields[5]);
			write_sectors = atoi  (fields[6]);
		}
		else if (numfields == (14 + fieldshift))
		{
			read_count  =  atoll (fields[3 + fieldshift]);
			write_count =  atoll (fields[7 + fieldshift]);

			read_sectors  = atoi (fields[5 + fieldshift]);
			write_sectors = atoi (fields[9 + fieldshift]);

			if ((fieldshift == 0) || (minor == 0))
			{
				is_disk = 1;
				read_merged  = atoll (fields[4 + fieldshift]);
				read_time    = atoll (fields[6 + fieldshift]);
				write_merged = atoll (fields[8 + fieldshift]);
				write_time   = atoll (fields[10+ fieldshift]);
			}
		}
		else
		{
			continue;
		}


		if (read_sectors >= ds->read_sectors)
			ds->read_bytes += 512 * (read_sectors - ds->read_sectors);
		else
			ds->read_bytes += 512 * ((UINT_MAX - ds->read_sectors) + read_sectors);

		if (write_sectors >= ds->write_sectors)
			ds->write_bytes += 512 * (write_sectors - ds->write_sectors);
		else
			ds->write_bytes += 512 * ((UINT_MAX - ds->write_sectors) + write_sectors);

		ds->read_sectors  = read_sectors;
		ds->write_sectors = write_sectors;
		read_bytes  = ds->read_bytes;
		write_bytes = ds->write_bytes;

		/* Don't write to the RRDs if we've just started.. */
		ds->poll_count++;
		if (ds->poll_count <= 6)
			continue;

		if ((read_count == 0) && (write_count == 0))
			continue;

		if (is_disk)
			disk_submit (disk_name, read_count, read_merged, read_bytes, read_time,
					write_count, write_merged, write_bytes, write_time);
		else
			partition_submit (disk_name, read_count, read_bytes, write_count, write_bytes);
	}

	fclose (fh);
/* #endif defined(KERNEL_LINUX) */

#elif HAVE_LIBKSTAT
	static kstat_io_t kio;
	int i;

	if (kc == NULL)
		return;

	for (i = 0; i < numdisk; i++)
	{
		if (kstat_read (kc, ksp[i], &kio) == -1)
			continue;

		if (strncmp (ksp[i]->ks_class, "disk", 4) == 0)
			disk_submit (ksp[i]->ks_name,
					kio.reads,  0LL, kio.nread,    kio.rtime,
					kio.writes, 0LL, kio.nwritten, kio.wtime);
		else if (strncmp (ksp[i]->ks_class, "partition", 9) == 0)
			partition_submit (ksp[i]->ks_name,
					kio.reads, kio.nread,
					kio.writes,kio.nwritten);
	}
#endif /* defined(HAVE_LIBKSTAT) */
} /* static void disk_read (void) */
#else
# define disk_read NULL
#endif /* DISK_HAVE_READ */

void module_register (void)
{
	plugin_register ("partition", NULL, NULL, partition_write);
	plugin_register (MODULE_NAME, disk_init, disk_read, disk_write);
}

#undef MODULE_NAME
