/**
 * collectd - src/nfs.c
 * Copyright (C) 2005,2006  Jason Pepas
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
 *   Jason Pepas <cell at ices.utexas.edu>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

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

static const char *nfs2_procedures_names[] =
{
	"null",
	"getattr",
	"setattr",
	"root",
	"lookup",
	"readlink",
	"read",
	"wrcache",
	"write",
	"create",
	"remove",
	"rename",
	"link",
	"symlink",
	"mkdir",
	"rmdir",
	"readdir",
	"fsstat",
	NULL
};
static int nfs2_procedures_names_num = 18;

static const char *nfs3_procedures_names[] =
{
	"null",
	"getattr",
	"setattr",
	"lookup",
	"access",
	"readlink",
	"read",
	"write",
	"create",
	"mkdir",
	"symlink",
	"mknod",
	"remove",
	"rmdir",
	"rename",
	"link",
	"readdir",
	"readdirplus",
	"fsstat",
	"fsinfo",
	"pathconf",
	"commit",
	NULL
};
static int nfs3_procedures_names_num = 22;

#if HAVE_LIBKSTAT && 0
extern kstat_ctl_t *kc;
static kstat_t *nfs2_ksp_client;
static kstat_t *nfs2_ksp_server;
static kstat_t *nfs3_ksp_client;
static kstat_t *nfs3_ksp_server;
static kstat_t *nfs4_ksp_client;
static kstat_t *nfs4_ksp_server;
#endif

/* Possibly TODO: NFSv4 statistics */

#if 0
static int nfs_init (void)
{
#if HAVE_LIBKSTAT && 0
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

	return (0);
} /* int nfs_init */
#endif

#define BUFSIZE 1024
static void nfs_procedures_submit (const char *plugin_instance,
	       	unsigned long long *val, const char **names, int len)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;
	int i;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "nfs", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
		       	sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "nfs_procedure", sizeof (vl.type));

	for (i = 0; i < len; i++)
	{
		values[0].derive = val[i];
		sstrncpy (vl.type_instance, names[i],
				sizeof (vl.type_instance));
		DEBUG ("%s-%s/nfs_procedure-%s = %llu",
				vl.plugin, vl.plugin_instance,
				vl.type_instance, val[i]);
		plugin_dispatch_values (&vl);
	}
} /* void nfs_procedures_submit */

static void nfs_read_stats_file (FILE *fh, char *inst)
{
	char buffer[BUFSIZE];

	char plugin_instance[DATA_MAX_NAME_LEN];

	char *fields[48];
	int numfields = 0;

	if (fh == NULL)
		return;

	while (fgets (buffer, BUFSIZE, fh) != NULL)
	{
		numfields = strsplit (buffer, fields, 48);

		if (((numfields - 2) != nfs2_procedures_names_num)
				&& ((numfields - 2)
				       	!= nfs3_procedures_names_num))
			continue;

		if (strcmp (fields[0], "proc2") == 0)
		{
			int i;
			unsigned long long *values;

			if ((numfields - 2) != nfs2_procedures_names_num)
			{
				WARNING ("nfs plugin: Wrong "
						"number of fields (= %i) "
						"for NFSv2 statistics.",
					       	numfields - 2);
				continue;
			}

			ssnprintf (plugin_instance, sizeof (plugin_instance),
					"v2%s", inst);

			values = (unsigned long long *) malloc (nfs2_procedures_names_num * sizeof (unsigned long long));
			if (values == NULL)
			{
				char errbuf[1024];
				ERROR ("nfs plugin: malloc "
						"failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				continue;
			}

			for (i = 0; i < nfs2_procedures_names_num; i++)
				values[i] = atoll (fields[i + 2]);

			nfs_procedures_submit (plugin_instance, values,
					nfs2_procedures_names,
					nfs2_procedures_names_num);

			free (values);
		}
		else if (strncmp (fields[0], "proc3", 5) == 0)
		{
			int i;
			unsigned long long *values;

			if ((numfields - 2) != nfs3_procedures_names_num)
			{
				WARNING ("nfs plugin: Wrong "
						"number of fields (= %i) "
						"for NFSv3 statistics.",
					       	numfields - 2);
				continue;
			}

			ssnprintf (plugin_instance, sizeof (plugin_instance),
					"v3%s", inst);

			values = (unsigned long long *) malloc (nfs3_procedures_names_num * sizeof (unsigned long long));
			if (values == NULL)
			{
				char errbuf[1024];
				ERROR ("nfs plugin: malloc "
						"failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				continue;
			}

			for (i = 0; i < nfs3_procedures_names_num; i++)
				values[i] = atoll (fields[i + 2]);

			nfs_procedures_submit (plugin_instance, values,
					nfs3_procedures_names,
					nfs3_procedures_names_num);

			free (values);
		}
	} /* while (fgets (buffer, BUFSIZE, fh) != NULL) */
} /* void nfs_read_stats_file */
#undef BUFSIZE

#if HAVE_LIBKSTAT && 0
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

static int nfs_read (void)
{
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

#if HAVE_LIBKSTAT && 0
	if (nfs2_ksp_client != NULL)
		nfs2_read_kstat (nfs2_ksp_client, "client");
	if (nfs2_ksp_server != NULL)
		nfs2_read_kstat (nfs2_ksp_server, "server");
#endif /* defined(HAVE_LIBKSTAT) */

	return (0);
}

void module_register (void)
{
	plugin_register_read ("nfs", nfs_read);
} /* void module_register */
