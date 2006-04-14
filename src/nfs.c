/**
 * collectd - src/nfs.c
 * Copyright (C) 2005,2006  Jason Pepas
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
 *   Jason Pepas <cell at ices.utexas.edu>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "nfs"

#if defined(KERNEL_LINUX) || defined(HAVE_LIBKSTAT)
# define NFS_HAVE_READ 1
#else
# define NFS_HAVE_READ 0
#endif

static char *nfs2_procedures_file  = "nfs2_procedures-%s.rrd";
static char *nfs3_procedures_file  = "nfs3_procedures-%s.rrd";

/*
see /proc/net/rpc/nfs
see http://www.missioncriticallinux.com/orph/NFS-Statistics

net x x x x
rpc_stat.netcnt         Not used; always zero.
rpc_stat.netudpcnt      Not used; always zero.
rpc_stat.nettcpcnt      Not used; always zero.
rpc_stat.nettcpconn     Not used; always zero.

rpc x x x
rpc_stat.rpccnt             The number of RPC calls.
rpc_stat.rpcretrans         The number of retransmitted RPC calls.
rpc_stat.rpcauthrefresh     The number of credential refreshes.

proc2 x x x...
proc3 x x x...

Procedure   NFS Version NFS Version 3
Number      Procedures  Procedures

0           null        null
1           getattr     getattr
2           setattr     setattr
3           root        lookup
4           lookup      access
5           readlink    readlink
6           read        read
7           wrcache     write
8           write       create
9           create      mkdir
10          remove      symlink
11          rename      mknod
12          link        remove
13          symlink     rmdir
14          mkdir       rename
15          rmdir       link
16          readdir     readdir
17          fsstat      readdirplus
18                      fsstat
19                      fsinfo
20                      pathconf
21                      commit
*/

static char *nfs2_procedures_ds_def[] =
{
	"DS:null:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:getattr:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:setattr:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:root:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:lookup:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:readlink:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:read:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:wrcache:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:write:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:create:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:remove:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rename:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:link:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:symlink:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:mkdir:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rmdir:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:readdir:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:fsstat:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int nfs2_procedures_ds_num = 18;

static char *nfs3_procedures_ds_def[] =
{
	"DS:null:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:getattr:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:setattr:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:lookup:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:access:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:readlink:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:read:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:write:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:create:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:mkdir:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:symlink:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:mknod:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:remove:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rmdir:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:rename:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:link:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:readdir:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:readdirplus:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:fsstat:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:fsinfo:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:pathconf:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:commit:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int nfs3_procedures_ds_num = 22;

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
static kstat_t *nfs2_ksp_client;
static kstat_t *nfs2_ksp_server;
static kstat_t *nfs3_ksp_client;
static kstat_t *nfs3_ksp_server;
static kstat_t *nfs4_ksp_client;
static kstat_t *nfs4_ksp_server;
#endif

/* Possibly TODO: NFSv4 statistics */

static void nfs_init (void)
{
#ifdef HAVE_LIBKSTAT
	kstat_t *ksp_chain;

	nfs2_ksp_client = NULL;
	nfs2_ksp_server = NULL;
	nfs3_ksp_client = NULL;
	nfs3_ksp_server = NULL;
	nfs4_ksp_client = NULL;
	nfs4_ksp_server = NULL;
	
	if (kc == NULL)
		return;

	for (ksp_chain = kc->kc_chain; ksp_chain != NULL;
			ksp_chain = ksp_chain->ks_next)
	{
		if (strncmp (ksp_chain->ks_module, "nfs", 3) != 0)
			continue;
		else if (strncmp (ksp_chain->ks_name, "rfsproccnt_v2", 13) == 0)
			nfs2_ksp_server = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsproccnt_v3", 13) == 0)
			nfs3_ksp_server = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsproccnt_v4", 13) == 0)
			nfs4_ksp_server = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsreqcnt_v2", 12) == 0)
			nfs2_ksp_client = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsreqcnt_v3", 12) == 0)
			nfs3_ksp_client = ksp_chain;
		else if (strncmp (ksp_chain->ks_name, "rfsreqcnt_v4", 12) == 0)
			nfs4_ksp_client = ksp_chain;
	}
#endif

	return;
}

#define BUFSIZE 1024
static void nfs2_procedures_write (char *host, char *inst, char *val)
{
	char filename[BUFSIZE];

	if (snprintf (filename, BUFSIZE, nfs2_procedures_file, inst) > BUFSIZE)
		return;

	rrd_update_file (host, filename, val, nfs2_procedures_ds_def,
			nfs2_procedures_ds_num);
}

static void nfs3_procedures_write (char *host, char *inst, char *val)
{
	char filename[BUFSIZE];

	if (snprintf (filename, BUFSIZE, nfs3_procedures_file, inst) > BUFSIZE)
		return;

	rrd_update_file (host, filename, val, nfs3_procedures_ds_def,
			nfs3_procedures_ds_num);
}

#if NFS_HAVE_READ
static void nfs2_procedures_submit (unsigned long long *val, char *inst)
{
	char buf[BUFSIZE];
	int retval = 0;

	retval = snprintf (buf, BUFSIZE, "%u:%llu:%llu:%llu:%llu:%llu:%llu:"
			"%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:"
			"%llu:%llu:%llu", /* 18x %llu */
			(unsigned int) curtime,
			val[0], val[1], val[2], val[3], val[4], val[5], val[6],
			val[7], val[8], val[9], val[10], val[11], val[12],
			val[13], val[14], val[15], val[16], val[17]);


	if (retval >= BUFSIZE)
		return;
	else if (retval < 0)
	{
		syslog (LOG_ERR, "nfs: snprintf's format failed: %s", strerror (errno));
		return;
	}

	plugin_submit ("nfs2_procedures", inst, buf);
}

static void nfs3_procedures_submit (unsigned long long *val, char *inst)
{
	char buf[BUFSIZE];
	int retval = 0;

	retval = snprintf(buf, BUFSIZE, "%u:%llu:%llu:%llu:%llu:%llu:%llu:"
			"%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:"
			"%llu:%llu:%llu:%llu:%llu:%llu:%llu", /* 22x %llu */
			(unsigned int) curtime,
			val[0], val[1], val[2], val[3], val[4], val[5], val[6],
			val[7], val[8], val[9], val[10], val[11], val[12],
			val[13], val[14], val[15], val[16], val[17], val[18],
			val[19], val[20], val[21]);

	if (retval >= BUFSIZE)
		return;
	else if (retval < 0)
	{
		syslog (LOG_ERR, "nfs: snprintf's format failed: %s", strerror (errno));
		return;
	}

	plugin_submit("nfs3_procedures", inst, buf);
}
#endif /* NFS_HAVE_READ */

#if defined(KERNEL_LINUX)
static void nfs_read_stats_file (FILE *fh, char *inst)
{
	char buffer[BUFSIZE];

	char *fields[48];
	int numfields = 0;

	if (fh == NULL)
		return;

	while (fgets (buffer, BUFSIZE, fh) != NULL)
	{
		numfields = strsplit (buffer, fields, 48);

		if (numfields < 2)
			continue;

		if (strncmp (fields[0], "proc2", 5) == 0)
		{
			int i;
			unsigned long long *values;

			if (numfields - 2 != nfs2_procedures_ds_num)
			{
				syslog (LOG_WARNING, "nfs: Wrong number of fields (= %i) for NFS2 statistics.", numfields - 2);
				continue;
			}

			if ((values = (unsigned long long *) malloc (nfs2_procedures_ds_num * sizeof (unsigned long long))) == NULL)
			{
				syslog (LOG_ERR, "nfs: malloc: %s", strerror (errno));
				continue;
			}

			for (i = 0; i < nfs2_procedures_ds_num; i++)
				values[i] = atoll (fields[i + 2]);

			nfs2_procedures_submit (values, inst);

			free (values);
		}
		else if (strncmp (fields[0], "proc3", 5) == 0)
		{
			int i;
			unsigned long long *values;

			if (numfields - 2 != nfs3_procedures_ds_num)
			{
				syslog (LOG_WARNING, "nfs: Wrong number of fields (= %i) for NFS3 statistics.", numfields - 2);
				continue;
			}

			if ((values = (unsigned long long *) malloc (nfs3_procedures_ds_num * sizeof (unsigned long long))) == NULL)
			{
				syslog (LOG_ERR, "nfs: malloc: %s", strerror (errno));
				continue;
			}

			for (i = 0; i < nfs3_procedures_ds_num; i++)
				values[i] = atoll (fields[i + 2]);

			nfs3_procedures_submit (values, inst);

			free (values);
		}
	}
}
#endif /* defined(KERNEL_LINUX) */
#undef BUFSIZE

#ifdef HAVE_LIBKSTAT
static void nfs2_read_kstat (kstat_t *ksp, char *inst)
{
	unsigned long long values[18];

	values[0] = get_kstat_value (ksp, "null");
	values[1] = get_kstat_value (ksp, "getattr");
	values[2] = get_kstat_value (ksp, "setattr");
	values[3] = get_kstat_value (ksp, "root");
	values[4] = get_kstat_value (ksp, "lookup");
	values[5] = get_kstat_value (ksp, "readlink");
	values[6] = get_kstat_value (ksp, "read");
	values[7] = get_kstat_value (ksp, "wrcache");
	values[8] = get_kstat_value (ksp, "write");
	values[9] = get_kstat_value (ksp, "create");
	values[10] = get_kstat_value (ksp, "remove");
	values[11] = get_kstat_value (ksp, "rename");
	values[12] = get_kstat_value (ksp, "link");
	values[13] = get_kstat_value (ksp, "symlink");
	values[14] = get_kstat_value (ksp, "mkdir");
	values[15] = get_kstat_value (ksp, "rmdir");
	values[16] = get_kstat_value (ksp, "readdir");
	values[17] = get_kstat_value (ksp, "statfs");

	nfs2_procedures_submit (values, inst);
}
#endif

#if NFS_HAVE_READ
static void nfs_read (void)
{
#if defined(KERNEL_LINUX)
	FILE *fh;

	if ((fh = fopen ("/proc/net/rpc/nfs", "r")) != NULL)
	{
		nfs_read_stats_file (fh, "client");
		fclose (fh);
	}

	if ((fh = fopen ("/proc/net/rpc/nfsd", "r")) != NULL)
	{
		nfs_read_stats_file (fh, "server");
		fclose (fh);
	}

/* #endif defined(KERNEL_LINUX) */

#elif defined(HAVE_LIBKSTAT)
	if (nfs2_ksp_client != NULL)
		nfs2_read_kstat (nfs2_ksp_client, "client");
	if (nfs2_ksp_server != NULL)
		nfs2_read_kstat (nfs2_ksp_server, "server");
#endif /* defined(HAVE_LIBKSTAT) */
}
#else
# define nfs_read NULL
#endif /* NFS_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, nfs_init, nfs_read, NULL);
	plugin_register ("nfs2_procedures", NULL, NULL, nfs2_procedures_write);
	plugin_register ("nfs3_procedures", NULL, NULL, nfs3_procedures_write);
}

#undef MODULE_NAME
