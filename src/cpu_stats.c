/*
 * Coraid CPU detailed stats collector
 *
 * Suitable for illumos, CorOS, and Solaris 11 derivatives.
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

extern kstat_ctl_t *kc;

/* pass the counters as collectd derive (int64_t) */
void
cpu_stats_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s)
{
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
cpu_stats_gauge(value_list_t *vl, kstat_t *ksp, char *k, char *s)
{
    value_t v[1];
    long long ll;

    if ((ll = get_kstat_value(ksp, k)) == -1LL) return;
    v[0].gauge = (gauge_t)ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/* 
 * Most of the work is done in the cpu_stats_read() callback.
 * For brevity, a simplistic approach is taken to match a reasonable
 * collectd and whisper-compatible namespace. The general form is:
 *   CPU_stats-[cpu instance].[gauge|derive]-statistic
 * 
 * As a convenience, we also collect summary derive types for all
 */
static int
cpu_stats_read(void)
{
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    char s[16];   /* cpu instance as string */

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "CPU_stats", sizeof (vl.plugin));

    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_module, "cpu", KSTAT_STRLEN) == 0) &&
            (strncmp(ksp->ks_name, "sys", KSTAT_STRLEN) == 0) &&
            (strncmp(ksp->ks_class, "misc", KSTAT_STRLEN) == 0)) {

            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
            (void) snprintf(s, sizeof (s), "%d", ksp->ks_instance);
            sstrncpy(vl.plugin_instance, s, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));

            /** collectors that are commented out exist, but tend to be less interesting **/
            /* cpu_stats_derive(&vl, ksp, "bawrite", NULL); */
            /* cpu_stats_derive(&vl, ksp, "bread", NULL); */
            /* cpu_stats_derive(&vl, ksp, "bwrite", NULL); */
            /* cpu_stats_derive(&vl, ksp, "canch", NULL); */
            /* cpu_stats_derive(&vl, ksp, "cpu_load_intr", NULL); */            
            cpu_stats_derive(&vl, ksp, "cpu_nsec_idle", NULL);
            cpu_stats_derive(&vl, ksp, "cpu_nsec_intr", NULL);
            cpu_stats_derive(&vl, ksp, "cpu_nsec_kernel", NULL);
            cpu_stats_derive(&vl, ksp, "cpu_nsec_user", NULL);
            /* cpu_stats_derive(&vl, ksp, "cpu_ticks_idle", NULL); */
            /* cpu_stats_derive(&vl, ksp, "cpu_ticks_kernel", NULL); */
            /* cpu_stats_derive(&vl, ksp, "cpu_ticks_user", NULL); */
            /* cpu_stats_derive(&vl, ksp, "cpu_ticks_wait", NULL); */
            cpu_stats_derive(&vl, ksp, "cpumigrate", NULL);
            cpu_stats_derive(&vl, ksp, "dtrace_probes", NULL);
            /* cpu_stats_derive(&vl, ksp, "idlethread", NULL); */
            cpu_stats_derive(&vl, ksp, "intr", NULL);
            cpu_stats_derive(&vl, ksp, "intrblk", NULL);
            cpu_stats_derive(&vl, ksp, "intrthread", NULL);
            cpu_stats_derive(&vl, ksp, "intrunpin", NULL);
            cpu_stats_derive(&vl, ksp, "inv_swtch", NULL);
            /* cpu_stats_derive(&vl, ksp, "iowait", NULL); */
            /* cpu_stats_derive(&vl, ksp, "lread", NULL); */
            /* cpu_stats_derive(&vl, ksp, "lwrite", NULL); */
            /* cpu_stats_derive(&vl, ksp, "mdmint", NULL); */
            /* cpu_stats_derive(&vl, ksp, "modload", NULL); */
            /* cpu_stats_derive(&vl, ksp, "modunload", NULL); */
            /* cpu_stats_derive(&vl, ksp, "msg", NULL); */
            /* cpu_stats_derive(&vl, ksp, "mutex_adenters", NULL); */
            /* cpu_stats_derive(&vl, ksp, "namei", NULL); */
            /* cpu_stats_derive(&vl, ksp, "nthreads", NULL); */
            /* cpu_stats_derive(&vl, ksp, "outch", NULL); */
            /* cpu_stats_derive(&vl, ksp, "phread", NULL); */
            /* cpu_stats_derive(&vl, ksp, "phwrite", NULL); */
            /* cpu_stats_derive(&vl, ksp, "procovf", NULL); */
            /* cpu_stats_derive(&vl, ksp, "pswitch", NULL); */
            /* cpu_stats_derive(&vl, ksp, "rawch", NULL); */
            /* cpu_stats_derive(&vl, ksp, "rcvint", NULL); */
            /* cpu_stats_derive(&vl, ksp, "readch", NULL); */
            /* cpu_stats_derive(&vl, ksp, "rw_rdfails", NULL); */
            /* cpu_stats_derive(&vl, ksp, "rw_wrfails", NULL); */
            /* cpu_stats_derive(&vl, ksp, "sema", NULL); */
            /* cpu_stats_derive(&vl, ksp, "syscall", NULL); */
            /* cpu_stats_derive(&vl, ksp, "sysexec", NULL); */
            /* cpu_stats_derive(&vl, ksp, "sysfork", NULL); */
            /* cpu_stats_derive(&vl, ksp, "sysread", NULL); */
            /* cpu_stats_derive(&vl, ksp, "sysvfork", NULL); */
            /* cpu_stats_derive(&vl, ksp, "syswrite", NULL); */
            /* cpu_stats_derive(&vl, ksp, "trap", NULL); */
            /* cpu_stats_derive(&vl, ksp, "ufsdirblk", NULL); */
            /* cpu_stats_derive(&vl, ksp, "ufsiget", NULL); */
            /* cpu_stats_derive(&vl, ksp, "ufsinopage", NULL); */
            /* cpu_stats_derive(&vl, ksp, "ufsipage", NULL); */
            /* cpu_stats_derive(&vl, ksp, "wait_ticks_io", NULL); */
            /* cpu_stats_derive(&vl, ksp, "writech", NULL); */
            cpu_stats_derive(&vl, ksp, "xcalls", NULL);
            /* cpu_stats_derive(&vl, ksp, "xmtint", NULL); */
        }
    }
    /* Some Intel processors have turbo mode, collect ACNT and MCNT MSRs */
    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_module, "turbo", KSTAT_STRLEN) == 0) &&
            (strncmp(ksp->ks_name, "turbo", KSTAT_STRLEN) == 0) &&
            (strncmp(ksp->ks_class, "misc", KSTAT_STRLEN) == 0)) {

            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
            (void) snprintf(s, sizeof (s), "%d", ksp->ks_instance);
            sstrncpy(vl.plugin_instance, s, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));
            cpu_stats_derive(&vl, ksp, "turbo_acnt", NULL);
            cpu_stats_derive(&vl, ksp, "turbo_mcnt", NULL);
        }
    }
    return (0);
}

static int
cpu_stats_init(void)
{
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("cpu_stats plugin: kstat chain control initialization failed");
        return (-1);
    }
    return (0);
}

void
module_register(void)
{
    plugin_register_init ("cpu_stats", cpu_stats_init);
    plugin_register_read ("cpu_stats", cpu_stats_read);
}
