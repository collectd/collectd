#!/usr/bin/python

##########################################################
# buddyinfo.py 
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
# Linux uses buddy allocator for memory management. Pages
# are allocated in each NUMA node and zones within each
# node. Within each zone, pages are allocated as 
# contiguous groups of 1, 2, 3, 4, and so on order memory
# chunck where 1 means 4K chunks. Number of free pages in
# each bucket is exposed through /proc/buddyinfo
# When this number goes below a threshold in any bucket,
# kswapd (slowpath for finding free pages) kicks in. It
# then scans for free pages in all order levels until
# all of them reach above min limit. This process can take
# long time and may cause issues for GC latencies. 
#
# Typical contents of /proc/buddyinfo:
#
# Node 0, zone   Normal   1490   4026  12224   8508   4493   1929    849    301    101     45   5257 
# Node 1, zone      DMA      1      1      1      1      1      0      1      0      1      1      3 
# Node 1, zone    DMA32     15      3      2      5      8      7      4      4      7      8    681 
# Node 1, zone   Normal   6061  13681  20887  15188   9097   4546   1948    731    273    125   3976 
#
# Here are the fields interpretation in each row:
# #1        NUMA node
# #2        Zone name
# #3 - end  Page order or buckets on page sizes: 
#                4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1024K, and 2048K
#
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

buddy_fname = '/proc/buddyinfo'
buddy_fields = ['numa_node', 
                 'zone_name',
                 'bucket_free_pages'
                ]
buddy_metrics = ['bucket_free_pages_per_sec',
                  'total_free_pages_per_sec',
                  'pct_fragment_per_sec'
                 ]

white_list = []
node_list = []
zone_list = []

stats_cache = {}
stats_current = {}

re_buddyinfo=re.compile(r'^\s*Node\s+(?P<node>\d+)'
                        r',\s+zone\s+(?P<zone>\S+)\s+(?P<pages>.*)$')

def get_host_type():
   for i in host_types:
      if i in host_name:
         host_type = i

def init_stats_cache():
   global white_list

   if os.path.exists(buddy_fname):
      num_buckets = 0
      with open(buddy_fname) as f:
         for line in f:
            match = re_buddyinfo.search(line)
            if not match:
               collectd.error('buddyinfo: unknown line pattern: %s' % (line))
               continue;
            node = match.group('node')
            zone = match.group('zone')
            free_pages = match.group('pages').strip().split()
            num_buckets = len(free_pages)
            if node not in node_list:
               node_list.append(node)
            if zone not in zone_list:
               zone_list.append(zone)
            stats_cache[(node, zone, 'val')] = free_pages
            stats_cache[(node, zone, 'ts')] = time.time()
      f.close()
      for i in range(0, num_buckets):
         white_list.append('free_pages_' + str(4*2**i) + 'K')
      collectd.info('buddyinfo: node_list : %s' % (node_list))
      collectd.info('buddyinfo: zone_list : %s' % (zone_list))
      collectd.info('buddyinfo: white_list: %s' % (white_list))
   else:
      collectd.info('buddyinfo: init_stats_cache: path: %s does not exist' 
                    % (buddy_fname))

def collect_buddyinfo():
    if os.path.exists(buddy_fname):
        with open(buddy_fname) as f:
            for line in f:
               match = re_buddyinfo.search(line)
               if not match:
                  continue;
               node = match.group('node')
               zone = match.group('zone')
               free_pages = match.group('pages').strip().split()
               stats_current[(node, zone, 'val')] = free_pages
               stats_current[(node, zone, 'ts')] = time.time()
               key_val = dict(zip(white_list, free_pages))
               metric = collectd.Values()
               metric.host = host_name
               metric.plugin = 'buddyinfo'
               metric.plugin_instance = node
               metric.type = 'gauge'
               for k in range(0, len(white_list)):
                  metric.type_instance = 'zone_' + zone + '.' 
                  metric.type_instance += white_list[k]
                  metric.values = [free_pages[k]]
                  metric.dispatch()
            f.close()
    else:
        collectd.error('buddyinfo: procfs path: %s does not exist' 
                       % (buddy_fname))

def swap_current_cache():
   stats_cache = stats_current.copy()

def configer(ObjConfiguration):
   collectd.info('buddyinfo plugin: configuring host: %s' % (host_name)) 

def initer():
   get_host_type()
   collectd.info('buddyinfo plugin: host of type: %s' % (host_type))
   collectd.info('buddyinfo initer: white list: %s ' % (white_list))
   init_stats_cache()
   collectd.info('buddyinfo init: stats_cache: %s ' % (stats_cache))

def reader(input_data=None):
   collect_buddyinfo()
   swap_current_cache()

def writer(metric, data=None):
   for i in metric.values:
      collectd.debug("%s (%s): %f" % (metric.plugin, metric.type, i))

def shutdown():
   collectd.info("buddyinfo plugin shutting down")

#== Callbacks ==#
if (os_name == 'Linux'):
   collectd.register_config(configer)
   collectd.register_init(initer)
   collectd.register_read(reader)
   collectd.register_write(writer)
   collectd.register_shutdown(shutdown)
else:
   collectd.warning('buddyinfo plugin currently works for Linux only')

