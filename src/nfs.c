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

#if HAVE_KSTAT_H
#include <kstat.h>
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

static const char *nfs2_procedures_names[] ={
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

static const char *nfs3_procedures_names[] ={
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
#if HAVE_LIBKSTAT
static const char *nfs4_procedures_names[] = {
    "null",
    "compound",
    "reserved",
    "access",
    "close",
    "commit",
    "create",
    "delegpurge",
    "delegreturn",
    "getattr",
    "getfh",
    "link",
    "lock",
    "lockt",
    "locku",
    "lookup",
    "lookupp",
    "nverify",
    "open",
    "openattr",
    "open_confirm",
    "open_downgrade",
    "putfh",
    "putpubfh",
    "putrootfh",
    "read",
    "readdir",
    "readlink",
    "remove",
    "rename",
    "renew",
    "restorefh",
    "savefh",
    "secinfo",
    "setattr",
    "setclientid",
    "setclientid_confirm",
    "verify",
    "write",
    NULL
};
static int nfs4_procedures_names_num = 39;
#endif

#if HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
static kstat_t *nfs2_ksp_client;
static kstat_t *nfs2_ksp_server;
static kstat_t *nfs3_ksp_client;
static kstat_t *nfs3_ksp_server;
static kstat_t *nfs4_ksp_client;
static kstat_t *nfs4_ksp_server;
#endif

/* Possibly TODO: NFSv4 statistics */

#if HAVE_LIBKSTAT

static int nfs_init(void) {


    kstat_t *ksp_chain = NULL;

    nfs2_ksp_client = NULL;
    nfs2_ksp_server = NULL;
    nfs3_ksp_client = NULL;
    nfs3_ksp_server = NULL;
    nfs4_ksp_client = NULL;
    nfs4_ksp_server = NULL;

    if (kc == NULL)
        return (-1);

    for (ksp_chain = kc->kc_chain; ksp_chain != NULL;
            ksp_chain = ksp_chain->ks_next) {
        if (strncmp(ksp_chain->ks_module, "nfs", 3) != 0) 
            continue;
        else if (strncmp(ksp_chain->ks_name, "rfsproccnt_v2", 13) == 0)
            nfs2_ksp_server = ksp_chain;
        else if (strncmp(ksp_chain->ks_name, "rfsproccnt_v3", 13) == 0)
            nfs3_ksp_server = ksp_chain;
        else if (strncmp(ksp_chain->ks_name, "rfsproccnt_v4", 13) == 0)
            nfs4_ksp_server = ksp_chain;
        else if (strncmp(ksp_chain->ks_name, "rfsreqcnt_v2", 12) == 0)
            nfs2_ksp_client = ksp_chain;
        else if (strncmp(ksp_chain->ks_name, "rfsreqcnt_v3", 12) == 0)
            nfs3_ksp_client = ksp_chain;
        else if (strncmp(ksp_chain->ks_name, "rfsreqcnt_v4", 12) == 0)
            nfs4_ksp_client = ksp_chain;
    }

    return (0);
} /* int nfs_init */
#endif

#define BUFSIZE 1024
#if HAVE_LIBKSTAT

static void nfs2_procedures_submit(unsigned long long *val,
        const char *plugin_instance, char *nfs_ver, int len) {
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;
    char pl_instance[30];
    int i;

    vl.values = values;
    vl.values_len = 1;
    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "nfs", sizeof (vl.plugin));
    sstrncpy(pl_instance, nfs_ver, strlen(nfs_ver) + 1);
    strcat(pl_instance, plugin_instance);
    sstrncpy(vl.plugin_instance, pl_instance,
            sizeof (vl.plugin_instance));
    sstrncpy(vl.type, "nfs_procedure", sizeof (vl.type));


    for (i = 0; i < len; i++) {
        values[0].derive = val[i];
        if (strcmp(nfs_ver, "nfs2") == 0)
            sstrncpy(vl.type_instance, nfs2_procedures_names[i],
                sizeof (vl.type_instance));
        else if (strcmp(nfs_ver, "nfs3") == 0)
            sstrncpy(vl.type_instance, nfs3_procedures_names[i],
                sizeof (vl.type_instance));
        else if (strcmp(nfs_ver, "nfs4") == 0) {
            sstrncpy(vl.type_instance, nfs4_procedures_names[i],
                    sizeof (vl.type_instance));
        }

        DEBUG("%s-%s/nfs_procedure-%s = %lld",
                vl.plugin, vl.plugin_instance,
                vl.type_instance, val[i]);
        plugin_dispatch_values(&vl);
    }
}
#endif

static void nfs_procedures_submit(const char *plugin_instance,
        unsigned long long *val, const char **names, int len) {

    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;
    int i;

    vl.values = values;
    vl.values_len = 1;
    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "nfs", sizeof (vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance,
            sizeof (vl.plugin_instance));
    sstrncpy(vl.type, "nfs_procedure", sizeof (vl.type));

    for (i = 0; (i < len); i++) {
        values[0].derive = val[i];
        sstrncpy(vl.type_instance, names[i],
                sizeof (vl.type_instance));
        DEBUG("%s-%s/nfs_procedure-%s = %llu",
                vl.plugin, vl.plugin_instance,
                vl.type_instance, val[i]);
        plugin_dispatch_values(&vl);
    }
} /* void nfs_procedures_submit */

static void nfs_read_stats_file(FILE *fh, char *inst) {
    char buffer[BUFSIZE];

    char plugin_instance[DATA_MAX_NAME_LEN];

    char *fields[48];
    int numfields = 0;

    if (fh == NULL)
        return;

    while (fgets(buffer, BUFSIZE, fh) != NULL) {
        numfields = strsplit(buffer, fields, 48);

        if (((numfields - 2) != nfs2_procedures_names_num)
                && ((numfields - 2)
                != nfs3_procedures_names_num))
            continue;

        if (strcmp(fields[0], "proc2") == 0) {
            int i;
            unsigned long long *values;

            if ((numfields - 2) != nfs2_procedures_names_num) {
                WARNING("nfs plugin: Wrong "
                        "number of fields (= %i) "
                        "for NFSv2 statistics.",
                        numfields - 2);
                continue;
            }

            ssnprintf(plugin_instance, sizeof (plugin_instance),
                    "v2%s", inst);

            values = (unsigned long long *) malloc(nfs2_procedures_names_num * sizeof (unsigned long long));
            if (values == NULL) {
                char errbuf[1024];
                ERROR("nfs plugin: malloc "
                        "failed: %s",
                        sstrerror(errno, errbuf, sizeof (errbuf)));
                continue;
            }

            for (i = 0; i < nfs2_procedures_names_num; i++)
                values[i] = atoll(fields[i + 2]);

            nfs_procedures_submit(plugin_instance, values,
                    nfs2_procedures_names,
                    nfs2_procedures_names_num);

            free(values);
        } else if (strncmp(fields[0], "proc3", 5) == 0) {
            int i;
            unsigned long long *values;

            if ((numfields - 2) != nfs3_procedures_names_num) {
                WARNING("nfs plugin: Wrong "
                        "number of fields (= %i) "
                        "for NFSv3 statistics.",
                        numfields - 2);
                continue;
            }

            ssnprintf(plugin_instance, sizeof (plugin_instance),
                    "v3%s", inst);

            values = (unsigned long long *) malloc(nfs3_procedures_names_num * sizeof (unsigned long long));
            if (values == NULL) {
                char errbuf[1024];
                ERROR("nfs plugin: malloc "
                        "failed: %s",
                        sstrerror(errno, errbuf, sizeof (errbuf)));
                continue;
            }

            for (i = 0; i < nfs3_procedures_names_num; i++)
                values[i] = atoll(fields[i + 2]);

            nfs_procedures_submit(plugin_instance, values,
                    nfs3_procedures_names,
                    nfs3_procedures_names_num);

            free(values);
        }
    } /* while (fgets (buffer, BUFSIZE, fh) != NULL) */
} /* void nfs_read_stats_file */
#undef BUFSIZE

#if HAVE_LIBKSTAT

static void nfs2_read_kstat(kstat_t *ksp, char *inst) {

    unsigned long long values[nfs2_procedures_names_num];

    kstat_read(kc, ksp, NULL);
    values[0] = get_kstat_value(ksp, "null");
    values[1] = get_kstat_value(ksp, "getattr");
    values[2] = get_kstat_value(ksp, "setattr");
    values[3] = get_kstat_value(ksp, "root");
    values[4] = get_kstat_value(ksp, "lookup");
    values[5] = get_kstat_value(ksp, "readlink");
    values[6] = get_kstat_value(ksp, "read");
    values[7] = get_kstat_value(ksp, "wrcache");
    values[8] = get_kstat_value(ksp, "write");
    values[9] = get_kstat_value(ksp, "create");
    values[10] = get_kstat_value(ksp, "remove");
    values[11] = get_kstat_value(ksp, "rename");
    values[12] = get_kstat_value(ksp, "link");
    values[13] = get_kstat_value(ksp, "symlink");
    values[14] = get_kstat_value(ksp, "mkdir");
    values[15] = get_kstat_value(ksp, "rmdir");
    values[16] = get_kstat_value(ksp, "readdir");
    values[17] = get_kstat_value(ksp, "statfs");

    nfs2_procedures_submit(values, inst, "nfs2", nfs2_procedures_names_num);
}

static void nfs3_read_kstat(kstat_t *ksp, char *inst) {
    unsigned long long values[nfs3_procedures_names_num];


    kstat_read(kc, ksp, NULL);
    values[0] = get_kstat_value(ksp, "null");
    values[1] = get_kstat_value(ksp, "getattr");
    values[2] = get_kstat_value(ksp, "setattr");
    values[3] = get_kstat_value(ksp, "lookup");
    values[4] = get_kstat_value(ksp, "access");
    values[5] = get_kstat_value(ksp, "readlink");
    values[6] = get_kstat_value(ksp, "read");
    values[7] = get_kstat_value(ksp, "write");
    values[8] = get_kstat_value(ksp, "create");
    values[9] = get_kstat_value(ksp, "mkdir");
    values[10] = get_kstat_value(ksp, "symlink");
    values[11] = get_kstat_value(ksp, "mknod");
    values[12] = get_kstat_value(ksp, "remove");
    values[13] = get_kstat_value(ksp, "rmdir");
    values[14] = get_kstat_value(ksp, "rename");
    values[15] = get_kstat_value(ksp, "link");
    values[16] = get_kstat_value(ksp, "readdir");
    values[17] = get_kstat_value(ksp, "readdirplus");
    values[18] = get_kstat_value(ksp, "fsstat");
    values[19] = get_kstat_value(ksp, "fsinfo");
    values[20] = get_kstat_value(ksp, "pathconf");
    values[21] = get_kstat_value(ksp, "commit");

    nfs2_procedures_submit(values, inst, "nfs3", nfs3_procedures_names_num);

}

static void nfs4_read_kstat(kstat_t *ksp, char *inst) {
    unsigned long long values[nfs4_procedures_names_num];

    kstat_read(kc, ksp, NULL);

    values[0] = get_kstat_value(ksp, "null");
    values[1] = get_kstat_value(ksp, "compound");
    values[2] = get_kstat_value(ksp, "reserved");
    values[3] = get_kstat_value(ksp, "access");
    values[4] = get_kstat_value(ksp, "close");
    values[5] = get_kstat_value(ksp, "commit");
    values[6] = get_kstat_value(ksp, "create");
    values[7] = get_kstat_value(ksp, "delegpurge");
    values[8] = get_kstat_value(ksp, "delegreturn");
    values[9] = get_kstat_value(ksp, "getattr");
    values[10] = get_kstat_value(ksp, "getfh");
    values[11] = get_kstat_value(ksp, "link");
    values[12] = get_kstat_value(ksp, "lock");
    values[13] = get_kstat_value(ksp, "lockt");
    values[14] = get_kstat_value(ksp, "locku");
    values[15] = get_kstat_value(ksp, "lookup");
    values[16] = get_kstat_value(ksp, "lookupp");
    values[17] = get_kstat_value(ksp, "nverify");
    values[18] = get_kstat_value(ksp, "open");
    values[19] = get_kstat_value(ksp, "openattr");
    values[20] = get_kstat_value(ksp, "open_confirm");
    values[21] = get_kstat_value(ksp, "open_downgrade");
    values[22] = get_kstat_value(ksp, "putfh");
    values[23] = get_kstat_value(ksp, "putpubfh");
    values[24] = get_kstat_value(ksp, "putrootfh");
    values[25] = get_kstat_value(ksp, "read");
    values[26] = get_kstat_value(ksp, "readdir");
    values[27] = get_kstat_value(ksp, "readlink");
    values[28] = get_kstat_value(ksp, "remove");
    values[29] = get_kstat_value(ksp, "rename");
    values[30] = get_kstat_value(ksp, "renew");
    values[31] = get_kstat_value(ksp, "restorefh");
    values[32] = get_kstat_value(ksp, "savefh");
    values[33] = get_kstat_value(ksp, "secinfo");
    values[34] = get_kstat_value(ksp, "setattr");
    values[35] = get_kstat_value(ksp, "setclientid");
    values[36] = get_kstat_value(ksp, "setclientid_confirm");
    values[37] = get_kstat_value(ksp, "verify");
    values[38] = get_kstat_value(ksp, "write");

    nfs2_procedures_submit(values, inst, "nfs4", nfs4_procedures_names_num);

}
#endif

static int nfs_read(void) {
    FILE *fh;

    if ((fh = fopen("/proc/net/rpc/nfs", "r")) != NULL) {
        nfs_read_stats_file(fh, "client");
        fclose(fh);
    }

    if ((fh = fopen("/proc/net/rpc/nfsd", "r")) != NULL) {
        nfs_read_stats_file(fh, "server");
        fclose(fh);
    }

#if HAVE_LIBKSTAT                    
    nfs_init();
    if (nfs2_ksp_client != NULL)
        nfs2_read_kstat(nfs2_ksp_client, "client");
    if (nfs2_ksp_server != NULL)
        nfs2_read_kstat(nfs2_ksp_server, "server");
    if (nfs3_ksp_client != NULL)
        nfs3_read_kstat(nfs3_ksp_client, "client");
    if (nfs3_ksp_server != NULL)
        nfs3_read_kstat(nfs3_ksp_server, "server");
    if (nfs4_ksp_client != NULL)
        nfs4_read_kstat(nfs4_ksp_client, "client");
    if (nfs4_ksp_server != NULL)
        nfs4_read_kstat(nfs4_ksp_server, "server");
    //nfs_kstat(nfs3_ksp_client);
#endif /* defined(HAVE_LIBKSTAT) */

    return (0);
}

void module_register(void) {
    plugin_register_read("nfs", nfs_read);
} /* void module_register */
