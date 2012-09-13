/**
 * collectd - src/processes.c
 * Copyright (C) 2005       Lyonel Vincent
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Oleg King
 * Copyright (C) 2009       Sebastian Harl
 * Copyright (C) 2009       Andrés J. Díaz
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2010       Clément Stenac
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
 *   Lyonel Vincent <lyonel at ezix.org>
 *   Florian octo Forster <octo at verplant.org>
 *   Oleg King <king2 at kaluga.ru>
 *   Sebastian Harl <sh at tokkee.org>
 *   Andrés J. Díaz <ajdiaz at connectical.com>
 *   Manuel Sanmartin
 *   Clément Stenac <clement.stenac at diwi.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

/* Include header files for the mach system, if they exist.. */
#if HAVE_THREAD_INFO
#  if HAVE_MACH_MACH_INIT_H
#    include <mach/mach_init.h>
#  endif
#  if HAVE_MACH_HOST_PRIV_H
#    include <mach/host_priv.h>
#  endif
#  if HAVE_MACH_MACH_ERROR_H
#    include <mach/mach_error.h>
#  endif
#  if HAVE_MACH_MACH_HOST_H
#    include <mach/mach_host.h>
#  endif
#  if HAVE_MACH_MACH_PORT_H
#    include <mach/mach_port.h>
#  endif
#  if HAVE_MACH_MACH_TYPES_H
#    include <mach/mach_types.h>
#  endif
#  if HAVE_MACH_MESSAGE_H
#    include <mach/message.h>
#  endif
#  if HAVE_MACH_PROCESSOR_SET_H
#    include <mach/processor_set.h>
#  endif
#  if HAVE_MACH_TASK_H
#    include <mach/task.h>
#  endif
#  if HAVE_MACH_THREAD_ACT_H
#    include <mach/thread_act.h>
#  endif
#  if HAVE_MACH_VM_REGION_H
#    include <mach/vm_region.h>
#  endif
#  if HAVE_MACH_VM_MAP_H
#    include <mach/vm_map.h>
#  endif
#  if HAVE_MACH_VM_PROT_H
#    include <mach/vm_prot.h>
#  endif
#  if HAVE_SYS_SYSCTL_H
#    include <sys/sysctl.h>
#  endif
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
#  if HAVE_LINUX_CONFIG_H
#    include <linux/config.h>
#  endif
#  ifndef CONFIG_HZ
#    define CONFIG_HZ 100
#  endif
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
#  include <kvm.h>
#  include <sys/param.h>
#  include <sys/sysctl.h>
#  include <sys/user.h>
#  include <sys/proc.h>
/* #endif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

#elif HAVE_PROCINFO_H
#  include <procinfo.h>
#  include <sys/types.h>

#define MAXPROCENTRY 32
#define MAXTHRDENTRY 16
#define MAXARGLN 1024
/* #endif HAVE_PROCINFO_H */

#else
# error "No applicable input method."
#endif

#if HAVE_REGEX_H
# include <regex.h>
#endif

#ifndef ARG_MAX
#  define ARG_MAX 4096
#endif

typedef struct procstat_entry_s
{
	unsigned long id;
	unsigned long age;

	unsigned long num_proc;
	unsigned long num_lwp;
	unsigned long vmem_size;
	unsigned long vmem_rss;
	unsigned long vmem_data;
	unsigned long vmem_code;
	unsigned long stack_size;

	unsigned long vmem_minflt;
	unsigned long vmem_majflt;
	derive_t      vmem_minflt_counter;
	derive_t      vmem_majflt_counter;

	unsigned long cpu_user;
	unsigned long cpu_system;
	derive_t      cpu_user_counter;
	derive_t      cpu_system_counter;

	/* io data */
	derive_t io_rchar;
	derive_t io_wchar;
	derive_t io_syscr;
	derive_t io_syscw;

	struct procstat_entry_s *next;
} procstat_entry_t;

#define PROCSTAT_NAME_LEN 256
typedef struct procstat
{
	char          name[PROCSTAT_NAME_LEN];
#if HAVE_REGEX_H
	regex_t *re;
#endif

	unsigned long num_proc;
	unsigned long num_lwp;
	unsigned long vmem_size;
	unsigned long vmem_rss;
	unsigned long vmem_data;
	unsigned long vmem_code;
	unsigned long stack_size;

	derive_t vmem_minflt_counter;
	derive_t vmem_majflt_counter;

	derive_t cpu_user_counter;
	derive_t cpu_system_counter;

	/* io data */
	derive_t io_rchar;
	derive_t io_wchar;
	derive_t io_syscr;
	derive_t io_syscw;

	struct procstat   *next;
	struct procstat_entry_s *instances;
} procstat_t;

static procstat_t *list_head_g = NULL;

#if HAVE_THREAD_INFO
static mach_port_t port_host_self;
static mach_port_t port_task_self;

static processor_set_name_array_t pset_list;
static mach_msg_type_number_t     pset_list_len;
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
static long pagesize_g;
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
static int pagesize;
/* #endif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

#elif HAVE_PROCINFO_H
static  struct procentry64 procentry[MAXPROCENTRY];
static  struct thrdentry64 thrdentry[MAXTHRDENTRY];
static int pagesize;

#ifndef _AIXVERSION_610
int     getprocs64 (void *procsinfo, int sizproc, void *fdsinfo, int sizfd, pid_t *index, int count);
int     getthrds64( pid_t, void *, int, tid64_t *, int );
#endif
int getargs (struct procentry64 *processBuffer, int bufferLen, char *argsBuffer, int argsLen);
#endif /* HAVE_PROCINFO_H */

/* put name of process from config to list_head_g tree
   list_head_g is a list of 'procstat_t' structs with
   processes names we want to watch */
static void ps_list_register (const char *name, const char *regexp)
{
	procstat_t *new;
	procstat_t *ptr;
	int status;

	new = (procstat_t *) malloc (sizeof (procstat_t));
	if (new == NULL)
	{
		ERROR ("processes plugin: ps_list_register: malloc failed.");
		return;
	}
	memset (new, 0, sizeof (procstat_t));
	sstrncpy (new->name, name, sizeof (new->name));

#if HAVE_REGEX_H
	if (regexp != NULL)
	{
		DEBUG ("ProcessMatch: adding \"%s\" as criteria to process %s.", regexp, name);
		new->re = (regex_t *) malloc (sizeof (regex_t));
		if (new->re == NULL)
		{
			ERROR ("processes plugin: ps_list_register: malloc failed.");
			sfree (new);
			return;
		}

		status = regcomp (new->re, regexp, REG_EXTENDED | REG_NOSUB);
		if (status != 0)
		{
			DEBUG ("ProcessMatch: compiling the regular expression \"%s\" failed.", regexp);
			sfree(new->re);
			return;
		}
	}
#else
	if (regexp != NULL)
	{
		ERROR ("processes plugin: ps_list_register: "
				"Regular expression \"%s\" found in config "
				"file, but support for regular expressions "
				"has been disabled at compile time.",
				regexp);
		sfree (new);
		return;
	}
#endif

	for (ptr = list_head_g; ptr != NULL; ptr = ptr->next)
	{
		if (strcmp (ptr->name, name) == 0)
		{
			WARNING ("processes plugin: You have configured more "
					"than one `Process' or "
					"`ProcessMatch' with the same name. "
					"All but the first setting will be "
					"ignored.");
			sfree (new->re);
			sfree (new);
			return;
		}

		if (ptr->next == NULL)
			break;
	}

	if (ptr == NULL)
		list_head_g = new;
	else
		ptr->next = new;
} /* void ps_list_register */

/* try to match name against entry, returns 1 if success */
static int ps_list_match (const char *name, const char *cmdline, procstat_t *ps)
{
#if HAVE_REGEX_H
	if (ps->re != NULL)
	{
		int status;
		const char *str;

		str = cmdline;
		if ((str == NULL) || (str[0] == 0))
			str = name;

		assert (str != NULL);

		status = regexec (ps->re, str,
				/* nmatch = */ 0,
				/* pmatch = */ NULL,
				/* eflags = */ 0);
		if (status == 0)
			return (1);
	}
	else
#endif
	if (strcmp (ps->name, name) == 0)
		return (1);

	return (0);
} /* int ps_list_match */

/* add process entry to 'instances' of process 'name' (or refresh it) */
static void ps_list_add (const char *name, const char *cmdline, procstat_entry_t *entry)
{
	procstat_t *ps;
	procstat_entry_t *pse;

	if (entry->id == 0)
		return;

	for (ps = list_head_g; ps != NULL; ps = ps->next)
	{
		if ((ps_list_match (name, cmdline, ps)) == 0)
			continue;

		for (pse = ps->instances; pse != NULL; pse = pse->next)
			if ((pse->id == entry->id) || (pse->next == NULL))
				break;

		if ((pse == NULL) || (pse->id != entry->id))
		{
			procstat_entry_t *new;

			new = (procstat_entry_t *) malloc (sizeof (procstat_entry_t));
			if (new == NULL)
				return;
			memset (new, 0, sizeof (procstat_entry_t));
			new->id = entry->id;

			if (pse == NULL)
				ps->instances = new;
			else
				pse->next = new;

			pse = new;
		}

		pse->age = 0;
		pse->num_proc   = entry->num_proc;
		pse->num_lwp    = entry->num_lwp;
		pse->vmem_size  = entry->vmem_size;
		pse->vmem_rss   = entry->vmem_rss;
		pse->vmem_data  = entry->vmem_data;
		pse->vmem_code  = entry->vmem_code;
		pse->stack_size = entry->stack_size;
		pse->io_rchar   = entry->io_rchar;
		pse->io_wchar   = entry->io_wchar;
		pse->io_syscr   = entry->io_syscr;
		pse->io_syscw   = entry->io_syscw;

		ps->num_proc   += pse->num_proc;
		ps->num_lwp    += pse->num_lwp;
		ps->vmem_size  += pse->vmem_size;
		ps->vmem_rss   += pse->vmem_rss;
		ps->vmem_data  += pse->vmem_data;
		ps->vmem_code  += pse->vmem_code;
		ps->stack_size += pse->stack_size;

		ps->io_rchar   += ((pse->io_rchar == -1)?0:pse->io_rchar);
		ps->io_wchar   += ((pse->io_wchar == -1)?0:pse->io_wchar);
		ps->io_syscr   += ((pse->io_syscr == -1)?0:pse->io_syscr);
		ps->io_syscw   += ((pse->io_syscw == -1)?0:pse->io_syscw);

		if ((entry->vmem_minflt_counter == 0)
				&& (entry->vmem_majflt_counter == 0))
		{
			pse->vmem_minflt_counter += entry->vmem_minflt;
			pse->vmem_minflt = entry->vmem_minflt;

			pse->vmem_majflt_counter += entry->vmem_majflt;
			pse->vmem_majflt = entry->vmem_majflt;
		}
		else
		{
			if (entry->vmem_minflt_counter < pse->vmem_minflt_counter)
			{
				pse->vmem_minflt = entry->vmem_minflt_counter
					+ (ULONG_MAX - pse->vmem_minflt_counter);
			}
			else
			{
				pse->vmem_minflt = entry->vmem_minflt_counter - pse->vmem_minflt_counter;
			}
			pse->vmem_minflt_counter = entry->vmem_minflt_counter;

			if (entry->vmem_majflt_counter < pse->vmem_majflt_counter)
			{
				pse->vmem_majflt = entry->vmem_majflt_counter
					+ (ULONG_MAX - pse->vmem_majflt_counter);
			}
			else
			{
				pse->vmem_majflt = entry->vmem_majflt_counter - pse->vmem_majflt_counter;
			}
			pse->vmem_majflt_counter = entry->vmem_majflt_counter;
		}

		ps->vmem_minflt_counter += pse->vmem_minflt;
		ps->vmem_majflt_counter += pse->vmem_majflt;

		if ((entry->cpu_user_counter == 0)
				&& (entry->cpu_system_counter == 0))
		{
			pse->cpu_user_counter += entry->cpu_user;
			pse->cpu_user = entry->cpu_user;

			pse->cpu_system_counter += entry->cpu_system;
			pse->cpu_system = entry->cpu_system;
		}
		else
		{
			if (entry->cpu_user_counter < pse->cpu_user_counter)
			{
				pse->cpu_user = entry->cpu_user_counter
					+ (ULONG_MAX - pse->cpu_user_counter);
			}
			else
			{
				pse->cpu_user = entry->cpu_user_counter - pse->cpu_user_counter;
			}
			pse->cpu_user_counter = entry->cpu_user_counter;

			if (entry->cpu_system_counter < pse->cpu_system_counter)
			{
				pse->cpu_system = entry->cpu_system_counter
					+ (ULONG_MAX - pse->cpu_system_counter);
			}
			else
			{
				pse->cpu_system = entry->cpu_system_counter - pse->cpu_system_counter;
			}
			pse->cpu_system_counter = entry->cpu_system_counter;
		}

		ps->cpu_user_counter   += pse->cpu_user;
		ps->cpu_system_counter += pse->cpu_system;
	}
}

/* remove old entries from instances of processes in list_head_g */
static void ps_list_reset (void)
{
	procstat_t *ps;
	procstat_entry_t *pse;
	procstat_entry_t *pse_prev;

	for (ps = list_head_g; ps != NULL; ps = ps->next)
	{
		ps->num_proc    = 0;
		ps->num_lwp     = 0;
		ps->vmem_size   = 0;
		ps->vmem_rss    = 0;
		ps->vmem_data   = 0;
		ps->vmem_code   = 0;
		ps->stack_size  = 0;
		ps->io_rchar = -1;
		ps->io_wchar = -1;
		ps->io_syscr = -1;
		ps->io_syscw = -1;

		pse_prev = NULL;
		pse = ps->instances;
		while (pse != NULL)
		{
			if (pse->age > 10)
			{
				DEBUG ("Removing this procstat entry cause it's too old: "
						"id = %lu; name = %s;",
						pse->id, ps->name);

				if (pse_prev == NULL)
				{
					ps->instances = pse->next;
					free (pse);
					pse = ps->instances;
				}
				else
				{
					pse_prev->next = pse->next;
					free (pse);
					pse = pse_prev->next;
				}
			}
			else
			{
				pse->age++;
				pse_prev = pse;
				pse = pse->next;
			}
		} /* while (pse != NULL) */
	} /* for (ps = list_head_g; ps != NULL; ps = ps->next) */
}

/* put all pre-defined 'Process' names from config to list_head_g tree */
static int ps_config (oconfig_item_t *ci)
{
	int i;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (strcasecmp (c->key, "Process") == 0)
		{
			if ((c->values_num != 1)
					|| (OCONFIG_TYPE_STRING != c->values[0].type)) {
				ERROR ("processes plugin: `Process' expects exactly "
						"one string argument (got %i).",
						c->values_num);
				continue;
			}

			if (c->children_num != 0) {
				WARNING ("processes plugin: the `Process' config option "
						"does not expect any child elements -- ignoring "
						"content (%i elements) of the <Process '%s'> block.",
						c->children_num, c->values[0].value.string);
			}

			ps_list_register (c->values[0].value.string, NULL);
		}
		else if (strcasecmp (c->key, "ProcessMatch") == 0)
		{
			if ((c->values_num != 2)
					|| (OCONFIG_TYPE_STRING != c->values[0].type)
					|| (OCONFIG_TYPE_STRING != c->values[1].type))
			{
				ERROR ("processes plugin: `ProcessMatch' needs exactly "
						"two string arguments (got %i).",
						c->values_num);
				continue;
			}

			if (c->children_num != 0) {
				WARNING ("processes plugin: the `ProcessMatch' config option "
						"does not expect any child elements -- ignoring "
						"content (%i elements) of the <ProcessMatch '%s' '%s'> "
						"block.", c->children_num, c->values[0].value.string,
						c->values[1].value.string);
			}

			ps_list_register (c->values[0].value.string,
					c->values[1].value.string);
		}
		else
		{
			ERROR ("processes plugin: The `%s' configuration option is not "
					"understood and will be ignored.", c->key);
			continue;
		}
	}

	return (0);
}

static int ps_init (void)
{
#if HAVE_THREAD_INFO
	kern_return_t status;

	port_host_self = mach_host_self ();
	port_task_self = mach_task_self ();

	if (pset_list != NULL)
	{
		vm_deallocate (port_task_self,
				(vm_address_t) pset_list,
				pset_list_len * sizeof (processor_set_t));
		pset_list = NULL;
		pset_list_len = 0;
	}

	if ((status = host_processor_sets (port_host_self,
					&pset_list,
				       	&pset_list_len)) != KERN_SUCCESS)
	{
		ERROR ("host_processor_sets failed: %s\n",
			       	mach_error_string (status));
		pset_list = NULL;
		pset_list_len = 0;
		return (-1);
	}
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
	pagesize_g = sysconf(_SC_PAGESIZE);
	DEBUG ("pagesize_g = %li; CONFIG_HZ = %i;",
			pagesize_g, CONFIG_HZ);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
	pagesize = getpagesize();
/* #endif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

#elif HAVE_PROCINFO_H
	pagesize = getpagesize();
#endif /* HAVE_PROCINFO_H */

	return (0);
} /* int ps_init */

/* submit global state (e.g.: qty of zombies, running, etc..) */
static void ps_submit_state (const char *state, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "processes", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "ps_state", sizeof (vl.type));
	sstrncpy (vl.type_instance, state, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

/* submit info about specific process (e.g.: memory taken, cpu usage, etc..) */
static void ps_submit_proc_list (procstat_t *ps)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "processes", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, ps->name, sizeof (vl.plugin_instance));

	sstrncpy (vl.type, "ps_vm", sizeof (vl.type));
	vl.values[0].gauge = ps->vmem_size;
	vl.values_len = 1;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_rss", sizeof (vl.type));
	vl.values[0].gauge = ps->vmem_rss;
	vl.values_len = 1;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_data", sizeof (vl.type));
	vl.values[0].gauge = ps->vmem_data;
	vl.values_len = 1;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_code", sizeof (vl.type));
	vl.values[0].gauge = ps->vmem_code;
	vl.values_len = 1;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_stacksize", sizeof (vl.type));
	vl.values[0].gauge = ps->stack_size;
	vl.values_len = 1;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_cputime", sizeof (vl.type));
	vl.values[0].derive = ps->cpu_user_counter;
	vl.values[1].derive = ps->cpu_system_counter;
	vl.values_len = 2;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_count", sizeof (vl.type));
	vl.values[0].gauge = ps->num_proc;
	vl.values[1].gauge = ps->num_lwp;
	vl.values_len = 2;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_pagefaults", sizeof (vl.type));
	vl.values[0].derive = ps->vmem_minflt_counter;
	vl.values[1].derive = ps->vmem_majflt_counter;
	vl.values_len = 2;
	plugin_dispatch_values (&vl);

	if ( (ps->io_rchar != -1) && (ps->io_wchar != -1) )
	{
		sstrncpy (vl.type, "ps_disk_octets", sizeof (vl.type));
		vl.values[0].derive = ps->io_rchar;
		vl.values[1].derive = ps->io_wchar;
		vl.values_len = 2;
		plugin_dispatch_values (&vl);
	}

	if ( (ps->io_syscr != -1) && (ps->io_syscw != -1) )
	{
		sstrncpy (vl.type, "ps_disk_ops", sizeof (vl.type));
		vl.values[0].derive = ps->io_syscr;
		vl.values[1].derive = ps->io_syscw;
		vl.values_len = 2;
		plugin_dispatch_values (&vl);
	}

	DEBUG ("name = %s; num_proc = %lu; num_lwp = %lu; "
                        "vmem_size = %lu; vmem_rss = %lu; vmem_data = %lu; "
			"vmem_code = %lu; "
			"vmem_minflt_counter = %"PRIi64"; vmem_majflt_counter = %"PRIi64"; "
			"cpu_user_counter = %"PRIi64"; cpu_system_counter = %"PRIi64"; "
			"io_rchar = %"PRIi64"; io_wchar = %"PRIi64"; "
			"io_syscr = %"PRIi64"; io_syscw = %"PRIi64";",
			ps->name, ps->num_proc, ps->num_lwp,
			ps->vmem_size, ps->vmem_rss,
			ps->vmem_data, ps->vmem_code,
			ps->vmem_minflt_counter, ps->vmem_majflt_counter,
			ps->cpu_user_counter, ps->cpu_system_counter,
			ps->io_rchar, ps->io_wchar, ps->io_syscr, ps->io_syscw);
} /* void ps_submit_proc_list */

/* ------- additional functions for KERNEL_LINUX/HAVE_THREAD_INFO ------- */
#if KERNEL_LINUX
static int ps_read_tasks (int pid)
{
	char           dirname[64];
	DIR           *dh;
	struct dirent *ent;
	int count = 0;

	ssnprintf (dirname, sizeof (dirname), "/proc/%i/task", pid);

	if ((dh = opendir (dirname)) == NULL)
	{
		DEBUG ("Failed to open directory `%s'", dirname);
		return (-1);
	}

	while ((ent = readdir (dh)) != NULL)
	{
		if (!isdigit ((int) ent->d_name[0]))
			continue;
		else
			count++;
	}
	closedir (dh);

	return ((count >= 1) ? count : 1);
} /* int *ps_read_tasks */

/* Read advanced virtual memory data from /proc/pid/status */
static procstat_t *ps_read_vmem (int pid, procstat_t *ps)
{
	FILE *fh;
	char buffer[1024];
	char filename[64];
	unsigned long long lib = 0;
	unsigned long long exe = 0;
	unsigned long long data = 0;
	char *fields[8];
	int numfields;

	ssnprintf (filename, sizeof (filename), "/proc/%i/status", pid);
	if ((fh = fopen (filename, "r")) == NULL)
		return (NULL);

	while (fgets (buffer, sizeof(buffer), fh) != NULL)
	{
		long long tmp;
		char *endptr;

		if (strncmp (buffer, "Vm", 2) != 0)
			continue;

		numfields = strsplit (buffer, fields,
                                      STATIC_ARRAY_SIZE (fields));

		if (numfields < 2)
			continue;

		errno = 0;
		endptr = NULL;
		tmp = strtoll (fields[1], &endptr, /* base = */ 10);
		if ((errno == 0) && (endptr != fields[1]))
		{
			if (strncmp (buffer, "VmData", 6) == 0)
			{
				data = tmp;
			}
			else if (strncmp (buffer, "VmLib", 5) == 0)
			{
				lib = tmp;
			}
			else if  (strncmp(buffer, "VmExe", 5) == 0)
			{
				exe = tmp;
			}
		}
	} /* while (fgets) */

	if (fclose (fh))
	{
		char errbuf[1024];
		WARNING ("processes: fclose: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

	ps->vmem_data = data * 1024;
	ps->vmem_code = (exe + lib) * 1024;

	return (ps);
} /* procstat_t *ps_read_vmem */

static procstat_t *ps_read_io (int pid, procstat_t *ps)
{
	FILE *fh;
	char buffer[1024];
	char filename[64];

	char *fields[8];
	int numfields;

	ssnprintf (filename, sizeof (filename), "/proc/%i/io", pid);
	if ((fh = fopen (filename, "r")) == NULL)
		return (NULL);

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		derive_t *val = NULL;
		long long tmp;
		char *endptr;

		if (strncasecmp (buffer, "rchar:", 6) == 0)
			val = &(ps->io_rchar);
		else if (strncasecmp (buffer, "wchar:", 6) == 0)
			val = &(ps->io_wchar);
		else if (strncasecmp (buffer, "syscr:", 6) == 0)
			val = &(ps->io_syscr);
		else if (strncasecmp (buffer, "syscw:", 6) == 0)
			val = &(ps->io_syscw);
		else
			continue;

		numfields = strsplit (buffer, fields,
				STATIC_ARRAY_SIZE (fields));

		if (numfields < 2)
			continue;

		errno = 0;
		endptr = NULL;
		tmp = strtoll (fields[1], &endptr, /* base = */ 10);
		if ((errno != 0) || (endptr == fields[1]))
			*val = -1;
		else
			*val = (derive_t) tmp;
	} /* while (fgets) */

	if (fclose (fh))
	{
		char errbuf[1024];
		WARNING ("processes: fclose: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

	return (ps);
} /* procstat_t *ps_read_io */

int ps_read_process (int pid, procstat_t *ps, char *state)
{
	char  filename[64];
	char  buffer[1024];

	char *fields[64];
	char  fields_len;

	int   buffer_len;

	char *buffer_ptr;
	size_t name_start_pos;
	size_t name_end_pos;
	size_t name_len;

	derive_t cpu_user_counter;
	derive_t cpu_system_counter;
	long long unsigned vmem_size;
	long long unsigned vmem_rss;
	long long unsigned stack_size;

	memset (ps, 0, sizeof (procstat_t));

	ssnprintf (filename, sizeof (filename), "/proc/%i/stat", pid);

	buffer_len = read_file_contents (filename,
			buffer, sizeof(buffer) - 1);
	if (buffer_len <= 0)
		return (-1);
	buffer[buffer_len] = 0;

	/* The name of the process is enclosed in parens. Since the name can
	 * contain parens itself, spaces, numbers and pretty much everything
	 * else, use these to determine the process name. We don't use
	 * strchr(3) and strrchr(3) to avoid pointer arithmetic which would
	 * otherwise be required to determine name_len. */
	name_start_pos = 0;
	while ((buffer[name_start_pos] != '(')
			&& (name_start_pos < buffer_len))
		name_start_pos++;

	name_end_pos = buffer_len;
	while ((buffer[name_end_pos] != ')')
			&& (name_end_pos > 0))
		name_end_pos--;

	/* Either '(' or ')' is not found or they are in the wrong order.
	 * Anyway, something weird that shouldn't happen ever. */
	if (name_start_pos >= name_end_pos)
	{
		ERROR ("processes plugin: name_start_pos = %zu >= name_end_pos = %zu",
				name_start_pos, name_end_pos);
		return (-1);
	}

	name_len = (name_end_pos - name_start_pos) - 1;
	if (name_len >= sizeof (ps->name))
		name_len = sizeof (ps->name) - 1;

	sstrncpy (ps->name, &buffer[name_start_pos + 1], name_len + 1);

	if ((buffer_len - name_end_pos) < 2)
		return (-1);
	buffer_ptr = &buffer[name_end_pos + 2];

	fields_len = strsplit (buffer_ptr, fields, STATIC_ARRAY_SIZE (fields));
	if (fields_len < 22)
	{
		DEBUG ("processes plugin: ps_read_process (pid = %i):"
				" `%s' has only %i fields..",
				(int) pid, filename, fields_len);
		return (-1);
	}

	*state = fields[0][0];

	if (*state == 'Z')
	{
		ps->num_lwp  = 0;
		ps->num_proc = 0;
	}
	else
	{
		if ( (ps->num_lwp = ps_read_tasks (pid)) == -1 )
		{
			/* returns -1 => kernel 2.4 */
			ps->num_lwp = 1;
		}
		ps->num_proc = 1;
	}

	/* Leave the rest at zero if this is only a zombi */
	if (ps->num_proc == 0)
	{
		DEBUG ("processes plugin: This is only a zombi: pid = %i; "
				"name = %s;", pid, ps->name);
		return (0);
	}

	cpu_user_counter   = atoll (fields[11]);
	cpu_system_counter = atoll (fields[12]);
	vmem_size          = atoll (fields[20]);
	vmem_rss           = atoll (fields[21]);
	ps->vmem_minflt_counter = atol (fields[7]);
	ps->vmem_majflt_counter = atol (fields[9]);

	{
		unsigned long long stack_start = atoll (fields[25]);
		unsigned long long stack_ptr   = atoll (fields[26]);

		stack_size = (stack_start > stack_ptr)
			? stack_start - stack_ptr
			: stack_ptr - stack_start;
	}

	/* Convert jiffies to useconds */
	cpu_user_counter   = cpu_user_counter   * 1000000 / CONFIG_HZ;
	cpu_system_counter = cpu_system_counter * 1000000 / CONFIG_HZ;
	vmem_rss = vmem_rss * pagesize_g;

	if ( (ps_read_vmem(pid, ps)) == NULL)
	{
		/* No VMem data */
		ps->vmem_data = -1;
		ps->vmem_code = -1;
		DEBUG("ps_read_process: did not get vmem data for pid %i",pid);
	}

	ps->cpu_user_counter = cpu_user_counter;
	ps->cpu_system_counter = cpu_system_counter;
	ps->vmem_size = (unsigned long) vmem_size;
	ps->vmem_rss = (unsigned long) vmem_rss;
	ps->stack_size = (unsigned long) stack_size;

	if ( (ps_read_io (pid, ps)) == NULL)
	{
		/* no io data */
		ps->io_rchar = -1;
		ps->io_wchar = -1;
		ps->io_syscr = -1;
		ps->io_syscw = -1;

		DEBUG("ps_read_process: not get io data for pid %i",pid);
	}

	/* success */
	return (0);
} /* int ps_read_process (...) */

static char *ps_get_cmdline (pid_t pid, char *name, char *buf, size_t buf_len)
{
	char  *buf_ptr;
	size_t len;

	char file[PATH_MAX];
	int  fd;

	size_t n;

	if ((pid < 1) || (NULL == buf) || (buf_len < 2))
		return NULL;

	ssnprintf (file, sizeof (file), "/proc/%u/cmdline",
		       	(unsigned int) pid);

	errno = 0;
	fd = open (file, O_RDONLY);
	if (fd < 0) {
		char errbuf[4096];
		/* ENOENT means the process exited while we were handling it.
		 * Don't complain about this, it only fills the logs. */
		if (errno != ENOENT)
			WARNING ("processes plugin: Failed to open `%s': %s.", file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		return NULL;
	}

	buf_ptr = buf;
	len     = buf_len;

	n = 0;

	while (42) {
		ssize_t status;

		status = read (fd, (void *)buf_ptr, len);

		if (status < 0) {
			char errbuf[1024];

			if ((EAGAIN == errno) || (EINTR == errno))
				continue;

			WARNING ("processes plugin: Failed to read from `%s': %s.", file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (fd);
			return NULL;
		}

		n += status;

		if (status == 0)
			break;

		buf_ptr += status;
		len     -= status;

		if (len <= 0)
			break;
	}

	close (fd);

	if (0 == n) {
		/* cmdline not available; e.g. kernel thread, zombie */
		if (NULL == name)
			return NULL;

		ssnprintf (buf, buf_len, "[%s]", name);
		return buf;
	}

	assert (n <= buf_len);

	if (n == buf_len)
		--n;
	buf[n] = '\0';

	--n;
	/* remove trailing whitespace */
	while ((n > 0) && (isspace (buf[n]) || ('\0' == buf[n]))) {
		buf[n] = '\0';
		--n;
	}

	/* arguments are separated by '\0' in /proc/<pid>/cmdline */
	while (n > 0) {
		if ('\0' == buf[n])
			buf[n] = ' ';
		--n;
	}
	return buf;
} /* char *ps_get_cmdline (...) */

static unsigned long read_fork_rate ()
{
	FILE *proc_stat;
	char buf[1024];
	unsigned long result = 0;
	int numfields;
	char *fields[3];

	proc_stat = fopen("/proc/stat", "r");
	if (proc_stat == NULL) {
		char errbuf[1024];
		ERROR ("processes plugin: fopen (/proc/stat) failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return ULONG_MAX;
	}

	while (fgets (buf, sizeof(buf), proc_stat) != NULL)
	{
		char *endptr;

		numfields = strsplit(buf, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields != 2)
			continue;

		if (strcmp ("processes", fields[0]) != 0)
			continue;

		errno = 0;
		endptr = NULL;
		result = strtoul(fields[1], &endptr, /* base = */ 10);
		if ((endptr == fields[1]) || (errno != 0)) {
			ERROR ("processes plugin: Cannot parse fork rate: %s",
					fields[1]);
			result = ULONG_MAX;
			break;
		}

		break;
	}

	fclose(proc_stat);

	return result;
}

static void ps_submit_fork_rate (unsigned long value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = (derive_t) value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "processes", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "fork_rate", sizeof (vl.type));
	sstrncpy (vl.type_instance, "", sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

#endif /* KERNEL_LINUX */

#if HAVE_THREAD_INFO
static int mach_get_task_name (task_t t, int *pid, char *name, size_t name_max_len)
{
	int mib[4];

	struct kinfo_proc kp;
	size_t            kp_size;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;

	if (pid_for_task (t, pid) != KERN_SUCCESS)
		return (-1);
	mib[3] = *pid;

	kp_size = sizeof (kp);
	if (sysctl (mib, 4, &kp, &kp_size, NULL, 0) != 0)
		return (-1);

	if (name_max_len > (MAXCOMLEN + 1))
		name_max_len = MAXCOMLEN + 1;

	strncpy (name, kp.kp_proc.p_comm, name_max_len - 1);
	name[name_max_len - 1] = '\0';

	DEBUG ("pid = %i; name = %s;", *pid, name);

	/* We don't do the special handling for `p_comm == "LaunchCFMApp"' as
	 * `top' does it, because it is a lot of work and only used when
	 * debugging. -octo */

	return (0);
}
#endif /* HAVE_THREAD_INFO */
/* ------- end of additional functions for KERNEL_LINUX/HAVE_THREAD_INFO ------- */

/* do actual readings from kernel */
static int ps_read (void)
{
#if HAVE_THREAD_INFO
	kern_return_t            status;

	int                      pset;
	processor_set_t          port_pset_priv;

	int                      task;
	task_array_t             task_list;
	mach_msg_type_number_t   task_list_len;

	int                      task_pid;
	char                     task_name[MAXCOMLEN + 1];

	int                      thread;
	thread_act_array_t       thread_list;
	mach_msg_type_number_t   thread_list_len;
	thread_basic_info_data_t thread_data;
	mach_msg_type_number_t   thread_data_len;

	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int blocked  = 0;

	procstat_t *ps;
	procstat_entry_t pse;

	ps_list_reset ();

	/*
	 * The Mach-concept is a little different from the traditional UNIX
	 * concept: All the work is done in threads. Threads are contained in
	 * `tasks'. Therefore, `task status' doesn't make much sense, since
	 * it's actually a `thread status'.
	 * Tasks are assigned to sets of processors, so that's where you go to
	 * get a list.
	 */
	for (pset = 0; pset < pset_list_len; pset++)
	{
		if ((status = host_processor_set_priv (port_host_self,
						pset_list[pset],
						&port_pset_priv)) != KERN_SUCCESS)
		{
			ERROR ("host_processor_set_priv failed: %s\n",
					mach_error_string (status));
			continue;
		}

		if ((status = processor_set_tasks (port_pset_priv,
						&task_list,
						&task_list_len)) != KERN_SUCCESS)
		{
			ERROR ("processor_set_tasks failed: %s\n",
					mach_error_string (status));
			mach_port_deallocate (port_task_self, port_pset_priv);
			continue;
		}

		for (task = 0; task < task_list_len; task++)
		{
			ps = NULL;
			if (mach_get_task_name (task_list[task],
						&task_pid,
						task_name, PROCSTAT_NAME_LEN) == 0)
			{
				/* search for at least one match */
				for (ps = list_head_g; ps != NULL; ps = ps->next)
					/* FIXME: cmdline should be here instead of NULL */
					if (ps_list_match (task_name, NULL, ps) == 1)
						break;
			}

			/* Collect more detailed statistics for this process */
			if (ps != NULL)
			{
				task_basic_info_data_t        task_basic_info;
				mach_msg_type_number_t        task_basic_info_len;
				task_events_info_data_t       task_events_info;
				mach_msg_type_number_t        task_events_info_len;
				task_absolutetime_info_data_t task_absolutetime_info;
				mach_msg_type_number_t        task_absolutetime_info_len;

				memset (&pse, '\0', sizeof (pse));
				pse.id = task_pid;

				task_basic_info_len = TASK_BASIC_INFO_COUNT;
				status = task_info (task_list[task],
						TASK_BASIC_INFO,
						(task_info_t) &task_basic_info,
						&task_basic_info_len);
				if (status != KERN_SUCCESS)
				{
					ERROR ("task_info failed: %s",
							mach_error_string (status));
					continue; /* with next thread_list */
				}

				task_events_info_len = TASK_EVENTS_INFO_COUNT;
				status = task_info (task_list[task],
						TASK_EVENTS_INFO,
						(task_info_t) &task_events_info,
						&task_events_info_len);
				if (status != KERN_SUCCESS)
				{
					ERROR ("task_info failed: %s",
							mach_error_string (status));
					continue; /* with next thread_list */
				}

				task_absolutetime_info_len = TASK_ABSOLUTETIME_INFO_COUNT;
				status = task_info (task_list[task],
						TASK_ABSOLUTETIME_INFO,
						(task_info_t) &task_absolutetime_info,
						&task_absolutetime_info_len);
				if (status != KERN_SUCCESS)
				{
					ERROR ("task_info failed: %s",
							mach_error_string (status));
					continue; /* with next thread_list */
				}

				pse.num_proc++;
				pse.vmem_size = task_basic_info.virtual_size;
				pse.vmem_rss = task_basic_info.resident_size;
				/* Does not seem to be easily exposed */
				pse.vmem_data = 0;
				pse.vmem_code = 0;

				pse.vmem_minflt_counter = task_events_info.cow_faults;
				pse.vmem_majflt_counter = task_events_info.faults;

				pse.cpu_user_counter = task_absolutetime_info.total_user;
				pse.cpu_system_counter = task_absolutetime_info.total_system;
			}

			status = task_threads (task_list[task], &thread_list,
					&thread_list_len);
			if (status != KERN_SUCCESS)
			{
				/* Apple's `top' treats this case a zombie. It
				 * makes sense to some extend: A `zombie'
				 * thread is nonsense, since the task/process
				 * is dead. */
				zombies++;
				DEBUG ("task_threads failed: %s",
						mach_error_string (status));
				if (task_list[task] != port_task_self)
					mach_port_deallocate (port_task_self,
							task_list[task]);
				continue; /* with next task_list */
			}

			for (thread = 0; thread < thread_list_len; thread++)
			{
				thread_data_len = THREAD_BASIC_INFO_COUNT;
				status = thread_info (thread_list[thread],
						THREAD_BASIC_INFO,
						(thread_info_t) &thread_data,
						&thread_data_len);
				if (status != KERN_SUCCESS)
				{
					ERROR ("thread_info failed: %s",
							mach_error_string (status));
					if (task_list[task] != port_task_self)
						mach_port_deallocate (port_task_self,
								thread_list[thread]);
					continue; /* with next thread_list */
				}

				if (ps != NULL)
					pse.num_lwp++;

				switch (thread_data.run_state)
				{
					case TH_STATE_RUNNING:
						running++;
						break;
					case TH_STATE_STOPPED:
					/* What exactly is `halted'? */
					case TH_STATE_HALTED:
						stopped++;
						break;
					case TH_STATE_WAITING:
						sleeping++;
						break;
					case TH_STATE_UNINTERRUPTIBLE:
						blocked++;
						break;
					/* There is no `zombie' case here,
					 * since there are no zombie-threads.
					 * There's only zombie tasks, which are
					 * handled above. */
					default:
						WARNING ("Unknown thread status: %i",
								thread_data.run_state);
						break;
				} /* switch (thread_data.run_state) */

				if (task_list[task] != port_task_self)
				{
					status = mach_port_deallocate (port_task_self,
							thread_list[thread]);
					if (status != KERN_SUCCESS)
						ERROR ("mach_port_deallocate failed: %s",
								mach_error_string (status));
				}
			} /* for (thread_list) */

			if ((status = vm_deallocate (port_task_self,
							(vm_address_t) thread_list,
							thread_list_len * sizeof (thread_act_t)))
					!= KERN_SUCCESS)
			{
				ERROR ("vm_deallocate failed: %s",
						mach_error_string (status));
			}
			thread_list = NULL;
			thread_list_len = 0;

			/* Only deallocate the task port, if it isn't our own.
			 * Don't know what would happen in that case, but this
			 * is what Apple's top does.. ;) */
			if (task_list[task] != port_task_self)
			{
				status = mach_port_deallocate (port_task_self,
						task_list[task]);
				if (status != KERN_SUCCESS)
					ERROR ("mach_port_deallocate failed: %s",
							mach_error_string (status));
			}

			if (ps != NULL)
				/* FIXME: cmdline should be here instead of NULL */
				ps_list_add (task_name, NULL, &pse);
		} /* for (task_list) */

		if ((status = vm_deallocate (port_task_self,
				(vm_address_t) task_list,
				task_list_len * sizeof (task_t))) != KERN_SUCCESS)
		{
			ERROR ("vm_deallocate failed: %s",
					mach_error_string (status));
		}
		task_list = NULL;
		task_list_len = 0;

		if ((status = mach_port_deallocate (port_task_self, port_pset_priv))
				!= KERN_SUCCESS)
		{
			ERROR ("mach_port_deallocate failed: %s",
					mach_error_string (status));
		}
	} /* for (pset_list) */

	ps_submit_state ("running", running);
	ps_submit_state ("sleeping", sleeping);
	ps_submit_state ("zombies", zombies);
	ps_submit_state ("stopped", stopped);
	ps_submit_state ("blocked", blocked);

	for (ps = list_head_g; ps != NULL; ps = ps->next)
		ps_submit_proc_list (ps);
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int paging   = 0;
	int blocked  = 0;

	struct dirent *ent;
	DIR           *proc;
	int            pid;

	char cmdline[ARG_MAX];

	int        status;
	procstat_t ps;
	procstat_entry_t pse;
	char       state;

	unsigned long fork_rate;

	procstat_t *ps_ptr;

	running = sleeping = zombies = stopped = paging = blocked = 0;
	ps_list_reset ();

	if ((proc = opendir ("/proc")) == NULL)
	{
		char errbuf[1024];
		ERROR ("Cannot open `/proc': %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while ((ent = readdir (proc)) != NULL)
	{
		if (!isdigit (ent->d_name[0]))
			continue;

		if ((pid = atoi (ent->d_name)) < 1)
			continue;

		status = ps_read_process (pid, &ps, &state);
		if (status != 0)
		{
			DEBUG ("ps_read_process failed: %i", status);
			continue;
		}

		pse.id       = pid;
		pse.age      = 0;

		pse.num_proc   = ps.num_proc;
		pse.num_lwp    = ps.num_lwp;
		pse.vmem_size  = ps.vmem_size;
		pse.vmem_rss   = ps.vmem_rss;
		pse.vmem_data  = ps.vmem_data;
		pse.vmem_code  = ps.vmem_code;
		pse.stack_size = ps.stack_size;

		pse.vmem_minflt = 0;
		pse.vmem_minflt_counter = ps.vmem_minflt_counter;
		pse.vmem_majflt = 0;
		pse.vmem_majflt_counter = ps.vmem_majflt_counter;

		pse.cpu_user = 0;
		pse.cpu_user_counter = ps.cpu_user_counter;
		pse.cpu_system = 0;
		pse.cpu_system_counter = ps.cpu_system_counter;

		pse.io_rchar = ps.io_rchar;
		pse.io_wchar = ps.io_wchar;
		pse.io_syscr = ps.io_syscr;
		pse.io_syscw = ps.io_syscw;

		switch (state)
		{
			case 'R': running++;  break;
			case 'S': sleeping++; break;
			case 'D': blocked++;  break;
			case 'Z': zombies++;  break;
			case 'T': stopped++;  break;
			case 'W': paging++;   break;
		}

		ps_list_add (ps.name,
				ps_get_cmdline (pid, ps.name, cmdline, sizeof (cmdline)),
				&pse);
	}

	closedir (proc);

	ps_submit_state ("running",  running);
	ps_submit_state ("sleeping", sleeping);
	ps_submit_state ("zombies",  zombies);
	ps_submit_state ("stopped",  stopped);
	ps_submit_state ("paging",   paging);
	ps_submit_state ("blocked",  blocked);

	for (ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
		ps_submit_proc_list (ps_ptr);

	fork_rate = read_fork_rate();
	if (fork_rate != ULONG_MAX)
		ps_submit_fork_rate(fork_rate);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int blocked  = 0;
	int idle     = 0;
	int wait     = 0;

	kvm_t *kd;
	char errbuf[1024];
  	struct kinfo_proc *procs;          /* array of processes */
	struct kinfo_proc *proc_ptr = NULL;
  	int count;                         /* returns number of processes */
	int i;

	procstat_t *ps_ptr;
	procstat_entry_t pse;

	ps_list_reset ();

	/* Open the kvm interface, get a descriptor */
	kd = kvm_open (NULL, NULL, NULL, 0, errbuf);
	if (kd == NULL)
	{
		ERROR ("processes plugin: Cannot open kvm interface: %s",
				errbuf);
		return (0);
	}

	/* Get the list of processes. */
	procs = kvm_getprocs(kd, KERN_PROC_ALL, 0, &count);
	if (procs == NULL)
	{
		ERROR ("processes plugin: Cannot get kvm processes list: %s",
				kvm_geterr(kd));
		kvm_close (kd);
		return (0);
	}

	/* Iterate through the processes in kinfo_proc */
	for (i = 0; i < count; i++)
	{
		/* Create only one process list entry per _process_, i.e.
		 * filter out threads (duplicate PID entries). */
		if ((proc_ptr == NULL) || (proc_ptr->ki_pid != procs[i].ki_pid))
		{
			char cmdline[ARG_MAX] = "";
			_Bool have_cmdline = 0;

			proc_ptr = &(procs[i]);
			/* Don't probe system processes and processes without arguments */
			if (((procs[i].ki_flag & P_SYSTEM) == 0)
					&& (procs[i].ki_args != NULL))
			{
				char **argv;
				int argc;
				int status;

				/* retrieve the arguments */
				argv = kvm_getargv (kd, proc_ptr, /* nchr = */ 0);
				argc = 0;
				if ((argv != NULL) && (argv[0] != NULL))
				{
					while (argv[argc] != NULL)
						argc++;

					status = strjoin (cmdline, sizeof (cmdline), argv, argc, " ");
					if (status < 0)
						WARNING ("processes plugin: Command line did not fit into buffer.");
					else
						have_cmdline = 1;
				}
			} /* if (process has argument list) */

			pse.id       = procs[i].ki_pid;
			pse.age      = 0;

			pse.num_proc = 1;
			pse.num_lwp  = procs[i].ki_numthreads;

			pse.vmem_size = procs[i].ki_size;
			pse.vmem_rss = procs[i].ki_rssize * pagesize;
			pse.vmem_data = procs[i].ki_dsize * pagesize;
			pse.vmem_code = procs[i].ki_tsize * pagesize;
			pse.stack_size = procs[i].ki_ssize * pagesize;
			pse.vmem_minflt = 0;
			pse.vmem_minflt_counter = procs[i].ki_rusage.ru_minflt;
			pse.vmem_majflt = 0;
			pse.vmem_majflt_counter = procs[i].ki_rusage.ru_majflt;

			pse.cpu_user = 0;
			pse.cpu_system = 0;
			pse.cpu_user_counter = 0;
			pse.cpu_system_counter = 0;
			/*
			 * The u-area might be swapped out, and we can't get
			 * at it because we have a crashdump and no swap.
			 * If it's here fill in these fields, otherwise, just
			 * leave them 0.
			 */
			if (procs[i].ki_flag & P_INMEM)
			{
				pse.cpu_user_counter = procs[i].ki_rusage.ru_utime.tv_usec
				       	+ (1000000lu * procs[i].ki_rusage.ru_utime.tv_sec);
				pse.cpu_system_counter = procs[i].ki_rusage.ru_stime.tv_usec
					+ (1000000lu * procs[i].ki_rusage.ru_stime.tv_sec);
			}

			/* no I/O data */
			pse.io_rchar = -1;
			pse.io_wchar = -1;
			pse.io_syscr = -1;
			pse.io_syscw = -1;

			ps_list_add (procs[i].ki_comm, have_cmdline ? cmdline : NULL, &pse);
		} /* if ((proc_ptr == NULL) || (proc_ptr->ki_pid != procs[i].ki_pid)) */

		switch (procs[i].ki_stat)
		{
			case SSTOP: 	stopped++;	break;
			case SSLEEP:	sleeping++;	break;
			case SRUN:	running++;	break;
			case SIDL:	idle++;		break;
			case SWAIT:	wait++;		break;
			case SLOCK:	blocked++;	break;
			case SZOMB:	zombies++;	break;
		}
	}

	kvm_close(kd);

	ps_submit_state ("running",  running);
	ps_submit_state ("sleeping", sleeping);
	ps_submit_state ("zombies",  zombies);
	ps_submit_state ("stopped",  stopped);
	ps_submit_state ("blocked",  blocked);
	ps_submit_state ("idle",     idle);
	ps_submit_state ("wait",     wait);

	for (ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
		ps_submit_proc_list (ps_ptr);
/* #endif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

#elif HAVE_PROCINFO_H
	/* AIX */
	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int paging   = 0;
	int blocked  = 0;

	pid_t pindex = 0;
	int nprocs;

	procstat_t *ps;
	procstat_entry_t pse;

	ps_list_reset ();
	while ((nprocs = getprocs64 (procentry, sizeof(struct procentry64),
					/* fdsinfo = */ NULL, sizeof(struct fdsinfo64),
					&pindex, MAXPROCENTRY)) > 0)
	{
		int i;

		for (i = 0; i < nprocs; i++)
		{
			tid64_t thindex;
			int nthreads;
			char arglist[MAXARGLN+1];
			char *cargs;
			char *cmdline;

			if (procentry[i].pi_state == SNONE) continue;
			/* if (procentry[i].pi_state == SZOMB)  FIXME */

			cmdline = procentry[i].pi_comm;
			cargs = procentry[i].pi_comm;
			if ( procentry[i].pi_flags & SKPROC )
			{
				if (procentry[i].pi_pid == 0)
					cmdline = "swapper";
				cargs = cmdline;
 			}
			else
			{
				if (getargs(&procentry[i], sizeof(struct procentry64), arglist, MAXARGLN) >= 0)
				{
					int n;

					n = -1;
					while (++n < MAXARGLN)
					{
						if (arglist[n] == '\0')
						{
							if (arglist[n+1] == '\0')
								break;
							arglist[n] = ' ';
						}
					}
					cargs = arglist;
				}
			}

			pse.id       = procentry[i].pi_pid;
			pse.age      = 0;
			pse.num_lwp  = procentry[i].pi_thcount;
			pse.num_proc = 1;

			thindex=0;
			while ((nthreads = getthrds64(procentry[i].pi_pid,
							thrdentry, sizeof(struct thrdentry64),
							&thindex, MAXTHRDENTRY)) > 0)
			{
				int j;

				for (j=0; j< nthreads; j++)
				{
					switch (thrdentry[j].ti_state)
					{
						/* case TSNONE: break; */
						case TSIDL:	blocked++;	break; /* FIXME is really blocked */
						case TSRUN:	running++;	break;
						case TSSLEEP:	sleeping++;	break;
						case TSSWAP:	paging++;	break;
						case TSSTOP:	stopped++;	break;
						case TSZOMB:	zombies++;	break;
					}
				}
				if (nthreads < MAXTHRDENTRY)
					break;
			}

			pse.cpu_user = 0;
			/* tv_usec is nanosec ??? */
			pse.cpu_user_counter = procentry[i].pi_ru.ru_utime.tv_sec * 1000000 +
				procentry[i].pi_ru.ru_utime.tv_usec / 1000;

			pse.cpu_system = 0;
			/* tv_usec is nanosec ??? */
			pse.cpu_system_counter = procentry[i].pi_ru.ru_stime.tv_sec * 1000000 +
				procentry[i].pi_ru.ru_stime.tv_usec / 1000;

			pse.vmem_minflt = 0;
			pse.vmem_minflt_counter = procentry[i].pi_minflt;
			pse.vmem_majflt = 0;
			pse.vmem_majflt_counter = procentry[i].pi_majflt;

			pse.vmem_size = procentry[i].pi_tsize + procentry[i].pi_dvm * pagesize;
			pse.vmem_rss = (procentry[i].pi_drss + procentry[i].pi_trss) * pagesize;
			/* Not supported */
			pse.vmem_data = 0;
			pse.vmem_code = 0;
			pse.stack_size =  0;

			pse.io_rchar = -1;
			pse.io_wchar = -1;
			pse.io_syscr = -1;
			pse.io_syscw = -1;

			ps_list_add (cmdline, cargs, &pse);
		} /* for (i = 0 .. nprocs) */

		if (nprocs < MAXPROCENTRY)
			break;
	} /* while (getprocs64() > 0) */
	ps_submit_state ("running",  running);
	ps_submit_state ("sleeping", sleeping);
	ps_submit_state ("zombies",  zombies);
	ps_submit_state ("stopped",  stopped);
	ps_submit_state ("paging",   paging);
	ps_submit_state ("blocked",  blocked);

	for (ps = list_head_g; ps != NULL; ps = ps->next)
		ps_submit_proc_list (ps);
#endif /* HAVE_PROCINFO_H */

	return (0);
} /* int ps_read */

void module_register (void)
{
	plugin_register_complex_config ("processes", ps_config);
	plugin_register_init ("processes", ps_init);
	plugin_register_read ("processes", ps_read);
} /* void module_register */
