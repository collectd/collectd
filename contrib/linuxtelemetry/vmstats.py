#!/usr/bin/python

##########################################################
# vmstats.py
#
# Copyright (c) 2015, Salesforce.com, Inc.
# All rights reserved.
#
# Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#
# Neither the name of Salesforce.com nor the names of its
# contributors may be used to endorse or promote products
# derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
# NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################

##########################################################
# Plugin description:
#
# This plugin gathers raw measurements from /proc/vmstat.
# Some of the metrics that we are intrested in are those
# in the output of 'sar -B' that reports  paging statistics. 
# Some of the metrics below are available only with post 2.5+
# kernels. The following values are available:
#
#  pgpgin/s
#           Total number of kilobytes the system paged in from
#           disk  per  second.   Note: With  old kernels (2.2.x)
#           this value is a number of blocks per second (and not
#           kilobytes).
#
#  pgpgout/s
#           Total number of kilobytes the system paged out to 
#           disk per second.  Note: With old kernels (2.2.x) this 
#           value is a number of blocks per second (and not kilo-
#           bytes).
#
#  fault/s
#           Number of page faults (major + minor) made by the system 
#           per second.  This  is not  a count of page faults that 
#           generate I/O, because some page faults can be resolved 
#           without I/O.
#
#  majflt/s
#           Number of major faults the system  has  made  per  second, 
#           those  which  have required loading a memory page from disk.
#
#  pgfree/s
#           Number of pages placed on the free list by the system per second.
#
#  pgscank/s
#           Number of pages scanned by the kswapd daemon per second.
#           Kernel theread kswapd provides asynchronous page reclamation
#           and runs only when free pages in a zone runs low.
#
#  pgscand/s
#           Number of pages scanned directly per second.
#           Direct reclaimation occurrs periodically.
#
#  pgsteal/s
#           Number  of pages the system has reclaimed from cache (pagecache
#           and swapcache) per second to satisfy its memory demands.
#
#  %vmeff
#           Calculated as pgsteal / pgscan, this is a metric of  the  
#           efficiency  of  page reclaim.  If it is near 100% then almost 
#           every page coming off the tail of the inactive list is being 
#           reaped. If it gets too low (e.g. less  than  30%)  then the 
#           virtual memory is having some difficulty.  This field is displayed 
#           as zero if no pages have been scanned during the interval of time.
#
#
# This plugin is based on /proc/vmstat output (varies with kernel version as
# well as various flavors of Linux systems):
#
# 1.  nr_free_pages 7057051
# 2.  nr_inactive_anon 86
# 3.  nr_active_anon 12702
# 4.  nr_inactive_file 2210411
# 5.  nr_active_file 2323658
# 6.  nr_unevictable 8
# 7.  nr_mlock 8
# 8.  nr_anon_pages 12116
# 9.  nr_mapped 5168
# 10. nr_file_pages 4534236
# 11. nr_dirty 36
# 12. nr_writeback 0
# 13. nr_slab_reclaimable 359519
# 14. nr_slab_unreclaimable 268554
# 15. nr_page_table_pages 3700
# 16. nr_kernel_stack 522
# 17. nr_unstable 0
# 18. nr_bounce 0
# 19. nr_vmscan_write 0
# 20. nr_writeback_temp 0
# 21. nr_isolated_anon 0
# 22. nr_isolated_file 0
# 23. nr_shmem 168
# 24. numa_hit 7521020255
# 25. numa_miss 1400949106
# 26. numa_foreign 1400949106
# 27. numa_interleave 38352
# 28. numa_local 7520997143
# 29. numa_other 1400972218
# 30. nr_anon_transparent_hugepages 1
# 31. pgpgin 203525573
# 32. pgpgout 619347783
# 33. pswpin 0
# 34. pswpout 0
# 35. pgalloc_dma 1
# 36. pgalloc_dma32 716304477
# 37. pgalloc_normal 8207012876
# 38. pgalloc_movable 0
# 39. pgfree 8930377568
# 40. pgactivate 8496932
# 41. pgdeactivate 0
# 42. pgfault 418769943
# 43. pgmajfault 2090
# 44. pgrefill_dma 0
# 45. pgrefill_dma32 0
# 46. pgrefill_normal 0
# 47. pgrefill_movable 0
# 48. pgsteal_dma 0
# 49. pgsteal_dma32 0
# 50. pgsteal_normal 0
# 51. pgsteal_movable 0
# 52. pgscan_kswapd_dma 0
# 53. pgscan_kswapd_dma32 0
# 54. pgscan_kswapd_normal 0
# 55. pgscan_kswapd_movable 0
# 56. pgscan_direct_dma 0
# 57. pgscan_direct_dma32 0
# 58. pgscan_direct_normal 0
# 59. pgscan_direct_movable 0
# 60. zone_reclaim_failed 0
# 61. pginodesteal 0
# 62. slabs_scanned 0
# 63. kswapd_steal 0
# 64. kswapd_inodesteal 0
# 65. kswapd_low_wmark_hit_quickly 0
# 66. kswapd_high_wmark_hit_quickly 0
# 67. kswapd_skip_congestion_wait 0
# 68. pageoutrun 0
# 69. allocstall 0
# 70. pgrotated 0
# 71. compact_blocks_moved 0
# 72. compact_pages_moved 0
# 73. compact_pagemigrate_failed 0
# 74. compact_stall 0
# 75. compact_fail 0
# 76. compact_success 0
# 77. htlb_buddy_alloc_success 0
# 78. htlb_buddy_alloc_fail 0
# 79. unevictable_pgs_culled 14239
# 80. unevictable_pgs_scanned 0
# 81. unevictable_pgs_rescued 60163177
# 82. unevictable_pgs_mlocked 60164843
# 83. unevictable_pgs_munlocked 60164803
# 84. unevictable_pgs_cleared 32
# 85. unevictable_pgs_stranded 0
# 86. unevictable_pgs_mlockfreed 0
# 
##########################################################

import collectd
import platform
import os
import socket
import time
import re

os_name = platform.system()
host_name = socket.gethostbyaddr(socket.gethostname())[0]
host_types = ['app', 'db', 'ffx', 'indexer', 'search', 'other']
host_type = 'other'

vms_fname = '/proc/vmstat'
vmstat_fields = ['nr_free_pages', 
                 'nr_inactive_anon',
                 'nr_active_anon',
                 'nr_inactive_file',
                 'nr_active_file',
                 'nr_unevictable',
                 'nr_mlock',
                 'nr_anon_pages',
                 'nr_mapped',
                 'nr_file_pages',
                 'nr_dirty',
                 'nr_writeback',
                 'nr_slab_reclaimable',
                 'nr_slab_unreclaimable',
                 'nr_page_table_pages',
                 'nr_kernel_stack',
                 'nr_unstable',
                 'nr_bounce',
                 'nr_vmscan_write',
                 'nr_writeback_temp',
                 'nr_isolated_anon',
                 'nr_isolated_file',
                 'nr_shmem',
                 'numa_hit',
                 'numa_miss',
                 'numa_foreign',
                 'numa_interleave',
                 'numa_local',
                 'numa_other',
                 'nr_anon_transparent_hugepages',
                 'pgpgin',
                 'pgpgout',
                 'pswpin',
                 'pswpout',
                 'pgalloc_dma',
                 'pgalloc_dma32',
                 'pgalloc_normal',
                 'pgalloc_movable',
                 'pgfree',
                 'pgactivate',
                 'pgdeactivate',
                 'pgfault',
                 'pgmajfault',
                 'pgrefill_dma',
                 'pgrefill_dma32',
                 'pgrefill_normal',
                 'pgrefill_movable',
                 'pgsteal_dma',
                 'pgsteal_dma32',
                 'pgsteal_normal',
                 'pgsteal_movable',
                 'pgscan_kswapd_dma',
                 'pgscan_kswapd_dma32',
                 'pgscan_kswapd_normal',
                 'pgscan_kswapd_movable',
                 'pgscan_direct_dma',
                 'pgscan_direct_dma32',
                 'pgscan_direct_normal',
                 'pgscan_direct_movable',
                 'zone_reclaim_failed',
                 'pginodesteal',
                 'slabs_scanned',
                 'kswapd_steal',
                 'kswapd_inodesteal',
                 'kswapd_low_wmark_hit_quickly',
                 'kswapd_high_wmark_hit_quickly',
                 'kswapd_skip_congestion_wait',
                 'pageoutrun',
                 'allocstall',
                 'pgrotated',
                 'compact_blocks_moved',
                 'compact_pages_moved',
                 'compact_pagemigrate_failed',
                 'compact_stall',
                 'compact_fail',
                 'compact_success',
                 'htlb_buddy_alloc_success',
                 'htlb_buddy_alloc_fail',
                 'unevictable_pgs_culled',
                 'unevictable_pgs_scanned',
                 'unevictable_pgs_rescued',
                 'unevictable_pgs_mlocked',
                 'unevictable_pgs_munlocked',
                 'unevictable_pgs_cleared',
                 'unevictable_pgs_stranded',
                 'unevictable_pgs_mlockfreed',
                 'thp_fault_alloc',
                 'thp_fault_fallback',
                 'thp_collapse_alloc',
                 'thp_collapse_alloc_failed',
                 'thp_split'
                 ]
vmstat_metrics = ['pgpgin_per_sec',
                  'pgpgout_per_sec',
                  'pswpin_per_sec',
                  'pswpout_per_sec',
                  'faults_per_sec',
                  'majflts_per_sec',
                  'pgfree_per_sec',
                  'pgscank_per_sec',
                  'pgscand_per_sec',
                  'pgsteal_per_sec',
                  'pct_vmeff']

white_list = ['pgpgin',
              'pgpgout',
              'pswpin',
              'pswpout',
              'pgalloc_dma',
              'pgalloc_dma32',
              'pgalloc_normal',
              'pgfault',
              'pgmajfault',
              'nr_free_pages',
              'pgscan_kswapd_dma',
              'pgscan_kswapd_dma32',
              'pgscan_kswapd_normal',
              'pgscan_direct_dma',
              'pgscan_direct_dma32',
              'pgscan_direct_normal',
              'pgsteal_dma',
              'pgsteal_dma32',
              'pgsteal_normal',
              'zone_reclaim_failed',
              'slabs_scanned',
              'kswapd_steal',
              'kswapd_inodesteal',
              'kswapd_low_wmark_hit_quickly',
              'kswapd_high_wmark_hit_quickly',
              'kswapd_skip_congestion_wait',
              'pageoutrun',
              'allocstall',
              'pgrotated',
              'compact_blocks_moved',
              'compact_pages_moved',
              'compact_pagemigrate_failed',
              'compact_stall',
              'compact_fail',
              'compact_success',
              'htlb_buddy_alloc_success',
              'htlb_buddy_alloc_fail',
              'nr_free_pages',
              'nr_inactive_anon',
              'nr_active_anon',
              'nr_inactive_file',
              'nr_active_file',
              'nr_unevictable',
              'nr_mlock',
              'nr_anon_pages',
              'nr_mapped',
              'nr_file_pages',
              'nr_dirty',
              'nr_writeback',
              'nr_shmem',
              'numa_hit',
              'numa_miss',
              'numa_foreign',
              'numa_interleave',
              'numa_local',
              'numa_other']
stats_cache = {}
stats_current = {}

def get_host_type():
   for i in host_types:
      if i in host_name:
         host_type = i

def init_stats_cache():
   global white_list
   tmp_white_list = []

   if os.path.exists(vms_fname):
      with open(vms_fname) as f:
         for line in f:
            fields = line.split()
            fields = [fl.strip() for fl in fields]
            key_name = fields[0]
            key_val = int(fields[1])
            if any(s in key_name for s in white_list):
               stats_cache[(key_name, 'val')] = key_val
               stats_cache[(key_name, 'ts')] = time.time()
               tmp_white_list.append(key_name)
      f.close()
      white_list = tmp_white_list
   else:
      collectd.info('vmstats: init_stats_cache: path: %s does not exist' 
                    % (vms_fname))

def collect_vmstats():
    if os.path.exists(vms_fname):
        with open(vms_fname) as f:
            for line in f:
                fields = line.split()
                fields = [fl.strip() for fl in fields]
                key_name = fields[0]
                key_val = fields[1]
                if any(key_name in s for s in white_list):
                    metric = collectd.Values()
                    metric.host = host_name
                    metric.plugin = 'vmstats'
                    metric.type = 'gauge'
                    metric.type_instance = key_name
                    metric.values = [fields[1]]
                    stats_current[(key_name, 'val')] = key_val
                    metric.dispatch()
                    stats_current[(key_name, 'ts')] = time.time()
            f.close()
    else:
        collectd.info('vmstats: procfs path: %s does not exist' % (vms_fname))

def swap_current_cache():
   for i in white_list:
       stats_cache[(i, 'val')] = stats_current[(i, 'val')]
       stats_cache[(i, 'ts')] = stats_current[(i, 'ts')]

def calc_vmstats_rate(m):
    cur_t = float(stats_current[(m, 'ts')])
    pre_t = float(stats_cache[(m, 'ts')])
    time_delta = cur_t - pre_t
    if (time_delta <= 0.0):
        return None

    cur_val = int(stats_current[(m, 'val')])
    pre_val = int(stats_cache[(m, 'val')])
    return (cur_val - pre_val)/time_delta if (cur_val >= pre_val) else None

def calc_vmstats():
    vm_rate = {}
    for i in white_list:
        vm_rate[(i)] = calc_vmstats_rate(i)

    # sort out and adjust final metrics values as needed
    pgpgin_ps = vm_rate['pgpgin']
    pgpgout_ps = vm_rate['pgpgout']
    pswpin_ps = vm_rate['pswpin']
    pswpout_ps = vm_rate['pswpout']
    faults_ps = vm_rate['pgfault']
    mjflts_ps = vm_rate['pgmajfault']
    pgfree_ps = vm_rate['nr_free_pages']

    pgscank_ps = vm_rate['pgscan_kswapd_dma']
    pgscank_ps += vm_rate['pgscan_kswapd_dma32']
    pgscank_ps += vm_rate['pgscan_kswapd_normal']

    pgscand_ps = vm_rate['pgscan_direct_dma']
    pgscand_ps += vm_rate['pgscan_direct_dma32']
    pgscand_ps += vm_rate['pgscan_direct_normal']

    pgsteal_ps = vm_rate['pgsteal_dma']
    pgsteal_ps += vm_rate['pgsteal_dma32']
    pgsteal_ps += vm_rate['pgsteal_normal']

    pgscan_ps = pgscank_ps + pgscand_ps
    vmeff = pgsteal_ps/pgscan_ps if (pgscan_ps > 0.0) else 0.0
    pct_vmeff = vmeff*100.0 if (vmeff < 1.0) else 100.0

    # return as key-value pairs dict
    m = [pgpgin_ps, pgpgout_ps, pswpin_ps, pswpout_ps,
         faults_ps, mjflts_ps, pgfree_ps,
         pgscank_ps, pgscand_ps, pgsteal_ps, pct_vmeff]
    vmst = dict(zip(vmstat_metrics, m))
    return vmst

def dispatch_metrics():
   metric = collectd.Values()
   metric.host = host_name
   metric.plugin = 'vmstats'
   metric.type = 'gauge'

   key_vals = {}
   key_vals = calc_vmstats()
   for i in vmstat_metrics:
      if key_vals[i] is not None:
         metric.type_instance = i
         metric.values = [key_vals[i]]
         metric.dispatch()


def configer(ObjConfiguration):
   collectd.info('vmstats plugin: configuring host: %s' % (host_name)) 

def initer():
   get_host_type()
   collectd.info('vmstats plugin: host of type: %s' % (host_type))
   collectd.info('vmstats initer: white list: %s ' % (white_list))
   init_stats_cache()
   collectd.info('vmstats init: stats_cache: %s ' % (stats_cache))

def reader(input_data=None):
   collect_vmstats()
   dispatch_metrics()
   swap_current_cache()

def writer(metric, data=None):
   for i in metric.values:
      collectd.debug("%s (%s): %f" % (metric.plugin, metric.type, i))

def shutdown():
   collectd.info("vmstats plugin shutting down")

#== Callbacks ==#
if (os_name == 'Linux'):
   collectd.register_config(configer)
   collectd.register_init(initer)
   collectd.register_read(reader)
   collectd.register_write(writer)
   collectd.register_shutdown(shutdown)
else:
   collectd.warning('vmstats plugin currently works for Linux only')

