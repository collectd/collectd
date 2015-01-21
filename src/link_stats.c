/*
 * Coraid collectd data collector for network link statistics.
 *
 * Suitable for illumos, OpenSolaris, and Solaris 11 derivatives.
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

extern kstat_ctl_t *kc;
char ks_name[KSTAT_STRLEN + 1];
boolean_t include_mac_protect = B_TRUE;         /* spoof stats? */
boolean_t include_broadcast_multicast = B_TRUE; /* broad|multicast stats? */

/*
 * Many of the kstat counters for network stats are not gauges. We do use
 * gauges to report link status and negotiaged speeds.
 *
 * The rest are passed as counters that only increment, using derive type.
 */

/* pass the counters as collectd derive (int64_t) */
void
link_stats_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s)
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

/* pass simple value as derive */
void
link_stats_simple_derive(value_list_t *vl, long long ll, char *k, char *s)
{
    value_t v[1];
    
    v[0].derive = (derive_t) ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/* pass the gauges (double) */
void
link_stats_gauge(value_list_t *vl, kstat_t *ksp, char *k, char *s)
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
 * Simple parser to collect the LINK field from an aggregation name.
 * The name format is "AGGR-LINK", we want the LINK
 */
char *
link_stats_get_aggrs_link(char *s) 
{
    char *t, *last;
    int maxlen = KSTAT_STRLEN;
    t = s;
    last = s;

    while (maxlen > 0 && *t != '\0') {
        if (*t == '-') last = t + 1;
        t++;
        maxlen--;
    }
    return (last);
}

/* 
 * Most of the work is done in the link_stats_read() callback.
 * For brevity, a simplistic approach is taken to match a reasonable
 * collectd and whisper-compatible namespace. The general form is:
 *   Links-<subset>.[gauge|derive]-statistic
 */
static int
link_stats_read(void)
{
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "Links", sizeof (vl.plugin));

    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_name, ks_name, KSTAT_STRLEN) == 0) &&
            (strncmp(ksp->ks_class, "net", KSTAT_STRLEN) == 0)) {
            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
 
            /* module name is either link or aggr-link combo */
            sstrncpy(vl.plugin_instance, ksp->ks_module,
                sizeof (vl.plugin_instance));
            /* record crtime & snaptime since these may change since boot */
            sstrncpy(vl.type, "derive", sizeof (vl.type));
            link_stats_simple_derive(&vl, ksp->ks_crtime, "crtime", NULL);
            link_stats_simple_derive(&vl, ksp->ks_snaptime, "snaptime", NULL);

            link_stats_derive(&vl, ksp, "blockcnt", NULL);
            link_stats_derive(&vl, ksp, "chainunder10", NULL);
            link_stats_derive(&vl, ksp, "chain10to50", NULL);
            link_stats_derive(&vl, ksp, "chainover50", NULL);
            link_stats_derive(&vl, ksp, "idropbytes", NULL);
            link_stats_derive(&vl, ksp, "idrops", NULL);
            link_stats_derive(&vl, ksp, "intrbytes", NULL);
            link_stats_derive(&vl, ksp, "intrs", NULL);
            link_stats_derive(&vl, ksp, "ipackets", NULL);
            link_stats_derive(&vl, ksp, "local", NULL);
            link_stats_derive(&vl, ksp, "localbytes", NULL);
            link_stats_derive(&vl, ksp, "obytes", NULL);
            link_stats_derive(&vl, ksp, "odropbytes", NULL);
            link_stats_derive(&vl, ksp, "odrops", NULL);
            link_stats_derive(&vl, ksp, "oerrors", NULL);
            link_stats_derive(&vl, ksp, "opackets", NULL);
            link_stats_derive(&vl, ksp, "pollbytes", NULL);
            link_stats_derive(&vl, ksp, "polls", NULL);
            link_stats_derive(&vl, ksp, "rbytes", NULL);
            link_stats_derive(&vl, ksp, "rxdrops", NULL);
            link_stats_derive(&vl, ksp, "rxlocal", NULL);
            link_stats_derive(&vl, ksp, "rxlocalbytes", NULL);
            link_stats_derive(&vl, ksp, "txdropts", NULL);
            link_stats_derive(&vl, ksp, "txerrors", NULL);
            link_stats_derive(&vl, ksp, "txlocal", NULL);
            link_stats_derive(&vl, ksp, "txlocalbytes", NULL);
            link_stats_derive(&vl, ksp, "unblockcnt", NULL);

            /*
             * mac protect stats are bumped when a potential conflict
             * is detected:
             *   dhcp spoof
             *   ip address spoof
             */
            if (include_mac_protect == B_TRUE) {
                link_stats_derive(&vl, ksp, "dhcpdropped", NULL);
                link_stats_derive(&vl, ksp, "dhcpspoofed", NULL);
                link_stats_derive(&vl, ksp, "ipspoofed", NULL);
                link_stats_derive(&vl, ksp, "macspoofed", NULL);
                link_stats_derive(&vl, ksp, "restricted", NULL);
            }

            /* broadcast/multicast stats */
            if (include_broadcast_multicast == B_TRUE) {
                link_stats_derive(&vl, ksp, "multircv", NULL);
                link_stats_derive(&vl, ksp, "multircvbytes", NULL);
                link_stats_derive(&vl, ksp, "multixmt", NULL);
                link_stats_derive(&vl, ksp, "multixmtbytes", NULL);
                link_stats_derive(&vl, ksp, "bcstrcvbytes", NULL);
                link_stats_derive(&vl, ksp, "bcstxmtbytes", NULL);
                link_stats_derive(&vl, ksp, "brdcstrcv", NULL);
                link_stats_derive(&vl, ksp, "brdcstxmt", NULL);
            }
        }
    }
    return (0);
}

static int
link_stats_init(void)
{
    kstat_t *ksp = NULL;
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("link_stats plugin: kstat chain control initialization failed");
        return (-1);
    }

    /* 
     * find name of link kstats we care about
     * for Solaris 11, this is "link"
     * for illumos or older Solaris, this is "mac_misc_stat"
     */
    strlcpy(ks_name, "none", sizeof(ks_name));
    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((ksp->ks_instance == 0) &&
           (strncmp(ksp->ks_class, "net", KSTAT_STRLEN) == 0)) {
            if ((strncmp(ksp->ks_name, "mac_misc_stat", KSTAT_STRLEN) == 0) ||
                (strncmp(ksp->ks_name, "link", KSTAT_STRLEN) == 0)) {
                strlcpy(ks_name, ksp->ks_name, sizeof(ks_name));
                return (0);
            }
        }
    }
    ERROR ("cannot find misc kstat info for links");
    return (0);
}

void
module_register(void)
{
    plugin_register_init ("link_stats", link_stats_init);
    plugin_register_read ("link_stats", link_stats_read);
}
