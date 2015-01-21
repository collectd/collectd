/*
 * Coraid AoE collectd data collector
 *
 * Suitable for CorOS, illumos, and Solaris 11 derivatives using
 * Coraid's AoE software target.
 *
 * AoE targets use the aoet module kstats.
 *   aoet:0:aoet_tgt_ADDR:target-alias 
 *     Contains the human-readable name associated with ADDR.
 *
 * AoE ports use the aoe module kstats. 
 *   aoe:0:aoet_port_ADDR:port-alias 
 *     Contains the human-readable name associated with ADDR.
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
#include <kstat.h>
#include <libnvpair.h>

extern kstat_ctl_t *kc;
/*
 * Many of the kstat counters for ARC stats are not gauges.
 * For those that are, we pass as gauges. The rest are passed as derive.
 * We also need to translate the most obscure kstat names into something
 * a human might recognize. To do this, accept an override to the kstat
 * statistic.
 */

/* pass the counters as collectd derive (int64_t) */
void
aoe_stats_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s)
{
    value_t v[1];
    long long ll = 0;

    if ((ll = get_kstat_value(ksp, k)) == -1LL) return;
    v[0].derive = (derive_t)ll;
    vl->values = v;
    vl->values_len = 1;
    sstrncpy(vl->type_instance, (s == NULL ? k : s), sizeof(vl->type_instance));
    plugin_dispatch_values(vl);
}

/* pass the gauges (double) */
void
aoe_stats_gauge(value_list_t *vl, kstat_t *ksp, char *k, char *s)
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
 * collectd's get_kstat_value() only understands numbers :-(
 * This implementation understands strings.
 */
char *
get_kstat_string(kstat_t *ksp, char *name) {
    kstat_named_t *kn;

    if (ksp == NULL) {
        ERROR("get_kstat_string: ksp not valid");
        return (NULL);
    }
    if (ksp->ks_type != KSTAT_TYPE_NAMED) {
        ERROR("get_kstat_string: ksp->ks_type not KSTAT_TYPE_NAMED");
        return (NULL);        
    }
    if ((kn = (kstat_named_t *) kstat_data_lookup(ksp, name)) == NULL) {
        return (NULL);
    }
    if (kn->data_type != KSTAT_DATA_STRING) {
        return (NULL);
    }
    return (KSTAT_NAMED_STR_PTR(kn));
}


/*
 * simple parser to collect the ADDR field from a name
 * the name format is "aoet_tgt_ADDR", we want the ADDR
 */
char *
aoe_get_addr(char *s) {
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

static int
aoe_aoet_stats_read(void)
{    
    nvlist_t *aliases;   /* list for mapping ids to names (aliases) */
    nvlist_t *aliases_child;
    nvpair_t *alias;
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    char *s;
    int64_t l;
    char t[KSTAT_STRLEN << 1];
    char *a;

    /* 
     * In the AoE target, each target's instance can associated with it's
     * targe-alias. Collect these into an nvlist for cross-referencing.
     *   target-alias = instance
     */
    if(nvlist_alloc(&aliases, NV_UNIQUE_NAME, 0)) {
        ERROR("nvlist allocation failed");
        return(-1);
    }
    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_module, "aoet", KSTAT_STRLEN) == 0) &&
           (strncmp(ksp->ks_class, "misc", KSTAT_STRLEN) == 0)) {
            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
            if ((s = get_kstat_string(ksp, "target-alias")) == NULL)
                continue;
            if (nvlist_alloc(&aliases_child, NV_UNIQUE_NAME, 0)) 
                continue;
            if (nvlist_add_string(aliases_child, "alias", s) ||
                nvlist_add_int64(aliases_child, "instance", 
                    (int64_t)ksp->ks_instance)) {
                ERROR("couldn't add to nvpair");
                return (-1);
            }
            if (nvlist_add_nvlist(aliases, aoe_get_addr(ksp->ks_name), 
                aliases_child)) {
                ERROR("couldn't add child nvlist to aliases");
                continue;
            }
        }
    }

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));

    /* cycle through the targets and locate their various kstats */
    for (alias = nvlist_next_nvpair(aliases, NULL); alias != NULL;
        alias = nvlist_next_nvpair(aliases, alias)) {
        if (nvpair_type(alias) != DATA_TYPE_NVLIST) {
            ERROR("expected DATA_TYPE_NVLIST, got %d for %s\n", 
                nvpair_type(alias), nvpair_name(alias));
            continue;
        }
        if (nvpair_value_nvlist(alias, &aliases_child) != 0) {
            ERROR("unable to get child nvlist for %s\n", nvpair_name(alias));
            continue;
        }
        if (nvlist_lookup_int64(aliases_child, "instance", &l)) {
            ERROR("unable to get instance for %s\n", nvpair_name(alias));
            continue;
        } 
        if (nvlist_lookup_string(aliases_child, "alias", &a)) {
            ERROR("unable to get alias for %s\n", nvpair_name(alias));
            continue;
        }
        snprintf(t, sizeof (t), "aoet_tgt_aoe_%s", nvpair_name(alias));
        get_kstat(&ksp, "aoet", (int)l, t);
        if (ksp != NULL) {
            sstrncpy(vl.plugin, "AoE-Target-Ops-In", sizeof (vl.plugin));
            sstrncpy(vl.plugin_instance, a, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));
            aoe_stats_derive(&vl, ksp, "in_ata_flush", "ata_flush");
            aoe_stats_derive(&vl, ksp, "in_ata_flushext", "ata_flushext");
            aoe_stats_derive(&vl, ksp, "in_ata_identify", "ata_identify");
            aoe_stats_derive(&vl, ksp, "in_ata_read", "ata_read");
            aoe_stats_derive(&vl, ksp, "in_ata_readext", "ata_readext");
            aoe_stats_derive(&vl, ksp, "in_ata_unknown", "ata_unknown");
            aoe_stats_derive(&vl, ksp, "in_ata_wbytes", "ata_wbytes");
            aoe_stats_derive(&vl, ksp, "in_ata_write", "ata_write");
            aoe_stats_derive(&vl, ksp, "in_ata_writeext", "ata_writeext");
            aoe_stats_derive(&vl, ksp, "in_kresrel_register", "kresrel_register");
            aoe_stats_derive(&vl, ksp, "in_kresrel_replace", "kresrel_replace");
            aoe_stats_derive(&vl, ksp, "in_kresrel_reserve", "kresrel_reserve");
            aoe_stats_derive(&vl, ksp, "in_kresrel_reset", "kresrel_reset");
            aoe_stats_derive(&vl, ksp, "in_kresrel_status", "kresrel_status");
            aoe_stats_derive(&vl, ksp, "in_kresrel_unknown", "kresrel_unknown");
            aoe_stats_derive(&vl, ksp, "in_krrtype_rw_g", "krrtype_rw_g");
            aoe_stats_derive(&vl, ksp, "in_krrtype_rw_o", "krrtype_rw_o");
            aoe_stats_derive(&vl, ksp, "in_krrtype_rw_s", "krrtype_rw_s");
            aoe_stats_derive(&vl, ksp, "in_krrtype_unknown", "krrtype_unknown");
            aoe_stats_derive(&vl, ksp, "in_krrtype_w_g", "krrtype_w_g");
            aoe_stats_derive(&vl, ksp, "in_krrtype_w_o", "krrtype_w_o");
            aoe_stats_derive(&vl, ksp, "in_krrtype_w_s", "krrtype_w_s");
            aoe_stats_derive(&vl, ksp, "in_mask_edit", "mask_edit");
            aoe_stats_derive(&vl, ksp, "in_mask_read", "mask_read");
            aoe_stats_derive(&vl, ksp, "in_mask_unknown", "mask_unknown");
            aoe_stats_derive(&vl, ksp, "in_mdir_add", "mdir_add");
            aoe_stats_derive(&vl, ksp, "in_mdir_del", "mdir_del");
            aoe_stats_derive(&vl, ksp, "in_mdir_noop", "mdir_noop");
            aoe_stats_derive(&vl, ksp, "in_mdir_unknown", "mdir_unknown");
            aoe_stats_derive(&vl, ksp, "in_qc_forceset", "qc_forceset");
            aoe_stats_derive(&vl, ksp, "in_qc_read", "qc_read");
            aoe_stats_derive(&vl, ksp, "in_qc_set", "qc_set");
            aoe_stats_derive(&vl, ksp, "in_qc_test", "qc_test");
            aoe_stats_derive(&vl, ksp, "in_qc_testprefix", "qc_testprefix");
            aoe_stats_derive(&vl, ksp, "in_qc_testreplace", "qc_testreplace");
            aoe_stats_derive(&vl, ksp, "in_qc_unknown", "qc_unknown");
            aoe_stats_derive(&vl, ksp, "in_resrel_forceset", "resrel_forceset");
            aoe_stats_derive(&vl, ksp, "in_resrel_read", "resrel_read");
            aoe_stats_derive(&vl, ksp, "in_resrel_set", "resrel_set");
            aoe_stats_derive(&vl, ksp, "in_resrel_unknown", "resrel_unknown");

            sstrncpy(vl.plugin, "AoE-Target-Ops-Out", sizeof (vl.plugin));
            aoe_stats_derive(&vl, ksp, "out_ata_err_abrt", "ata_err_abrt");
            aoe_stats_derive(&vl, ksp, "out_ata_err_amnf", "ata_err_amnf");
            aoe_stats_derive(&vl, ksp, "out_ata_err_bbk_icrc", "ata_err_bbk_icrc");
            aoe_stats_derive(&vl, ksp, "out_ata_err_eom", "ata_err_eom");
            aoe_stats_derive(&vl, ksp, "out_ata_err_idnf", "ata_err_idnf");
            aoe_stats_derive(&vl, ksp, "out_ata_err_mc", "ata_err_mc");
            aoe_stats_derive(&vl, ksp, "out_ata_err_mcr", "ata_err_mcr");
            aoe_stats_derive(&vl, ksp, "out_ata_err_unc", "ata_err_unc");
            aoe_stats_derive(&vl, ksp, "out_ata_flush", "ata_flush");
            aoe_stats_derive(&vl, ksp, "out_ata_flushext", "ata_flushext");
            aoe_stats_derive(&vl, ksp, "out_ata_identify", "ata_identify");
            aoe_stats_derive(&vl, ksp, "out_ata_rbytes", "ata_rbytes");
            aoe_stats_derive(&vl, ksp, "out_ata_read", "ata_read");
            aoe_stats_derive(&vl, ksp, "out_ata_readext", "ata_readext");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_ae", "ata_sta_ae");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_bsy", "ata_sta_bsy");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_df", "ata_sta_df");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_drdy", "ata_sta_drdy");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_drq", "ata_sta_drq");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_dwe", "ata_sta_dwe");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_err", "ata_sta_err");
            aoe_stats_derive(&vl, ksp, "out_ata_sta_sda", "ata_sta_sda");
            aoe_stats_derive(&vl, ksp, "out_ata_write", "ata_write");
            aoe_stats_derive(&vl, ksp, "out_ata_writeext", "ata_writeext");
            aoe_stats_derive(&vl, ksp, "out_kresrel_register", "kresrel_register");
            aoe_stats_derive(&vl, ksp, "out_kresrel_replace", "kresrel_replace");
            aoe_stats_derive(&vl, ksp, "out_kresrel_reserve", "kresrel_reserve");
            aoe_stats_derive(&vl, ksp, "out_kresrel_reset", "kresrel_reset");
            aoe_stats_derive(&vl, ksp, "out_kresrel_status", "kresrel_status");
            aoe_stats_derive(&vl, ksp, "out_mask_edit", "mask_edit");
            aoe_stats_derive(&vl, ksp, "out_mask_read", "mask_read");
            aoe_stats_derive(&vl, ksp, "out_qc_announce", "qc_announce");
            aoe_stats_derive(&vl, ksp, "out_qc_forceset", "qc_forceset");
            aoe_stats_derive(&vl, ksp, "out_qc_read", "qc_read");
            aoe_stats_derive(&vl, ksp, "out_qc_set", "qc_set");
            aoe_stats_derive(&vl, ksp, "out_qc_test", "qc_test");
            aoe_stats_derive(&vl, ksp, "out_qc_testprefix", "qc_testprefix");
            aoe_stats_derive(&vl, ksp, "out_qc_testreplace", "qc_testreplace");
            aoe_stats_derive(&vl, ksp, "out_resrel_forceset", "resrel_forceset");
            aoe_stats_derive(&vl, ksp, "out_resrel_read", "resrel_read");
            aoe_stats_derive(&vl, ksp, "out_resrel_set", "resrel_set");
        }
        snprintf(t, sizeof (t), "aoet_tgt_io_%s", nvpair_name(alias));
        get_kstat(&ksp, "aoet", (int)l, t);
        if (ksp != NULL) {
            sstrncpy(vl.plugin, "AoE-Target-IO-In", sizeof (vl.plugin));
            sstrncpy(vl.plugin_instance, a, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));
            aoe_stats_derive(&vl, ksp, "in_bytes", "bytes");            
            aoe_stats_derive(&vl, ksp, "in_delivered", "delivered");
            aoe_stats_derive(&vl, ksp, "in_dropped_badarg", "dropped_badarg");
            aoe_stats_derive(&vl, ksp, "in_dropped_badcmd", "dropped_badcmd");
            aoe_stats_derive(&vl, ksp, "in_dropped_badflags", "dropped_badflags");
            aoe_stats_derive(&vl, ksp, "in_dropped_badsender", "dropped_badsender");
            aoe_stats_derive(&vl, ksp, "in_dropped_badver", "dropped_badver");
            aoe_stats_derive(&vl, ksp, "in_dropped_notask", "dropped_notask");
            aoe_stats_derive(&vl, ksp, "in_dropped_toolong", "dropped_toolong");
            aoe_stats_derive(&vl, ksp, "in_dropped_tooshort", "dropped_tooshort");
            aoe_stats_derive(&vl, ksp, "in_extcmd", "extcmd");
            aoe_stats_derive(&vl, ksp, "in_packets", "packets");
            aoe_stats_derive(&vl, ksp, "in_task_copied", "task_copied");

            sstrncpy(vl.plugin, "AoE-Target-IO-Out", sizeof (vl.plugin));
            sstrncpy(vl.plugin_instance, a, sizeof (vl.plugin_instance));
            aoe_stats_derive(&vl, ksp, "out_bytes", "bytes");
            aoe_stats_derive(&vl, ksp, "out_dropped_nomem", "dropped_nomem");
            aoe_stats_derive(&vl, ksp, "out_err_arginval", "err_arginval");
            aoe_stats_derive(&vl, ksp, "out_err_cfgset", "err_cfgset");
            aoe_stats_derive(&vl, ksp, "out_err_cmdunknown", "err_cmdunknown");
            aoe_stats_derive(&vl, ksp, "out_err_devunavail", "err_devunavail");
            aoe_stats_derive(&vl, ksp, "out_err_tgtreserved", "err_tgtreserved");
            aoe_stats_derive(&vl, ksp, "out_err_vernotsupp", "err_vernotsupp");
            aoe_stats_derive(&vl, ksp, "out_frame_norecycled", "frame_norecycled");
            aoe_stats_derive(&vl, ksp, "out_frame_recycled", "frame_recycled");
            aoe_stats_derive(&vl, ksp, "out_packets", "packets");
       }
    }

    /* clean up */
    for (alias = nvlist_next_nvpair(aliases, NULL); alias != NULL;
        alias = nvlist_next_nvpair(aliases, alias)) {
        if (nvpair_type(alias) == DATA_TYPE_NVLIST)
            if (nvpair_value_nvlist(alias, &aliases_child) == 0)
              nvlist_free(aliases_child);
    }
    nvlist_free(aliases);
    return (0);
}
/*
 * atmf kstats are IO type and require different handling than NAMED
 */
#define DISPATCH_IO(valuename, stringname) \
    v[0].derive = (derive_t)ksio->valuename; \
    sstrncpy(vl.type_instance, stringname, sizeof(vl.type_instance)); \
    plugin_dispatch_values(&vl);

#define ATMF_TYPE_TARGET 0
#define ATMF_TYPE_LU 1

static int
aoe_atmf_stats_read(int atmf_type)
{    
    nvlist_t *aliases;   /* list for mapping ids to names (aliases) */
    nvlist_t *aliases_child;
    nvpair_t *alias;
    kstat_t *ksp = NULL;
    kstat_io_t *ksio;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    value_t v[1];
    char *s;
    int64_t l;
    char t[DATA_MAX_NAME_LEN];
    char *a;
    char *atmf_type_s = "target-alias";
    char *atmf_type_desc = "AoE-Target-IO";
    char *atmf_type_ks = "tgt";

    if (atmf_type == ATMF_TYPE_LU) {
        atmf_type_s = "lun-alias";
        atmf_type_desc = "AoE-LU-IO";
        atmf_type_ks = "lu";        
    }

    /* 
     * In the AoE target, each target's instance can associated with it's
     * targe-alias. Collect these into an nvlist for cross-referencing.
     *   target-alias = instance
     */
    if (nvlist_alloc(&aliases, NV_UNIQUE_NAME, 0)) {
        ERROR("nvlist allocation failed");
        return(-1);
    }

    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_module, "atmf", KSTAT_STRLEN) == 0) &&
           (strncmp(ksp->ks_class, "misc", KSTAT_STRLEN) == 0)) {
            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
            if ((s = get_kstat_string(ksp, atmf_type_s)) == NULL)
                continue;
            if (nvlist_alloc(&aliases_child, NV_UNIQUE_NAME, 0)) 
                continue;
            if (nvlist_add_string(aliases_child, "alias", s) ||
                nvlist_add_int64(aliases_child, "instance", 
                    (int64_t)ksp->ks_instance)) {
                ERROR("couldn't add to nvpair");
                return (-1);
            }
            if (nvlist_add_nvlist(aliases, aoe_get_addr(ksp->ks_name), 
                aliases_child)) {
                ERROR("couldn't add child nvlist to aliases");
                continue;
            }
        }
    }

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, atmf_type_desc, sizeof (vl.plugin));

    /* cycle through the targets and locate their various kstats */
    for (alias = nvlist_next_nvpair(aliases, NULL); alias != NULL;
        alias = nvlist_next_nvpair(aliases, alias)) {
        if (nvpair_type(alias) != DATA_TYPE_NVLIST) {
            ERROR("expected DATA_TYPE_NVLIST, got %d for %s\n", 
                nvpair_type(alias), nvpair_name(alias));
            continue;
        }
        if (nvpair_value_nvlist(alias, &aliases_child) != 0) {
            ERROR("unable to get child nvlist for %s\n", nvpair_name(alias));
            continue;
        }
        if (nvlist_lookup_int64(aliases_child, "instance", &l)) {
            ERROR("unable to get instance for %s\n", nvpair_name(alias));
            continue;
        } 
        if (nvlist_lookup_string(aliases_child, "alias", &a)) {
            ERROR("unable to get alias for %s\n", nvpair_name(alias));
            continue;
        }
        snprintf(t, sizeof (t), "atmf_%s_io_%s", atmf_type_ks, 
            nvpair_name(alias));
        get_kstat(&ksp, "atmf", (int)l, t);
        if (ksp != NULL) {
            if (ksp->ks_type != KSTAT_TYPE_IO) {
                ERROR("aoe_stats_io: ksp->ks_type not KSTAT_TYPE_IO");
                return (0);
            }

            ksio = KSTAT_IO_PTR(ksp);

            vl.values = v;
            vl.values_len = 1;
            sstrncpy(vl.plugin_instance, a, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));

            DISPATCH_IO(nread, "nread");
            DISPATCH_IO(reads, "reads");
            DISPATCH_IO(nwritten, "nwritten");
            DISPATCH_IO(writes, "writes");
            DISPATCH_IO(wtime, "wtime");
            DISPATCH_IO(wlentime, "wlentime");
            DISPATCH_IO(wlastupdate, "wlastupdate");
            DISPATCH_IO(wcnt, "wcnt");
            DISPATCH_IO(rtime, "rtime");
            DISPATCH_IO(rlentime, "rlentime");
            DISPATCH_IO(rlastupdate, "rlastupdate");
            DISPATCH_IO(rcnt, "rcnt");
       }
    }

    /* clean up */
    for (alias = nvlist_next_nvpair(aliases, NULL); alias != NULL;
        alias = nvlist_next_nvpair(aliases, alias)) {
        if (nvpair_type(alias) == DATA_TYPE_NVLIST)
            if (nvpair_value_nvlist(alias, &aliases_child) == 0)
              nvlist_free(aliases_child);
    }
    nvlist_free(aliases);
    return (0);
}

static int
aoe_port_stats_read(void)
{    
    nvlist_t *aliases;   /* list for mapping ids to names (aliases) */
    nvlist_t *aliases_child;
    nvpair_t *alias;
    kstat_t *ksp = NULL;
    kid_t kid;
    value_list_t vl = VALUE_LIST_INIT;
    char *s;
    int64_t l;
    char t[KSTAT_STRLEN];
    char *a;

    /* 
     * In the AoE target, each target's instance can associated with it's
     * targe-alias. Collect these into an nvlist for cross-referencing.
     *   target-alias = instance
     */
    if(nvlist_alloc(&aliases, NV_UNIQUE_NAME, 0)) {
        ERROR("nvlist allocation failed");
        return(-1);
    }
    for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
        if ((strncmp(ksp->ks_module, "aoe", KSTAT_STRLEN) == 0) &&
           (strncmp(ksp->ks_class, "misc", KSTAT_STRLEN) == 0)) {
            if ((kid = kstat_read(kc, ksp, NULL)) == -1)
                continue;
            if ((s = get_kstat_string(ksp, "port-alias")) == NULL)
                continue;
            if (nvlist_alloc(&aliases_child, NV_UNIQUE_NAME, 0)) 
                continue;
            if (nvlist_add_string(aliases_child, "alias", s) ||
                nvlist_add_int64(aliases_child, "instance", (int64_t)ksp->ks_instance)) {
                ERROR("couldn't add to nvpair");
                return (-1);
            }
            if (nvlist_add_nvlist(aliases, aoe_get_addr(ksp->ks_name), aliases_child)) {
                ERROR("couldn't add child nvlist to aliases");
                continue;
            }
        }
    }

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "AoE-Port-MAC", sizeof (vl.plugin));

    /* cycle through the targets and locate their various kstats */
    for (alias = nvlist_next_nvpair(aliases, NULL); alias != NULL;
        alias = nvlist_next_nvpair(aliases, alias)) {
        if (nvpair_type(alias) != DATA_TYPE_NVLIST) {
            ERROR("expected DATA_TYPE_NVLIST, got %d for %s\n", nvpair_type(alias), nvpair_name(alias));
            continue;
        }
        if (nvpair_value_nvlist(alias, &aliases_child) != 0) {
            ERROR("unable to get child nvlist for %s\n", nvpair_name(alias));
            continue;
        }
        if (nvlist_lookup_int64(aliases_child, "instance", &l)) {
            ERROR("unable to get instance for %s\n", nvpair_name(alias));
            continue;
        } 
        if (nvlist_lookup_string(aliases_child, "alias", &a)) {
            ERROR("unable to get alias for %s\n", nvpair_name(alias));
            continue;
        }
        snprintf(t, sizeof (t), "aoe_port_mac_%s", nvpair_name(alias));
        get_kstat(&ksp, "aoe", (int)l, t);
        if (ksp != NULL) {
            sstrncpy(vl.plugin_instance, a, sizeof (vl.plugin_instance));
            sstrncpy(vl.type, "derive", sizeof (vl.type));
            aoe_stats_derive(&vl, ksp, "delivered", NULL);
            aoe_stats_derive(&vl, ksp, "dropped_nomem", NULL);
            aoe_stats_derive(&vl, ksp, "dropped_other", NULL);
            aoe_stats_derive(&vl, ksp, "dropped_runt", NULL);
            aoe_stats_derive(&vl, ksp, "dropped_tooshort", NULL);
            aoe_stats_derive(&vl, ksp, "pullups", NULL);
       }
    }

    /* clean up */
    for (alias = nvlist_next_nvpair(aliases, NULL); alias != NULL;
        alias = nvlist_next_nvpair(aliases, alias)) {
        if (nvpair_type(alias) == DATA_TYPE_NVLIST)
            if (nvpair_value_nvlist(alias, &aliases_child) == 0)
              nvlist_free(aliases_child);
    }
    nvlist_free(aliases);
    return (0);
}

/*
 * EtherDrive initiator (ethdrv) stats are read from pseudo files:
 *   /dev/ethdrv/ca
 *   /dev/ethdrv/acbs
 *
 * If these files don't exist or don't contain anything, they are ignored.
 */
#define DISPATCH_ETHDRV_GAUGE(name) \
    if ((t = strtok_r(NULL, " ", &st)) == NULL) continue; \
    v[0].gauge = (gauge_t)strtoll(t, NULL, 0); \
    sstrncpy(vl.type_instance, name, sizeof(vl.type_instance)); \
    plugin_dispatch_values(&vl);
#define DISPATCH_ETHDRV_DERIVE(name) \
    if ((t = strtok_r(NULL, " ", &st)) == NULL) continue; \
    v[0].derive = (derive_t)strtoll(t, NULL, 0); \
    sstrncpy(vl.type_instance, name, sizeof(vl.type_instance)); \
    plugin_dispatch_values(&vl);
#define DISPATCH_ETHDRV_MS(name) \
    if ((t = strtok_r(NULL, " ", &st)) == NULL) continue; \
    v[0].gauge = (gauge_t)(long long)(strtod(t, (char **)NULL) * 1e6); \
    sstrncpy(vl.type_instance, name, sizeof(vl.type_instance)); \
    plugin_dispatch_values(&vl);

static int
aoe_ethdrv_stats_read(void)
{    
    value_list_t vl = VALUE_LIST_INIT;
    value_t v[1];
    FILE *fp;
    char *s, *st, *t, *tt;
    char *poolnum, *volnum;
    char poolvol[DATA_MAX_NAME_LEN];
    size_t cap = 0;

    if ((fp = fopen("/dev/ethdrv/ca", "r")) == NULL)
        return (0);

    vl.values = v;
    vl.values_len = 1;
    sstrncpy(vl.type, "gauge", sizeof (vl.type));

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "AoE-Ethdrv", sizeof (vl.plugin));
    s = NULL;
    while (getline(&s, &cap, fp) > 0) {
        // skip first
        if ((t = strtok_r(s, " ", &st)) == NULL) continue; 
        // old notation: shelf.slot, new notation: pool.vol
        if ((t = strtok_r(NULL, " ", &st)) == NULL) continue;
        if ((poolnum = strtok_r(t, ".", &tt)) == NULL) continue;
        if ((volnum = strtok_r(NULL, ".", &tt)) == NULL) continue;
        snprintf(poolvol, sizeof (poolvol), "pool-%s-vol-%s", poolnum,
            volnum);
        sstrncpy(vl.plugin_instance, poolvol, sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "gauge", sizeof (vl.type));
        DISPATCH_ETHDRV_GAUGE("cwrk");
        DISPATCH_ETHDRV_GAUGE("clamp");
        DISPATCH_ETHDRV_GAUGE("mxwn");
        DISPATCH_ETHDRV_GAUGE("ssthresh");
        DISPATCH_ETHDRV_MS("rttavg");
        DISPATCH_ETHDRV_MS("rttdelt");
        free (s);
        s = NULL;
    }
    fclose(fp);

    if ((fp = fopen("/dev/ethdrv/acbs", "r")) == NULL)
        return (0);
    s = NULL;
    while (getline(&s, &cap, fp) > 0) {
        // skip first two entries
        if ((t = strtok_r(s, " ", &st)) == NULL) continue;
        if ((t = strtok_r(NULL, " ", &st)) == NULL) continue;

        // old notation: shelf.slot, new notation: pool.vol
        if ((t = strtok_r(NULL, " ", &st)) == NULL) continue;
        if ((poolnum = strtok_r(t, ".", &tt)) == NULL) continue;
        if ((volnum = strtok_r(NULL, ".", &tt)) == NULL) continue;
        snprintf(poolvol, sizeof (poolvol), "pool-%s-vol-%s", poolnum,
            volnum);
        sstrncpy(vl.plugin_instance, poolvol, sizeof (vl.plugin_instance));
        // skip next two entries, they are redundant with above ca file
        if ((t = strtok_r(NULL, " ", &st)) == NULL) continue;
        if ((t = strtok_r(NULL, " ", &st)) == NULL) continue;
        sstrncpy(vl.type, "gauge", sizeof (vl.type));
        DISPATCH_ETHDRV_GAUGE("cscsi");
        DISPATCH_ETHDRV_GAUGE("caoe");
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        DISPATCH_ETHDRV_DERIVE("cmds");
        DISPATCH_ETHDRV_DERIVE("rtx");
        DISPATCH_ETHDRV_DERIVE("unre");
        free (s);
        s = NULL;
    }
    fclose(fp);

    return (0);
}

static int
aoe_stats_read(void)
{
    int ret = 0;
    if ((ret = aoe_aoet_stats_read()) != 0)
        return (ret);
    if ((ret = aoe_atmf_stats_read(ATMF_TYPE_TARGET)) != 0)
        return (ret);
    if ((ret = aoe_atmf_stats_read(ATMF_TYPE_LU)) != 0)
        return (ret);
    if ((ret = aoe_port_stats_read()) != 0)
        return (ret);
    if ((ret = aoe_ethdrv_stats_read()) != 0)
        return (ret);
    return (ret);
}

static int
aoe_stats_init(void)
{
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("aoe_stats plugin: kstat chain control initialization failed");
        return (-1);
    }
    return (0);
}

void
module_register(void)
{
    plugin_register_init ("aoe_stats", aoe_stats_init);
    plugin_register_read ("aoe_stats", aoe_stats_read);
}
