/*
 * Coraid NFS server collectd data collector
 *
 * This can be compiled for illumos or Solaris derivative, but the kstats
 * must be available for data to flow.
 *
 * The collectd source contains a generic NFS data collector. However,
 * delivering a single NFS collector for a number of different OSes becomes
 * tedious to manage enhancements. This collector is specifically targeted
 * at illumos and Solaris NFS server data collection. As such it offers more
 * information for NFS servers, but does not collect any client information.
 * Thus it complements, but does not necessarily replace the data collected
 * by the nfs.c collector.
 *
 * Copyright 2014 Coraid, Inc.
 * 
 * MIT License
 * ===========
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/* kstat chain is defined elsewhere */
extern kstat_ctl_t *kc;

/*
 * By default, we look for v2, v3, and v4 data. You can choose
 * to disable any version using the "IgnoreNFSv[234] true" in the
 * collectd.conf file.
 */
static _Bool do_nfsv[] = {B_FALSE, B_FALSE, B_TRUE, B_TRUE, B_TRUE};

/* Referrals are rarely used, set "IgnoreReferrals" option to disable */
static _Bool do_referrals = B_TRUE;

/* ACL information */
static _Bool do_acls = B_TRUE;

static const char *config_keys[] =
{
    "IgnoreNFSv2",
    "IgnoreNFSv3",
    "IgnoreNFSv4",
    "IgnoreReferrals",
    "IgnoreACLs"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/*
 * Some kstats are gauges. The rest are passed as derive.
 * We also need to translate the most obscure kstat names into something
 * a human might recognize. To do this, accept an override to the kstat
 * statistic.
 */

/* pass the counters as collectd derive (int64_t) */
void
nfssvr_stats_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s) {
    value_t v[1];
    long long ll;

    if ((ll = get_kstat_value(ksp, k)) == -1LL) return;
    v[0].derive = (derive_t)ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/*
 * Simple parser to collect the second (last) token from a string with '_' 
 * as separator.
 * Any parse error returns the original string.
 */
char *
nfssvr_stats_get_version(char *s) {
    char *st, *t;
    if ((t = strtok_r(s, "_", &st)) == NULL)
        return (s);
    if ((t = strtok_r(NULL, "_", &st)) == NULL)
        return (s);
    return (t);
}

/* 
 * Most of the work is done in the nfssvr_stats_read() callback.
 */
static int
nfssvr_stats_read(void) {
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    int i;
    char s[DATA_MAX_NAME_LEN];

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "NFSsvr", sizeof (vl.plugin));
    /* for this plugin, all data types are derive */
    sstrncpy(vl.type, "derive", sizeof (vl.type));

    /*
     * there are three sets of kstats to consider
     *    nfs:0:rfsproccnt_v[234]:*  class=misc  procedure counters
     *    nfs:[234]:nfs_server:*     class=misc  RPC statistics
     *    nfs_acl:0:aclproccnt_v[234]:*  class=misc  ACL statistics
     */
    for (i = 2; i <= 4; i++) {
        if (do_nfsv[i] == B_FALSE)
            continue;
        for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
            /* RPC statistics */
            if (ksp->ks_instance == i && 
                (strncmp(ksp->ks_module, "nfs", KSTAT_STRLEN) == 0) &&
                (strncmp(ksp->ks_class, "misc", KSTAT_STRLEN) == 0)) {
                if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                    continue;

                snprintf(s, DATA_MAX_NAME_LEN, "v%d_rpc", i);
                sstrncpy(vl.plugin_instance, s, sizeof(vl.plugin_instance));

                nfssvr_stats_derive(&vl, ksp, "badcalls", NULL);
                nfssvr_stats_derive(&vl, ksp, "calls", NULL);
                if (do_referrals == B_TRUE) {
                    nfssvr_stats_derive(&vl, ksp, "referlinks", NULL);
                    nfssvr_stats_derive(&vl, ksp, "referrals", NULL);
                }
            }

            /* procedure counters */
            snprintf(s, KSTAT_STRLEN, "rfsproccnt_v%d", i);
            if ((strncmp(ksp->ks_name, s, KSTAT_STRLEN) == 0) && ksp->ks_instance == 0 &&
                (strncmp(ksp->ks_module, "nfs", KSTAT_STRLEN) == 0)) {
                if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                    continue;

                snprintf(s, DATA_MAX_NAME_LEN, "v%d_ops", i);
                sstrncpy(vl.plugin_instance, s, sizeof(vl.plugin_instance));

                /* ops common to all versions */
                nfssvr_stats_derive(&vl, ksp, "create", NULL);
                nfssvr_stats_derive(&vl, ksp, "getattr", NULL);
                nfssvr_stats_derive(&vl, ksp, "link", NULL);
                nfssvr_stats_derive(&vl, ksp, "lookup", NULL);
                nfssvr_stats_derive(&vl, ksp, "null", NULL);
                nfssvr_stats_derive(&vl, ksp, "read", NULL);
                nfssvr_stats_derive(&vl, ksp, "readdir", NULL);
                nfssvr_stats_derive(&vl, ksp, "readlink", NULL);
                nfssvr_stats_derive(&vl, ksp, "remove", NULL);
                nfssvr_stats_derive(&vl, ksp, "rename", NULL);
                nfssvr_stats_derive(&vl, ksp, "setattr", NULL);
                nfssvr_stats_derive(&vl, ksp, "write", NULL);

                switch (i) {
                    case 2:
                        nfssvr_stats_derive(&vl, ksp, "mkdir", NULL);
                        nfssvr_stats_derive(&vl, ksp, "rmdir", NULL);
                        nfssvr_stats_derive(&vl, ksp, "root", NULL);
                        nfssvr_stats_derive(&vl, ksp, "statfs", NULL);
                        nfssvr_stats_derive(&vl, ksp, "symlink", NULL);
                        nfssvr_stats_derive(&vl, ksp, "wrcache", NULL);
                        break;
                    case 3:
                        nfssvr_stats_derive(&vl, ksp, "access", NULL);
                        nfssvr_stats_derive(&vl, ksp, "commit", NULL);
                        nfssvr_stats_derive(&vl, ksp, "fsinfo", NULL);
                        nfssvr_stats_derive(&vl, ksp, "fsstat", NULL);
                        nfssvr_stats_derive(&vl, ksp, "mkdir", NULL);
                        nfssvr_stats_derive(&vl, ksp, "mknod", NULL);
                        nfssvr_stats_derive(&vl, ksp, "pathconf", NULL);
                        nfssvr_stats_derive(&vl, ksp, "readdirplus", NULL);
                        nfssvr_stats_derive(&vl, ksp, "rmdir", NULL);
                        nfssvr_stats_derive(&vl, ksp, "symlink", NULL);
                        break;
                    case 4:
                        nfssvr_stats_derive(&vl, ksp, "access", NULL);
                        nfssvr_stats_derive(&vl, ksp, "close", NULL);
                        nfssvr_stats_derive(&vl, ksp, "commit", NULL);
                        nfssvr_stats_derive(&vl, ksp, "compound", NULL);
                        nfssvr_stats_derive(&vl, ksp, "delegpurge", NULL);
                        nfssvr_stats_derive(&vl, ksp, "delegreturn", NULL);
                        nfssvr_stats_derive(&vl, ksp, "getfh", NULL);
                        nfssvr_stats_derive(&vl, ksp, "illegal", NULL);
                        nfssvr_stats_derive(&vl, ksp, "lock", NULL);
                        nfssvr_stats_derive(&vl, ksp, "lockt", NULL);
                        nfssvr_stats_derive(&vl, ksp, "locku", NULL);
                        nfssvr_stats_derive(&vl, ksp, "lookupp", NULL);
                        nfssvr_stats_derive(&vl, ksp, "nverify", NULL);
                        nfssvr_stats_derive(&vl, ksp, "open_confirm", NULL);
                        nfssvr_stats_derive(&vl, ksp, "open_downgrade", NULL);
                        nfssvr_stats_derive(&vl, ksp, "open", NULL);
                        nfssvr_stats_derive(&vl, ksp, "openattr", NULL);
                        nfssvr_stats_derive(&vl, ksp, "putfh", NULL);
                        nfssvr_stats_derive(&vl, ksp, "putpubfh", NULL);
                        nfssvr_stats_derive(&vl, ksp, "putrootfh", NULL);
                        nfssvr_stats_derive(&vl, ksp, "release_lockowner", NULL);
                        nfssvr_stats_derive(&vl, ksp, "renew", NULL);
                        nfssvr_stats_derive(&vl, ksp, "reserved", NULL);
                        nfssvr_stats_derive(&vl, ksp, "restorefh", NULL);
                        nfssvr_stats_derive(&vl, ksp, "savefh", NULL);
                        nfssvr_stats_derive(&vl, ksp, "secinfo", NULL);
                        nfssvr_stats_derive(&vl, ksp, "setclientid_confirm", NULL);
                        nfssvr_stats_derive(&vl, ksp, "setclientid", NULL);
                        nfssvr_stats_derive(&vl, ksp, "verify", NULL);
                        break;
                }
            }

            /* ACL counters */
            if (do_acls == B_TRUE) {
                snprintf(s, KSTAT_STRLEN, "aclproccnt_v%d", i);
                if ((strncmp(ksp->ks_name, s, KSTAT_STRLEN) == 0) && ksp->ks_instance == 0 &&
                    (strncmp(ksp->ks_module, "nfs_acl", KSTAT_STRLEN) == 0)) {
                    if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                        continue;

                    snprintf(s, DATA_MAX_NAME_LEN, "v%d_acls", i);
                    sstrncpy(vl.plugin_instance, s, sizeof(vl.plugin_instance));

                    /* ops common to all versions */
                    nfssvr_stats_derive(&vl, ksp, "getacl", NULL);
                    nfssvr_stats_derive(&vl, ksp, "setacl", NULL);
                    nfssvr_stats_derive(&vl, ksp, "null", NULL);
                    if ((i == 2) || (i == 3))
                        nfssvr_stats_derive(&vl, ksp, "getxattrdir", NULL);
                }
            }
        }
    }
    return (0);
}

static int 
nfssvr_stats_config (const char *key, const char *value)
{
    if (strncasecmp ("IgnoreNFSv2", key, 11) == 0) {
        do_nfsv[2] = IS_TRUE (value) ? B_FALSE : B_TRUE;
    } else if (strncasecmp ("IgnoreNFSv3", key, 11) == 0) {
        do_nfsv[3] = IS_TRUE (value) ? B_FALSE : B_TRUE;
    } else if (strncasecmp ("IgnoreNFSv4", key, 11) == 0) {
        do_nfsv[4] = IS_TRUE (value) ? B_FALSE : B_TRUE;
    } else if (strncasecmp ("IgnoreReferrals", key, 15) == 0) {
        do_referrals = IS_TRUE (value) ? B_FALSE : B_TRUE;
    } else if (strncasecmp ("IgnoreACLs", key, 10) == 0) {
        do_acls = IS_TRUE (value) ? B_FALSE : B_TRUE;
    } else {
        return (-1);
    }
    return (0);
}


static int
nfssvr_stats_init(void)
{
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("nfssvr_stats plugin: kstat chain control initialization failed");
        return (-1);
    }
    return (0);
}

void
module_register(void)
{
    plugin_register_config ("nfssvr_stats", nfssvr_stats_config,
      config_keys, config_keys_num);
    plugin_register_init ("nfssvr_stats", nfssvr_stats_init);
    plugin_register_read ("nfssvr_stats", nfssvr_stats_read);
}
