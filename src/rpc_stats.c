/*
 * Coraid RPC collectd data collector
 *
 * Suitable for CorOS 8. 
 * This can be compiled for illumos or Solaris derivative, but the kstats
 * must be available for data to flow.
 *
 * The kstats of interest have the (kstat -p) form:
 *   rpcmod:0:svc_[program]_[instance]_[pid]
 * These are translated to 
 *   RPC-[program].[gauge|derive]-statistic
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

/* 
 * Solaris complains about compiling in large file environments for 32-bit 
 * processes. This is annoying and not really pertinent to how we use procfs.
 * As a workaround, fake out the size of the file offset for this include.
 */
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 32
#include <procfs.h>

extern kstat_ctl_t *kc;
/*
 * Some kstats are gauges. The rest are passed as derive.
 * We also need to translate the most obscure kstat names into something
 * a human might recognize. To do this, accept an override to the kstat
 * statistic.
 */

/* pass the counters as collectd derive (int64_t) */
void
rpc_stats_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s) {
    value_t v[1];
    long long ll;

    if ((ll = get_kstat_value(ksp, k)) == -1LL) return;
    v[0].derive = (derive_t)ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/* pass the gauges (double) */
void
rpc_stats_gauge(value_list_t *vl, kstat_t *ksp, char *k, char *s) {
    value_t v[1];
    long long ll;

    if ((ll = get_kstat_value(ksp, k)) == -1LL) return;
    v[0].gauge = (gauge_t)ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/* since RPC services can be restarted, record crtime and snaptime */
/* send crtime and snaptime as derive */
void
rpc_stats_send_kstimes(value_list_t *vl, kstat_t *ksp) {
    value_t v[1];

    vl->values = v;
    vl->values_len = 1;

    v[0].derive = (derive_t)ksp->ks_crtime;
    sstrncpy(vl->type_instance, "crtime", sizeof(vl->type_instance));
    plugin_dispatch_values(vl);

    v[0].derive = (derive_t)ksp->ks_snaptime;
    sstrncpy(vl->type_instance, "snaptime", sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/*
 * simple parser to collect the [program] from a string
 * any parse error returns the original string
 */
char *
rpc_stats_get_instance(char *s) {
    char *st, *t;
    if ((t = strtok_r(s, "_", &st)) == NULL)
        return (s);
    if ((t = strtok_r(NULL, "_", &st)) == NULL)
        return (s);
    return (t);
}
/*
 * simple parser to collect the pid field from a name
 */
char *
rpc_stats_get_pid(char *s) {
    char *t, *last;
    int maxlen = KSTAT_STRLEN;
    t = s;
    last = s;

    while (maxlen > 0 && *t != '\0') {
        if (*t == '_') last = t + 1;
        t++;
        maxlen--;
    }
    return (last);
}

/* 
 * Most of the work is done in the rpc_stats_read() callback.
 */
static int
rpc_stats_read(void) {
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    psinfo_t info;
    int fd;
    char pname[MAXNAMELEN];

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "RPC", sizeof (vl.plugin));

    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_class, "rpcmod", KSTAT_STRLEN) == 0)) {
            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;

            /* Solaris uses name format: stp_[instance?]_[zone?]_[pid] */
            if (strncmp(ksp->ks_name, "stp_", 4) == 0) {
                /* lookup the program name from pid */
                (void) snprintf(pname, sizeof (pname), "/proc/%s/psinfo", 
                    rpc_stats_get_pid(ksp->ks_name));
                if (((fd = open(pname, O_RDONLY)) != -1) &&
                    (read(fd, (char *)&info, sizeof(info)) != -1)) {
                    sstrncpy(vl.plugin_instance, info.pr_fname, sizeof (vl.plugin_instance));
                    (void) close(fd);
                } else {
                    sstrncpy(vl.plugin_instance, rpc_stats_get_instance(ksp->ks_name), 
                        sizeof (vl.plugin_instance));                    
                }

                sstrncpy(vl.type, "gauge", sizeof (vl.type));
                rpc_stats_gauge(&vl, ksp, "active_threads", NULL);
                rpc_stats_gauge(&vl, ksp, "avg_throttle", NULL);
                rpc_stats_gauge(&vl, ksp, "csw_control", NULL);
                rpc_stats_gauge(&vl, ksp, "flow_control", NULL);
                rpc_stats_gauge(&vl, ksp, "requests_inq", NULL);

                sstrncpy(vl.type, "derive", sizeof (vl.type));
                rpc_stats_send_kstimes(&vl, ksp);
                rpc_stats_derive(&vl, ksp, "dispatch_failed", NULL);
            }

            /* CorOS uses name format: svc_[program]_[instance]_[pid] */
            if (strncmp(ksp->ks_name, "svc_", 4) == 0) {
                sstrncpy(vl.plugin_instance, rpc_stats_get_instance(ksp->ks_name), 
                    sizeof (vl.plugin_instance));

                sstrncpy(vl.type, "gauge", sizeof (vl.type));
                rpc_stats_gauge(&vl, ksp, "pool_mem_hiwat", NULL);
                rpc_stats_gauge(&vl, ksp, "pool_mem_inuse", NULL);
                rpc_stats_gauge(&vl, ksp, "pool_mem_lowat", NULL);
                rpc_stats_gauge(&vl, ksp, "pool_mem_max", NULL);
                rpc_stats_gauge(&vl, ksp, "pool_reqs_backlog", NULL);
                rpc_stats_gauge(&vl, ksp, "pool_reqs_backlog_max", NULL);
                rpc_stats_gauge(&vl, ksp, "pool_xprt_eager", NULL);
                rpc_stats_gauge(&vl, ksp, "xprt_ready_qcnt", NULL);
                rpc_stats_gauge(&vl, ksp, "xprt_ready_qsize", NULL);
                rpc_stats_gauge(&vl, ksp, "xprt_registered", NULL);

                sstrncpy(vl.type, "derive", sizeof (vl.type));
                rpc_stats_send_kstimes(&vl, ksp);
                rpc_stats_derive(&vl, ksp, "pool_flow_ctl_off", NULL);
                rpc_stats_derive(&vl, ksp, "pool_flow_ctl_on", NULL);
                rpc_stats_derive(&vl, ksp, "pool_overflow_off", NULL);
                rpc_stats_derive(&vl, ksp, "pool_overflow_on", NULL);
                rpc_stats_derive(&vl, ksp, "xprt_flow_ctl_off", NULL);
                rpc_stats_derive(&vl, ksp, "xprt_flow_ctl_on", NULL);
            }
        }
    }
    return (0);
}

static int
rpc_stats_init(void)
{
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("rpc_stats plugin: kstat chain control initialization failed");
        return (-1);
    }
    return (0);
}

void
module_register(void)
{
    plugin_register_init ("rpc_stats", rpc_stats_init);
    plugin_register_read ("rpc_stats", rpc_stats_read);
}
