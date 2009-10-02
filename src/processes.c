/**
 * collectd - src/processes.c
 * Copyright (C) 2005       Lyonel Vincent
 * Copyright (C) 2006-2008  Florian octo Forster
 * Copyright (C) 2008       Oleg King
 * Copyright (C) 2009       Sebastian Harl
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

#else
# error "No applicable input method."
#endif

#if HAVE_REGEX_H
# include <regex.h>
#endif

#ifndef ARG_MAX
#  define ARG_MAX 4096
#endif

#define BUFSIZE 256

static const char *config_keys[] =
{
	"Process",
	"ProcessMatch"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

typedef struct procstat_entry_s
{
	unsigned long id;
	unsigned long age;

	unsigned long num_proc;
	unsigned long num_lwp;
	unsigned long vmem_size;
	unsigned long vmem_rss;
	unsigned long stack_size;

	unsigned long vmem_minflt;
	unsigned long vmem_majflt;
	unsigned long vmem_minflt_counter;
	unsigned long vmem_majflt_counter;

	unsigned long cpu_user;
	unsigned long cpu_system;
	unsigned long cpu_user_counter;
	unsigned long cpu_system_counter;

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
	unsigned long stack_size;

	unsigned long vmem_minflt_counter;
	unsigned long vmem_majflt_counter;

	unsigned long cpu_user_counter;
	unsigned long cpu_system_counter;

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
/* no global variables */
#endif /* HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

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
				"has been dispabled at compile time.",
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
		pse->stack_size = entry->stack_size;

		ps->num_proc   += pse->num_proc;
		ps->num_lwp    += pse->num_lwp;
		ps->vmem_size  += pse->vmem_size;
		ps->vmem_rss   += pse->vmem_rss;
		ps->stack_size += pse->stack_size;

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
		ps->stack_size  = 0;

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
static int ps_config (const char *key, const char *value)
{
	if (strcasecmp (key, "Process") == 0)
	{
		ps_list_register (value, NULL);
	}
	else if (strcasecmp (key, "ProcessMatch") == 0)
	{
		char *new_val;
		char *fields[3];
		int fields_num;

		new_val = strdup (value);
		if (new_val == NULL) {
			ERROR ("processes plugin: strdup failed when processing "
					"`ProcessMatch %s'.", value);
			return (1);
		}

		fields_num = strsplit (new_val, fields,
				STATIC_ARRAY_SIZE (fields));
		if (fields_num != 2)
		{
			ERROR ("processes plugin: `ProcessMatch' needs exactly "
					"two string arguments.");
			sfree (new_val);
			return (1);
		}
		ps_list_register (fields[0], fields[1]);
		sfree (new_val);
	}
	else
	{
		ERROR ("processes plugin: The `%s' configuration option is not "
				"understood and will be ignored.", key);
		return (-1);
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
/* no initialization */
#endif /* HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

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

	sstrncpy (vl.type, "ps_stacksize", sizeof (vl.type));
	vl.values[0].gauge = ps->stack_size;
	vl.values_len = 1;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_cputime", sizeof (vl.type));
	vl.values[0].counter = ps->cpu_user_counter;
	vl.values[1].counter = ps->cpu_system_counter;
	vl.values_len = 2;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_count", sizeof (vl.type));
	vl.values[0].gauge = ps->num_proc;
	vl.values[1].gauge = ps->num_lwp;
	vl.values_len = 2;
	plugin_dispatch_values (&vl);

	sstrncpy (vl.type, "ps_pagefaults", sizeof (vl.type));
	vl.values[0].counter = ps->vmem_minflt_counter;
	vl.values[1].counter = ps->vmem_majflt_counter;
	vl.values_len = 2;
	plugin_dispatch_values (&vl);

	DEBUG ("name = %s; num_proc = %lu; num_lwp = %lu; vmem_rss = %lu; "
			"vmem_minflt_counter = %lu; vmem_majflt_counter = %lu; "
			"cpu_user_counter = %lu; cpu_system_counter = %lu;",
			ps->name, ps->num_proc, ps->num_lwp, ps->vmem_rss,
			ps->vmem_minflt_counter, ps->vmem_majflt_counter,
		       	ps->cpu_user_counter, ps->cpu_system_counter);
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

int ps_read_process (int pid, procstat_t *ps, char *state)
{
	char  filename[64];
	char  buffer[1024];

	char *fields[64];
	char  fields_len;

	int   i;

	int   ppid;
	int   name_len;

	long long unsigned cpu_user_counter;
	long long unsigned cpu_system_counter;
	long long unsigned vmem_size;
	long long unsigned vmem_rss;
	long long unsigned stack_size;

	memset (ps, 0, sizeof (procstat_t));

	ssnprintf (filename, sizeof (filename), "/proc/%i/stat", pid);

	i = read_file_contents (filename, buffer, sizeof(buffer) - 1);
	if (i <= 0)
		return (-1);
	buffer[i] = 0;

	fields_len = strsplit (buffer, fields, 64);
	if (fields_len < 24)
	{
		DEBUG ("processes plugin: ps_read_process (pid = %i):"
				" `%s' has only %i fields..",
				(int) pid, filename, fields_len);
		return (-1);
	}

	/* copy the name, strip brackets in the process */
	name_len = strlen (fields[1]) - 2;
	if ((fields[1][0] != '(') || (fields[1][name_len + 1] != ')'))
	{
		DEBUG ("No brackets found in process name: `%s'", fields[1]);
		return (-1);
	}
	fields[1] = fields[1] + 1;
	fields[1][name_len] = '\0';
	strncpy (ps->name, fields[1], PROCSTAT_NAME_LEN);

	ppid = atoi (fields[3]);

	*state = fields[2][0];

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

	cpu_user_counter   = atoll (fields[13]);
	cpu_system_counter = atoll (fields[14]);
	vmem_size          = atoll (fields[22]);
	vmem_rss           = atoll (fields[23]);
	ps->vmem_minflt_counter = atol (fields[9]);
	ps->vmem_majflt_counter = atol (fields[11]);

	{
		unsigned long long stack_start = atoll (fields[27]);
		unsigned long long stack_ptr   = atoll (fields[28]);

		stack_size = (stack_start > stack_ptr)
			? stack_start - stack_ptr
			: stack_ptr - stack_start;
	}

	/* Convert jiffies to useconds */
	cpu_user_counter   = cpu_user_counter   * 1000000 / CONFIG_HZ;
	cpu_system_counter = cpu_system_counter * 1000000 / CONFIG_HZ;
	vmem_rss = vmem_rss * pagesize_g;

	ps->cpu_user_counter = (unsigned long) cpu_user_counter;
	ps->cpu_system_counter = (unsigned long) cpu_system_counter;
	ps->vmem_size = (unsigned long) vmem_size;
	ps->vmem_rss = (unsigned long) vmem_rss;
	ps->stack_size = (unsigned long) stack_size;

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

	ssnprintf (file, sizeof (file), "/proc/%u/cmdline", pid);

	fd = open (file, O_RDONLY);
	if (fd < 0) {
		char errbuf[4096];
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
			char errbuf[4096];

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
		if (numfields != 2) {
			ERROR ("processes plugin: processes in /proc/stat "
					"contains more than 2 fields.");
			continue;
		}
		if (strcmp ("processes", fields[0]) != 0)
			continue;

		errno = 0;
		endptr = NULL;
		result = strtoul(fields[1], &endptr, 10);
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
				pse.vmem_rss = task_basic_info.resident_size;

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
		pse.stack_size = ps.stack_size;

		pse.vmem_minflt = 0;
		pse.vmem_minflt_counter = ps.vmem_minflt_counter;
		pse.vmem_majflt = 0;
		pse.vmem_majflt_counter = ps.vmem_majflt_counter;

		pse.cpu_user = 0;
		pse.cpu_user_counter = ps.cpu_user_counter;
		pse.cpu_system = 0;
		pse.cpu_system_counter = ps.cpu_system_counter;

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
	char cmdline[ARG_MAX];
	char *cmdline_ptr;
  	struct kinfo_proc *procs;          /* array of processes */
  	char **argv;
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
		kvm_close (kd);
		ERROR ("processes plugin: Cannot get kvm processes list: %s",
				kvm_geterr(kd));
		return (0);
	}

	/* Iterate through the processes in kinfo_proc */
	for (i = 0; i < count; i++)
	{
		/* retrieve the arguments */
		cmdline[0] = 0;
		cmdline_ptr = NULL;

		argv = kvm_getargv (kd, (const struct kinfo_proc *) &(procs[i]), 0);
		if (argv != NULL)
		{
			int status;
			int argc;

			argc = 0;
			while (argv[argc] != NULL)
				argc++;

			status = strjoin (cmdline, sizeof (cmdline),
					argv, argc, " ");

			if (status < 0)
			{
				WARNING ("processes plugin: Command line did "
						"not fit into buffer.");
			}
			else
			{
				cmdline_ptr = &cmdline[0];
			}
		}

		pse.id       = procs[i].ki_pid;
		pse.age      = 0;

		pse.num_proc = 1;
		pse.num_lwp  = procs[i].ki_numthreads;

		pse.vmem_size = procs[i].ki_size;
		pse.vmem_rss = procs[i].ki_rssize * getpagesize();
		pse.stack_size = procs[i].ki_ssize * getpagesize();
		pse.vmem_minflt = 0;
		pse.vmem_minflt_counter = procs[i].ki_rusage.ru_minflt;
		pse.vmem_majflt = 0;
		pse.vmem_majflt_counter = procs[i].ki_rusage.ru_majflt;

		pse.cpu_user = 0;
		pse.cpu_user_counter = procs[i].ki_rusage.ru_utime.tv_sec
			* 1000
			+ procs[i].ki_rusage.ru_utime.tv_usec;
		pse.cpu_system = 0;
		pse.cpu_system_counter = procs[i].ki_rusage.ru_stime.tv_sec
			* 1000
			+ procs[i].ki_rusage.ru_stime.tv_usec;

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

		ps_list_add (procs[i].ki_comm, cmdline_ptr, &pse);
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
#endif /* HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

	return (0);
} /* int ps_read */

void module_register (void)
{
	plugin_register_config ("processes", ps_config,
			config_keys, config_keys_num);
	plugin_register_init ("processes", ps_init);
	plugin_register_read ("processes", ps_read);
} /* void module_register */
