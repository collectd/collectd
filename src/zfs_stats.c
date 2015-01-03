/*
 * Coraid ZFS collectd data collector for ZFS global statistics.
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
/*
 * Many of the kstat counters for ARC stats are not gauges.
 * For those that are, we pass as gauges. The rest are passed as derive.
 * We also need to translate the most obscure kstat names into something
 * a human might recognize. To do this, accept an override to the kstat
 * statistic.
 */

/* pass the counters as collectd derive (int64_t) */
void
zfs_stats_derive(value_list_t *vl, kstat_t *ksp, char *k, char *s)
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
zfs_stats_gauge(value_list_t *vl, kstat_t *ksp, char *k, char *s)
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
 * Most of the work is done in the zfs_stats_read() callback.
 * For brevity, a simplistic approach is taken to match a reasonable
 * collectd and whisper-compatible namespace. The general form is:
 *   ZFS-<subset>.[gauge|derive]-statistic
 */
static int
zfs_stats_read(void)
{
    kstat_t *ksp = NULL;
    value_list_t vl = VALUE_LIST_INIT;

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "ZFS", sizeof (vl.plugin));

    get_kstat(&ksp, "zfs", 0, "arcstats");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "ARC", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "gauge", sizeof (vl.type));
        zfs_stats_gauge(&vl, ksp, "arc_meta_limit", NULL);
        zfs_stats_gauge(&vl, ksp, "arc_meta_max", NULL);
        zfs_stats_gauge(&vl, ksp, "arc_meta_used", NULL);
        zfs_stats_gauge(&vl, ksp, "arc_no_grow", NULL);
        zfs_stats_gauge(&vl, ksp, "buf_size", NULL);
        zfs_stats_gauge(&vl, ksp, "c", "target_max");
        zfs_stats_gauge(&vl, ksp, "c_max", "arc_max");
        zfs_stats_gauge(&vl, ksp, "c_min", "arc_min");
        zfs_stats_gauge(&vl, ksp, "data_size", NULL);
        zfs_stats_gauge(&vl, ksp, "duplicate_buffers", NULL);
        zfs_stats_gauge(&vl, ksp, "duplicate_buffers_size", NULL);
        zfs_stats_gauge(&vl, ksp, "hash_chain_max", NULL);
        zfs_stats_gauge(&vl, ksp, "hash_elements_max", NULL);
        zfs_stats_gauge(&vl, ksp, "hdr_size", NULL);
        zfs_stats_gauge(&vl, ksp, "l2_asize", NULL);
        zfs_stats_gauge(&vl, ksp, "l2_hdr_size", NULL);
        zfs_stats_gauge(&vl, ksp, "l2_size", NULL);
        zfs_stats_gauge(&vl, ksp, "meta_limit", NULL);
        zfs_stats_gauge(&vl, ksp, "meta_max", NULL);
        zfs_stats_gauge(&vl, ksp, "meta_used", NULL);
        zfs_stats_gauge(&vl, ksp, "other_size", NULL);
        zfs_stats_gauge(&vl, ksp, "p", "mru_target_size");
        zfs_stats_gauge(&vl, ksp, "size", "arc_size");

        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "deleted", NULL);
        zfs_stats_derive(&vl, ksp, "demand_data_hits", NULL);
        zfs_stats_derive(&vl, ksp, "demand_data_misses", NULL);
        zfs_stats_derive(&vl, ksp, "demand_metadata_hits", NULL);
        zfs_stats_derive(&vl, ksp, "demand_metadata_misses", NULL);
        zfs_stats_derive(&vl, ksp, "duplicate_reads", NULL);
        zfs_stats_derive(&vl, ksp, "evict_allocfail", NULL);
        zfs_stats_derive(&vl, ksp, "evict_l2_cached", NULL);
        zfs_stats_derive(&vl, ksp, "evict_l2_eligible", NULL);
        zfs_stats_derive(&vl, ksp, "evict_l2_ineligible", NULL);
        zfs_stats_derive(&vl, ksp, "evict_lock_drops", NULL);
        zfs_stats_derive(&vl, ksp, "evict_mfu", NULL);
        zfs_stats_derive(&vl, ksp, "evict_mru", NULL);
        zfs_stats_derive(&vl, ksp, "evict_skip", NULL);
        zfs_stats_derive(&vl, ksp, "evict_user_bufs", NULL);
        zfs_stats_derive(&vl, ksp, "hash_chains", NULL);
        zfs_stats_derive(&vl, ksp, "hash_collisions", NULL);
        zfs_stats_derive(&vl, ksp, "hash_elements", NULL);
        zfs_stats_derive(&vl, ksp, "hits", NULL);
        zfs_stats_derive(&vl, ksp, "l2_abort_lowmem", NULL);
        zfs_stats_derive(&vl, ksp, "l2_cksum_bad", NULL);
        zfs_stats_derive(&vl, ksp, "l2_compress_failures", NULL);
        zfs_stats_derive(&vl, ksp, "l2_compress_successes", NULL);
        zfs_stats_derive(&vl, ksp, "l2_compress_zeros", NULL);
        zfs_stats_derive(&vl, ksp, "l2_evict_lock_retry", NULL);
        zfs_stats_derive(&vl, ksp, "l2_evict_reading", NULL);
        zfs_stats_derive(&vl, ksp, "l2_feeds", NULL);
        zfs_stats_derive(&vl, ksp, "l2_free_on_write", NULL);
        zfs_stats_derive(&vl, ksp, "l2_hits", NULL);
        zfs_stats_derive(&vl, ksp, "l2_io_error", NULL);
        zfs_stats_derive(&vl, ksp, "l2_misses", NULL);
        zfs_stats_derive(&vl, ksp, "l2_read_bytes", NULL);
        zfs_stats_derive(&vl, ksp, "l2_rw_clash", NULL);
        zfs_stats_derive(&vl, ksp, "l2_write_bytes", NULL);
        zfs_stats_derive(&vl, ksp, "l2_writes_done", NULL);
        zfs_stats_derive(&vl, ksp, "l2_writes_error", NULL);
        zfs_stats_derive(&vl, ksp, "l2_writes_hdr_miss", NULL);
        zfs_stats_derive(&vl, ksp, "l2_writes_sent", NULL);
        zfs_stats_derive(&vl, ksp, "memory_throttle_count", NULL);
        zfs_stats_derive(&vl, ksp, "mfu_ghost_hits", NULL);
        zfs_stats_derive(&vl, ksp, "mfu_hits", NULL);
        zfs_stats_derive(&vl, ksp, "misses", NULL);
        zfs_stats_derive(&vl, ksp, "mru_ghost_hits", NULL);
        zfs_stats_derive(&vl, ksp, "mru_hits", NULL);
        zfs_stats_derive(&vl, ksp, "mutex_miss", NULL);
        zfs_stats_derive(&vl, ksp, "prefetch_data_hits", NULL);
        zfs_stats_derive(&vl, ksp, "prefetch_data_misses", NULL);
        zfs_stats_derive(&vl, ksp, "prefetch_metadata_hits", NULL);
        zfs_stats_derive(&vl, ksp, "prefetch_metadata_misses", NULL);
        zfs_stats_derive(&vl, ksp, "recycle_miss", NULL);
        zfs_stats_derive(&vl, ksp, "shrinks", NULL);
        zfs_stats_derive(&vl, ksp, "snaptime", "arcstats_snaptime");
    }

    get_kstat(&ksp, "unix", 0, "vopstats_zfs");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "VOps", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "naccess", NULL);
        zfs_stats_derive(&vl, ksp, "naddmap", NULL);
        zfs_stats_derive(&vl, ksp, "nclose", NULL);
        zfs_stats_derive(&vl, ksp, "ncmp", NULL);
        zfs_stats_derive(&vl, ksp, "ncreate", NULL);
        zfs_stats_derive(&vl, ksp, "ndelmap", NULL);
        zfs_stats_derive(&vl, ksp, "ndispose", NULL);
        zfs_stats_derive(&vl, ksp, "ndump", NULL);
        zfs_stats_derive(&vl, ksp, "ndumpctl", NULL);
        zfs_stats_derive(&vl, ksp, "nfid", NULL);
        zfs_stats_derive(&vl, ksp, "nfrlock", NULL);
        zfs_stats_derive(&vl, ksp, "nfsync", NULL);
        zfs_stats_derive(&vl, ksp, "ngetattr", NULL);
        zfs_stats_derive(&vl, ksp, "ngetpage", NULL);
        zfs_stats_derive(&vl, ksp, "ngetsecattr", NULL);
        zfs_stats_derive(&vl, ksp, "ninactive", NULL);
        zfs_stats_derive(&vl, ksp, "nioctl", NULL);
        zfs_stats_derive(&vl, ksp, "nlink", NULL);
        zfs_stats_derive(&vl, ksp, "nlookup", NULL);
        zfs_stats_derive(&vl, ksp, "nmap", NULL);
        zfs_stats_derive(&vl, ksp, "nmkdir", NULL);
        zfs_stats_derive(&vl, ksp, "nopen", NULL);
        zfs_stats_derive(&vl, ksp, "npageio", NULL);
        zfs_stats_derive(&vl, ksp, "npathconf", NULL);
        zfs_stats_derive(&vl, ksp, "npoll", NULL);
        zfs_stats_derive(&vl, ksp, "nputpage", NULL);
        zfs_stats_derive(&vl, ksp, "nread", NULL);
        zfs_stats_derive(&vl, ksp, "nreaddir", NULL);
        zfs_stats_derive(&vl, ksp, "nreadlink", NULL);
        zfs_stats_derive(&vl, ksp, "nrealvp", NULL);
        zfs_stats_derive(&vl, ksp, "nremove", NULL);
        zfs_stats_derive(&vl, ksp, "nrename", NULL);
        zfs_stats_derive(&vl, ksp, "nreqzcbuf", NULL);
        zfs_stats_derive(&vl, ksp, "nretzcbuf", NULL);
        zfs_stats_derive(&vl, ksp, "nrmdir", NULL);
        zfs_stats_derive(&vl, ksp, "nrwlock", NULL);
        zfs_stats_derive(&vl, ksp, "nrwunlock", NULL);
        zfs_stats_derive(&vl, ksp, "nseek", NULL);
        zfs_stats_derive(&vl, ksp, "nsetattr", NULL);
        zfs_stats_derive(&vl, ksp, "nsetfl", NULL);
        zfs_stats_derive(&vl, ksp, "nsetsecattr", NULL);
        zfs_stats_derive(&vl, ksp, "nshrlock", NULL);
        zfs_stats_derive(&vl, ksp, "nspace", NULL);
        zfs_stats_derive(&vl, ksp, "nsymlink", NULL);
        zfs_stats_derive(&vl, ksp, "nvnevent", NULL);
        zfs_stats_derive(&vl, ksp, "nwrite", NULL);
        zfs_stats_derive(&vl, ksp, "read_bytes", NULL);
        zfs_stats_derive(&vl, ksp, "readdir_bytes", NULL);
        zfs_stats_derive(&vl, ksp, "snaptime", NULL);
        zfs_stats_derive(&vl, ksp, "write_bytes", NULL);
    }

    get_kstat(&ksp, "zfs", 0, "vdev_cache_stats");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "vdev-cache", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "delegations", NULL);
        zfs_stats_derive(&vl, ksp, "hits", NULL);
        zfs_stats_derive(&vl, ksp, "misses", NULL);
        zfs_stats_derive(&vl, ksp, "snaptime", NULL);
    }

    get_kstat(&ksp, "zfs", 0, "xuio_stats");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "XUIO", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "onloan_read_buf", NULL);
        zfs_stats_derive(&vl, ksp, "onloan_write_buf", NULL);
        zfs_stats_derive(&vl, ksp, "read_buf_copied", NULL);
        zfs_stats_derive(&vl, ksp, "read_buf_nocopy", NULL);
        zfs_stats_derive(&vl, ksp, "snaptime", NULL);
        zfs_stats_derive(&vl, ksp, "write_buf_copied", NULL);
        zfs_stats_derive(&vl, ksp, "write_buf_nocopy", NULL);
    }

    get_kstat(&ksp, "zfs", 0, "zfetchstats");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "data-prefetch", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "bogus_streams", NULL);
        zfs_stats_derive(&vl, ksp, "colinear_hits", NULL);
        zfs_stats_derive(&vl, ksp, "colinear_misses", NULL);
        zfs_stats_derive(&vl, ksp, "hits", NULL);
        zfs_stats_derive(&vl, ksp, "misses", NULL);
        zfs_stats_derive(&vl, ksp, "reclaim_failures", NULL);
        zfs_stats_derive(&vl, ksp, "reclaim_successes", NULL);
        zfs_stats_derive(&vl, ksp, "snaptime", NULL);
        zfs_stats_derive(&vl, ksp, "streams_noresets", NULL);
        zfs_stats_derive(&vl, ksp, "streams_resets", NULL);
        zfs_stats_derive(&vl, ksp, "stride_hits", NULL);
        zfs_stats_derive(&vl, ksp, "stride_misses", NULL);
    }

    /* ARC-related kmem cache info */
    get_kstat(&ksp, "unix", 0, "arc_buf_t");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "kmem", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "buf_inuse", "arc_buf_inuse");
        zfs_stats_derive(&vl, ksp, "reap", "arc_buf_reap");
    }
    get_kstat(&ksp, "unix", 0, "kmem_alloc_32");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "kmem", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "buf_inuse", "alloc_32_buf_inuse");
    }
    get_kstat(&ksp, "unix", 0, "kmem_alloc_40");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "kmem", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "buf_inuse", "alloc_40_buf_inuse");
    }
    get_kstat(&ksp, "unix", 0, "dnode_t");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "kmem", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "reap", "arc_dnode_reap");
        zfs_stats_derive(&vl, ksp, "move_callbacks", "arc_dnode_move_callbacks");
    }
    get_kstat(&ksp, "unix", 0, "zfs_znode_cache");
    if (ksp != NULL) {
        sstrncpy(vl.plugin_instance, "kmem", sizeof (vl.plugin_instance));
        sstrncpy(vl.type, "derive", sizeof (vl.type));
        zfs_stats_derive(&vl, ksp, "reap", "arc_znode_reap");
        zfs_stats_derive(&vl, ksp, "move_callbacks", "arc_znode_move_callbacks");
    }
    return (0);
}

static int
zfs_stats_init(void)
{
    /* the kstat chain is opened already, if not bail out */
    if (kc == NULL) {
        ERROR ("zfs_stats plugin: kstat chain control initialization failed");
        return (-1);
    }
    return (0);
}

void
module_register(void)
{
    plugin_register_init ("zfs_stats", zfs_stats_init);
    plugin_register_read ("zfs_stats", zfs_stats_read);
}
