/**
 * collectd - src/disk.c
 * Copyright (C) 2005-2010  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
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
 *   Manuel Sanmartin
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if HAVE_MACH_MACH_TYPES_H
#  include <mach/mach_types.h>
#endif
#if HAVE_MACH_MACH_INIT_H
#  include <mach/mach_init.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#  include <mach/mach_error.h>
#endif
#if HAVE_MACH_MACH_PORT_H
#  include <mach/mach_port.h>
#endif
#if HAVE_COREFOUNDATION_COREFOUNDATION_H
#  include <CoreFoundation/CoreFoundation.h>
#endif
#if HAVE_IOKIT_IOKITLIB_H
#  include <IOKit/IOKitLib.h>
#endif
#if HAVE_IOKIT_IOTYPES_H
#  include <IOKit/IOTypes.h>
#endif
#if HAVE_IOKIT_STORAGE_IOBLOCKSTORAGEDRIVER_H
#  include <IOKit/storage/IOBlockStorageDriver.h>
#endif
#if HAVE_IOKIT_IOBSD_H
#  include <IOKit/IOBSD.h>
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif
#ifndef UINT_MAX
#  define UINT_MAX 4294967295U
#endif

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

#if HAVE_PERFSTAT
# ifndef _AIXVERSION_610
# include <sys/systemcfg.h>
# endif
# include <sys/protosw.h>
# include <libperfstat.h>
#endif

#if HAVE_IOKIT_IOKITLIB_H
static mach_port_t io_master_port = MACH_PORT_NULL;
/* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_LINUX
typedef struct diskstats
{
	char *name;

	/* This overflows in roughly 1361 years */
	unsigned int poll_count;

	derive_t read_sectors;
	derive_t write_sectors;

	derive_t read_bytes;
	derive_t write_bytes;

	derive_t read_ops;
	derive_t write_ops;
	derive_t read_time;
	derive_t write_time;

	derive_t avg_read_time;
	derive_t avg_write_time;

	struct diskstats *next;
} diskstats_t;

static diskstats_t *disklist;
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
#define MAX_NUMDISK 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMDISK];
static int numdisk = 0;
/* #endif HAVE_LIBKSTAT */

#elif defined(HAVE_LIBSTATGRAB)
/* #endif HAVE_LIBKSTATGRAB */

#elif HAVE_PERFSTAT
static perfstat_disk_t * stat_disk;
static int numdisk;
static int pnumdisk;
/* #endif HAVE_PERFSTAT */

#else
# error "No applicable input method."
#endif

static const char *config_keys[] =
{
	"Disk",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;

static int disk_config (const char *key, const char *value)
{
  if (ignorelist == NULL)
    ignorelist = ignorelist_create (/* invert = */ 1);
  if (ignorelist == NULL)
    return (1);

  if (strcasecmp ("Disk", key) == 0)
  {
    ignorelist_add (ignorelist, value);
  }
  else if (strcasecmp ("IgnoreSelected", key) == 0)
  {
    int invert = 1;
    if (IS_TRUE (value))
      invert = 0;
    ignorelist_set_invert (ignorelist, invert);
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int disk_config */

static int disk_init (void)
{
#if HAVE_IOKIT_IOKITLIB_H
	kern_return_t status;

	if (io_master_port != MACH_PORT_NULL)
	{
		mach_port_deallocate (mach_task_self (),
				io_master_port);
		io_master_port = MACH_PORT_NULL;
	}

	status = IOMasterPort (MACH_PORT_NULL, &io_master_port);
	if (status != kIOReturnSuccess)
	{
		ERROR ("IOMasterPort failed: %s",
				mach_error_string (status));
		io_master_port = MACH_PORT_NULL;
		return (-1);
	}
/* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_LINUX
	/* do nothing */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	kstat_t *ksp_chain;

	numdisk = 0;

	if (kc == NULL)
		return (-1);

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

	return (0);
} /* int disk_init */

static void disk_submit (const char *plugin_instance,
		const char *type,
		derive_t read, derive_t write)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	/* Both `ignorelist' and `plugin_instance' may be NULL. */
	if (ignorelist_match (ignorelist, plugin_instance) != 0)
	  return;

	values[0].derive = read;
	values[1].derive = write;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "disk", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void disk_submit */

#if HAVE_IOKIT_IOKITLIB_H
static signed long long dict_get_value (CFDictionaryRef dict, const char *key)
{
	signed long long val_int;
	CFNumberRef      val_obj;
	CFStringRef      key_obj;

	/* `key_obj' needs to be released. */
	key_obj = CFStringCreateWithCString (kCFAllocatorDefault, key,
		       	kCFStringEncodingASCII);
	if (key_obj == NULL)
	{
		DEBUG ("CFStringCreateWithCString (%s) failed.", key);
		return (-1LL);
	}
	
	/* get => we don't need to release (== free) the object */
	val_obj = (CFNumberRef) CFDictionaryGetValue (dict, key_obj);

	CFRelease (key_obj);

	if (val_obj == NULL)
	{
		DEBUG ("CFDictionaryGetValue (%s) failed.", key);
		return (-1LL);
	}

	if (!CFNumberGetValue (val_obj, kCFNumberSInt64Type, &val_int))
	{
		DEBUG ("CFNumberGetValue (%s) failed.", key);
		return (-1LL);
	}

	return (val_int);
}
#endif /* HAVE_IOKIT_IOKITLIB_H */

static int disk_read (void)
{
#if HAVE_IOKIT_IOKITLIB_H
	io_registry_entry_t	disk;
	io_registry_entry_t	disk_child;
	io_iterator_t		disk_list;
	CFDictionaryRef		props_dict;
	CFDictionaryRef		stats_dict;
	CFDictionaryRef		child_dict;
	kern_return_t           status;

	signed long long read_ops;
	signed long long read_byt;
	signed long long read_tme;
	signed long long write_ops;
	signed long long write_byt;
	signed long long write_tme;

	int  disk_major;
	int  disk_minor;
	char disk_name[64];

	/* Get the list of all disk objects. */
	if (IOServiceGetMatchingServices (io_master_port,
				IOServiceMatching (kIOBlockStorageDriverClass),
				&disk_list) != kIOReturnSuccess)
	{
		ERROR ("disk plugin: IOServiceGetMatchingServices failed.");
		return (-1);
	}

	while ((disk = IOIteratorNext (disk_list)) != 0)
	{
		props_dict = NULL;
		stats_dict = NULL;
		child_dict = NULL;

		/* `disk_child' must be released */
		if ((status = IORegistryEntryGetChildEntry (disk, kIOServicePlane, &disk_child))
			       	!= kIOReturnSuccess)
		{
			/* This fails for example for DVD/CD drives.. */
			DEBUG ("IORegistryEntryGetChildEntry (disk) failed: 0x%08x", status);
			IOObjectRelease (disk);
			continue;
		}

		/* We create `props_dict' => we need to release it later */
		if (IORegistryEntryCreateCFProperties (disk,
					(CFMutableDictionaryRef *) &props_dict,
					kCFAllocatorDefault,
					kNilOptions)
				!= kIOReturnSuccess)
		{
			ERROR ("disk-plugin: IORegistryEntryCreateCFProperties failed.");
			IOObjectRelease (disk_child);
			IOObjectRelease (disk);
			continue;
		}

		if (props_dict == NULL)
		{
			DEBUG ("IORegistryEntryCreateCFProperties (disk) failed.");
			IOObjectRelease (disk_child);
			IOObjectRelease (disk);
			continue;
		}

		stats_dict = (CFDictionaryRef) CFDictionaryGetValue (props_dict,
				CFSTR (kIOBlockStorageDriverStatisticsKey));

		if (stats_dict == NULL)
		{
			DEBUG ("CFDictionaryGetValue (%s) failed.",
				       	kIOBlockStorageDriverStatisticsKey);
			CFRelease (props_dict);
			IOObjectRelease (disk_child);
			IOObjectRelease (disk);
			continue;
		}

		if (IORegistryEntryCreateCFProperties (disk_child,
					(CFMutableDictionaryRef *) &child_dict,
					kCFAllocatorDefault,
					kNilOptions)
				!= kIOReturnSuccess)
		{
			DEBUG ("IORegistryEntryCreateCFProperties (disk_child) failed.");
			IOObjectRelease (disk_child);
			CFRelease (props_dict);
			IOObjectRelease (disk);
			continue;
		}

		/* kIOBSDNameKey */
		disk_major = (int) dict_get_value (child_dict,
			       	kIOBSDMajorKey);
		disk_minor = (int) dict_get_value (child_dict,
			       	kIOBSDMinorKey);
		read_ops  = dict_get_value (stats_dict,
				kIOBlockStorageDriverStatisticsReadsKey);
		read_byt  = dict_get_value (stats_dict,
				kIOBlockStorageDriverStatisticsBytesReadKey);
		read_tme  = dict_get_value (stats_dict,
				kIOBlockStorageDriverStatisticsTotalReadTimeKey);
		write_ops = dict_get_value (stats_dict,
				kIOBlockStorageDriverStatisticsWritesKey);
		write_byt = dict_get_value (stats_dict,
				kIOBlockStorageDriverStatisticsBytesWrittenKey);
		/* This property describes the number of nanoseconds spent
		 * performing writes since the block storage driver was
		 * instantiated. It is one of the statistic entries listed
		 * under the top-level kIOBlockStorageDriverStatisticsKey
		 * property table. It has an OSNumber value. */
		write_tme = dict_get_value (stats_dict,
				kIOBlockStorageDriverStatisticsTotalWriteTimeKey);

		if (ssnprintf (disk_name, sizeof (disk_name),
				"%i-%i", disk_major, disk_minor) >= sizeof (disk_name))
		{
			DEBUG ("snprintf (major, minor) failed.");
			CFRelease (child_dict);
			IOObjectRelease (disk_child);
			CFRelease (props_dict);
			IOObjectRelease (disk);
			continue;
		}
		DEBUG ("disk_name = %s", disk_name);

		if ((read_byt != -1LL) || (write_byt != -1LL))
			disk_submit (disk_name, "disk_octets", read_byt, write_byt);
		if ((read_ops != -1LL) || (write_ops != -1LL))
			disk_submit (disk_name, "disk_ops", read_ops, write_ops);
		if ((read_tme != -1LL) || (write_tme != -1LL))
			disk_submit (disk_name, "disk_time",
					read_tme / 1000,
					write_tme / 1000);

		CFRelease (child_dict);
		IOObjectRelease (disk_child);
		CFRelease (props_dict);
		IOObjectRelease (disk);
	}
	IOObjectRelease (disk_list);
/* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	
	char *fields[32];
	int numfields;
	int fieldshift = 0;

	int minor = 0;

	derive_t read_sectors  = 0;
	derive_t write_sectors = 0;

	derive_t read_ops      = 0;
	derive_t read_merged   = 0;
	derive_t read_time     = 0;
	derive_t write_ops     = 0;
	derive_t write_merged  = 0;
	derive_t write_time    = 0;
	int is_disk = 0;

	diskstats_t *ds, *pre_ds;

	if ((fh = fopen ("/proc/diskstats", "r")) == NULL)
	{
		fh = fopen ("/proc/partitions", "r");
		if (fh == NULL)
		{
			ERROR ("disk plugin: fopen (/proc/{diskstats,partitions}) failed.");
			return (-1);
		}

		/* Kernel is 2.4.* */
		fieldshift = 1;
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *disk_name;

		numfields = strsplit (buffer, fields, 32);

		if ((numfields != (14 + fieldshift)) && (numfields != 7))
			continue;

		minor = atoll (fields[1]);

		disk_name = fields[2 + fieldshift];

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
			read_ops      = atoll (fields[3]);
			read_sectors  = atoll (fields[4]);
			write_ops     = atoll (fields[5]);
			write_sectors = atoll (fields[6]);
		}
		else if (numfields == (14 + fieldshift))
		{
			read_ops  =  atoll (fields[3 + fieldshift]);
			write_ops =  atoll (fields[7 + fieldshift]);

			read_sectors  = atoll (fields[5 + fieldshift]);
			write_sectors = atoll (fields[9 + fieldshift]);

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
			DEBUG ("numfields = %i; => unknown file format.", numfields);
			continue;
		}

		{
			derive_t diff_read_sectors;
			derive_t diff_write_sectors;

		/* If the counter wraps around, it's only 32 bits.. */
			if (read_sectors < ds->read_sectors)
				diff_read_sectors = 1 + read_sectors
					+ (UINT_MAX - ds->read_sectors);
			else
				diff_read_sectors = read_sectors - ds->read_sectors;
			if (write_sectors < ds->write_sectors)
				diff_write_sectors = 1 + write_sectors
					+ (UINT_MAX - ds->write_sectors);
			else
				diff_write_sectors = write_sectors - ds->write_sectors;

			ds->read_bytes += 512 * diff_read_sectors;
			ds->write_bytes += 512 * diff_write_sectors;
			ds->read_sectors = read_sectors;
			ds->write_sectors = write_sectors;
		}

		/* Calculate the average time an io-op needs to complete */
		if (is_disk)
		{
			derive_t diff_read_ops;
			derive_t diff_write_ops;
			derive_t diff_read_time;
			derive_t diff_write_time;

			if (read_ops < ds->read_ops)
				diff_read_ops = 1 + read_ops
					+ (UINT_MAX - ds->read_ops);
			else
				diff_read_ops = read_ops - ds->read_ops;
			DEBUG ("disk plugin: disk_name = %s; read_ops = %"PRIi64"; "
					"ds->read_ops = %"PRIi64"; diff_read_ops = %"PRIi64";",
					disk_name,
					read_ops, ds->read_ops, diff_read_ops);

			if (write_ops < ds->write_ops)
				diff_write_ops = 1 + write_ops
					+ (UINT_MAX - ds->write_ops);
			else
				diff_write_ops = write_ops - ds->write_ops;

			if (read_time < ds->read_time)
				diff_read_time = 1 + read_time
					+ (UINT_MAX - ds->read_time);
			else
				diff_read_time = read_time - ds->read_time;

			if (write_time < ds->write_time)
				diff_write_time = 1 + write_time
					+ (UINT_MAX - ds->write_time);
			else
				diff_write_time = write_time - ds->write_time;

			if (diff_read_ops != 0)
				ds->avg_read_time += (diff_read_time
						+ (diff_read_ops / 2))
					/ diff_read_ops;
			if (diff_write_ops != 0)
				ds->avg_write_time += (diff_write_time
						+ (diff_write_ops / 2))
					/ diff_write_ops;

			ds->read_ops = read_ops;
			ds->read_time = read_time;
			ds->write_ops = write_ops;
			ds->write_time = write_time;
		} /* if (is_disk) */

		/* Don't write to the RRDs if we've just started.. */
		ds->poll_count++;
		if (ds->poll_count <= 2)
		{
			DEBUG ("disk plugin: (ds->poll_count = %i) <= "
					"(min_poll_count = 2); => Not writing.",
					ds->poll_count);
			continue;
		}

		if ((read_ops == 0) && (write_ops == 0))
		{
			DEBUG ("disk plugin: ((read_ops == 0) && "
					"(write_ops == 0)); => Not writing.");
			continue;
		}

		if ((ds->read_bytes != 0) || (ds->write_bytes != 0))
			disk_submit (disk_name, "disk_octets",
					ds->read_bytes, ds->write_bytes);

		if ((ds->read_ops != 0) || (ds->write_ops != 0))
			disk_submit (disk_name, "disk_ops",
					read_ops, write_ops);

		if ((ds->avg_read_time != 0) || (ds->avg_write_time != 0))
			disk_submit (disk_name, "disk_time",
					ds->avg_read_time, ds->avg_write_time);

		if (is_disk)
		{
			disk_submit (disk_name, "disk_merged",
					read_merged, write_merged);
		} /* if (is_disk) */
	} /* while (fgets (buffer, sizeof (buffer), fh) != NULL) */

	fclose (fh);
/* #endif defined(KERNEL_LINUX) */

#elif HAVE_LIBKSTAT
# if HAVE_KSTAT_IO_T_WRITES && HAVE_KSTAT_IO_T_NWRITES && HAVE_KSTAT_IO_T_WTIME
#  define KIO_ROCTETS reads
#  define KIO_WOCTETS writes
#  define KIO_ROPS    nreads
#  define KIO_WOPS    nwrites
#  define KIO_RTIME   rtime
#  define KIO_WTIME   wtime
# elif HAVE_KSTAT_IO_T_NWRITTEN && HAVE_KSTAT_IO_T_WRITES && HAVE_KSTAT_IO_T_WTIME
#  define KIO_ROCTETS nread
#  define KIO_WOCTETS nwritten
#  define KIO_ROPS    reads
#  define KIO_WOPS    writes
#  define KIO_RTIME   rtime
#  define KIO_WTIME   wtime
# else
#  error "kstat_io_t does not have the required members"
# endif
	static kstat_io_t kio;
	int i;

	if (kc == NULL)
		return (-1);

	for (i = 0; i < numdisk; i++)
	{
		if (kstat_read (kc, ksp[i], &kio) == -1)
			continue;

		if (strncmp (ksp[i]->ks_class, "disk", 4) == 0)
		{
			disk_submit (ksp[i]->ks_name, "disk_octets",
					kio.KIO_ROCTETS, kio.KIO_WOCTETS);
			disk_submit (ksp[i]->ks_name, "disk_ops",
					kio.KIO_ROPS, kio.KIO_WOPS);
			/* FIXME: Convert this to microseconds if necessary */
			disk_submit (ksp[i]->ks_name, "disk_time",
					kio.KIO_RTIME, kio.KIO_WTIME);
		}
		else if (strncmp (ksp[i]->ks_class, "partition", 9) == 0)
		{
			disk_submit (ksp[i]->ks_name, "disk_octets",
					kio.KIO_ROCTETS, kio.KIO_WOCTETS);
			disk_submit (ksp[i]->ks_name, "disk_ops",
					kio.KIO_ROPS, kio.KIO_WOPS);
		}
	}
/* #endif defined(HAVE_LIBKSTAT) */

#elif defined(HAVE_LIBSTATGRAB)
	sg_disk_io_stats *ds;
	int disks, counter;
	char name[DATA_MAX_NAME_LEN];
	
	if ((ds = sg_get_disk_io_stats(&disks)) == NULL)
		return (0);
		
	for (counter=0; counter < disks; counter++) {
		strncpy(name, ds->disk_name, sizeof(name));
		name[sizeof(name)-1] = '\0'; /* strncpy doesn't terminate longer strings */
		disk_submit (name, "disk_octets", ds->read_bytes, ds->write_bytes);
		ds++;
	}
/* #endif defined(HAVE_LIBSTATGRAB) */

#elif defined(HAVE_PERFSTAT)
	derive_t read_sectors;
	derive_t write_sectors;
	derive_t read_time;
	derive_t write_time;
	derive_t read_ops;
	derive_t write_ops;
	perfstat_id_t firstpath;
	int rnumdisk;
	int i;

	if ((numdisk = perfstat_disk(NULL, NULL, sizeof(perfstat_disk_t), 0)) < 0) 
	{
		char errbuf[1024];
		WARNING ("disk plugin: perfstat_disk: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	if (numdisk != pnumdisk || stat_disk==NULL) {
		if (stat_disk!=NULL) 
			free(stat_disk);
		stat_disk = (perfstat_disk_t *)calloc(numdisk, sizeof(perfstat_disk_t));
	} 
	pnumdisk = numdisk;

	firstpath.name[0]='\0';
	if ((rnumdisk = perfstat_disk(&firstpath, stat_disk, sizeof(perfstat_disk_t), numdisk)) < 0) 
	{
		char errbuf[1024];
		WARNING ("disk plugin: perfstat_disk : %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	for (i = 0; i < rnumdisk; i++) 
	{
		read_sectors = stat_disk[i].rblks*stat_disk[i].bsize;
		write_sectors = stat_disk[i].wblks*stat_disk[i].bsize;
		disk_submit (stat_disk[i].name, "disk_octets", read_sectors, write_sectors);

		read_ops = stat_disk[i].xrate;
		write_ops = stat_disk[i].xfers - stat_disk[i].xrate;
		disk_submit (stat_disk[i].name, "disk_ops", read_ops, write_ops);

		read_time = stat_disk[i].rserv;
		read_time *= ((double)(_system_configuration.Xint)/(double)(_system_configuration.Xfrac)) / 1000000.0;
		write_time = stat_disk[i].wserv;
		write_time *= ((double)(_system_configuration.Xint)/(double)(_system_configuration.Xfrac)) / 1000000.0;
		disk_submit (stat_disk[i].name, "disk_time", read_time, write_time);
	}
#endif /* defined(HAVE_PERFSTAT) */

	return (0);
} /* int disk_read */

void module_register (void)
{
  plugin_register_config ("disk", disk_config,
      config_keys, config_keys_num);
  plugin_register_init ("disk", disk_init);
  plugin_register_read ("disk", disk_read);
} /* void module_register */
