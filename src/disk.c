/**
 * collectd - src/disk.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
#include "common.h"
#include "plugin.h"

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

#if HAVE_IOKIT_IOKITLIB_H || KERNEL_LINUX || HAVE_LIBKSTAT
# define DISK_HAVE_READ 1
#else
# define DISK_HAVE_READ 0
#endif

/* 2^34 = 17179869184 = ~17.2GByte/s */
static data_source_t octets_dsrc[2] =
{
	{"read",  DS_TYPE_COUNTER, 0, 17179869183.0},
	{"write", DS_TYPE_COUNTER, 0, 17179869183.0}
};

static data_set_t octets_ds =
{
	"disk_octets", 2, octets_dsrc
};

static data_source_t operations_dsrc[2] =
{
	{"read",  DS_TYPE_COUNTER, 0, 4294967295.0},
	{"write", DS_TYPE_COUNTER, 0, 4294967295.0}
};

static data_set_t operations_ds =
{
	"disk_ops", 2, operations_dsrc
};

static data_source_t merged_dsrc[2] =
{
	{"read",  DS_TYPE_COUNTER, 0, 4294967295.0},
	{"write", DS_TYPE_COUNTER, 0, 4294967295.0}
};

static data_set_t merged_ds =
{
	"disk_merged", 2, merged_dsrc
};

/* max is 1000000us per second. */
static data_source_t time_dsrc[2] =
{
	{"read",  DS_TYPE_COUNTER, 0, 1000000.0},
	{"write", DS_TYPE_COUNTER, 0, 1000000.0}
};

static data_set_t time_ds =
{
	"disk_time", 2, time_dsrc
};

#if DISK_HAVE_READ
#if HAVE_IOKIT_IOKITLIB_H
static mach_port_t io_master_port = MACH_PORT_NULL;
/* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_LINUX
typedef struct diskstats
{
	char *name;

	/* This overflows in roughly 1361 year */
	unsigned int poll_count;

	counter_t read_sectors;
	counter_t write_sectors;

	counter_t read_bytes;
	counter_t write_bytes;

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
		counter_t read, counter_t write)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = read;
	values[1].counter = write;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "disk");
	strncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));

	plugin_dispatch_values (type, &vl);
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

	static complain_t complain_obj;

	/* Get the list of all disk objects. */
	if (IOServiceGetMatchingServices (io_master_port,
				IOServiceMatching (kIOBlockStorageDriverClass),
				&disk_list) != kIOReturnSuccess)
	{
		plugin_complain (LOG_ERR, &complain_obj, "disk plugin: "
				"IOServiceGetMatchingServices failed.");
		return (-1);
	}
	else if (complain_obj.interval != 0)
	{
		plugin_relief (LOG_NOTICE, &complain_obj, "disk plugin: "
				"IOServiceGetMatchingServices succeeded.");
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

		if (snprintf (disk_name, 64, "%i-%i", disk_major, disk_minor) >= 64)
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

	int major = 0;
	int minor = 0;

	counter_t read_sectors  = 0;
	counter_t write_sectors = 0;

	counter_t read_count    = 0;
	counter_t read_merged   = 0;
	counter_t read_bytes    = 0;
	counter_t read_time     = 0;
	counter_t write_count   = 0;
	counter_t write_merged  = 0;
	counter_t write_bytes   = 0;
	counter_t write_time    = 0;
	int is_disk = 0;

	diskstats_t *ds, *pre_ds;

	static complain_t complain_obj;

	if ((fh = fopen ("/proc/diskstats", "r")) == NULL)
	{
		if ((fh = fopen ("/proc/partitions", "r")) == NULL)
		{
			plugin_complain (LOG_ERR, &complain_obj,
					"disk plugin: Failed to open /proc/"
					"{diskstats,partitions}.");
			return (-1);
		}

		/* Kernel is 2.4.* */
		fieldshift = 1;
	}

	plugin_relief (LOG_NOTICE, &complain_obj, "disk plugin: "
			"Succeeded to open /proc/{diskstats,partitions}.");

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *disk_name;

		numfields = strsplit (buffer, fields, 32);

		if ((numfields != (14 + fieldshift)) && (numfields != 7))
			continue;

		major = atoll (fields[0]);
		minor = atoll (fields[1]);

		disk_name = fields[2];

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
			read_sectors  = atoll (fields[4]);
			write_count   = atoll (fields[5]);
			write_sectors = atoll (fields[6]);
		}
		else if (numfields == (14 + fieldshift))
		{
			read_count  =  atoll (fields[3 + fieldshift]);
			write_count =  atoll (fields[7 + fieldshift]);

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

		/* If the counter wraps around, it's only 32 bits.. */
		if (read_sectors < ds->read_sectors)
			ds->read_bytes += 512 * ((0xFFFFFFFF - ds->read_sectors) + read_sectors);
		else
			ds->read_bytes += 512 * (read_sectors - ds->read_sectors);

		if (write_sectors < ds->write_sectors)
			ds->write_bytes += 512 * ((0xFFFFFFFF - ds->write_sectors) + write_sectors);
		else
			ds->write_bytes += 512 * (write_sectors - ds->write_sectors);

		ds->read_sectors  = read_sectors;
		ds->write_sectors = write_sectors;
		read_bytes  = ds->read_bytes;
		write_bytes = ds->write_bytes;

		/* Don't write to the RRDs if we've just started.. */
		ds->poll_count++;
		if (ds->poll_count <= 2)
		{
			DEBUG ("(ds->poll_count = %i) <= (min_poll_count = 2); => Not writing.",
					ds->poll_count);
			continue;
		}

		if ((read_count == 0) && (write_count == 0))
		{
			DEBUG ("((read_count == 0) && (write_count == 0)); => Not writing.");
			continue;
		}

		if ((read_bytes != -1LL) || (write_bytes != -1LL))
			disk_submit (disk_name, "disk_octets", read_bytes, write_bytes);
		if ((read_count != -1LL) || (write_count != -1LL))
			disk_submit (disk_name, "disk_ops", read_count, write_count);
		if (is_disk)
		{
			if ((read_merged != -1LL) || (write_merged != -1LL))
				disk_submit (disk_name, "disk_merged",
						read_merged, write_merged);
			if ((read_time != -1LL) || (write_time != -1LL))
				disk_submit (disk_name, "disk_time",
						read_time * 1000,
						write_time * 1000);
		}
	} /* while (fgets (buffer, sizeof (buffer), fh) != NULL) */

	fclose (fh);
/* #endif defined(KERNEL_LINUX) */

#elif HAVE_LIBKSTAT
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
			disk_submit (ksp[i]->ks_name, "disk_octets", kio.nread, kio.nwritten);
			disk_submit (ksp[i]->ks_name, "disk_ops", kio.reads, kio.writes);
			/* FIXME: Convert this to microseconds if necessary */
			disk_submit (ksp[i]->ks_name, "disk_time", kio.rtime, kio.wtime);
		}
		else if (strncmp (ksp[i]->ks_class, "partition", 9) == 0)
		{
			disk_submit (ksp[i]->ks_name, "disk_octets", kio.nread, kio.nwritten);
			disk_submit (ksp[i]->ks_name, "disk_ops", kio.reads, kio.writes);
		}
	}
#endif /* defined(HAVE_LIBKSTAT) */

	return (0);
} /* int disk_read */
#endif /* DISK_HAVE_READ */

void module_register (modreg_e load)
{
	if (load & MR_DATASETS)
	{
		plugin_register_data_set (&octets_ds);
		plugin_register_data_set (&operations_ds);
		plugin_register_data_set (&merged_ds);
		plugin_register_data_set (&time_ds);
	}

#if DISK_HAVE_READ
	if (load & MR_READ)
	{
		plugin_register_init ("disk", disk_init);
		plugin_register_read ("disk", disk_read);
	}
#endif /* DISK_HAVE_READ */
} /* void module_register */
