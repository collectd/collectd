#!/usr/bin/python

############################################################
# zoneinfo.py
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
# /proc/zoneinfo breaks down virtual memory stats with
# respect to each NUMA node and memory zone. It suppliments
# the measurements provided by vmstta and buddyinfo plugins.
############################################################

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

zoneinfo_fname = '/proc/zoneinfo'

white_list = ['min',
              'low',
              'high',
              'scanned',
              'nr_free_pages',
              'nr_dirty',
              'nr_writeback',
              'nr_vmscan_write',
              'nr_anon_transparent_hugepages']
node_list = []
zone_list = []

stats_cache = {}
stats_current = {}

re_zoneinfo=re.compile(r'^\s*Node\s+(?P<node>\d+)'
                       r',\s+zone\s+(?P<zone>\S+)'
                       r'(?:\n|\r\n?)'
                       r'^\s+pages free\s+(?P<zone_free>\d+)'
                       r'(?:\n|\r\n?)'
                       r'^\s+min\s+(?P<min>\d+)'
                       r'(?:\n|\r\n?)'
                       r'^\s+low\s+(?P<low>\d+)'
                       r'(?:\n|\r\n?)'
                       r'^\s+high\s+(?P<high>\d+)'
                       r'(?:\n|\r\n?)'
                       r'^\s+scanned\s+(?P<scanned>\d+)'
                       r'(?:\n|\r\n?)'
                       r'(^(.+)\n)+?'
                       r'^\s+nr_free_pages\s+(?P<nr_free_pages>\d+)'
                       r'(?:\n|\r\n?)'
                       r'(^(.+)\n)+?'
                       r'^\s+nr_dirty\s+(?P<nr_dirty>\d+)'
                       r'(?:\n|\r\n?)'
                       r'^\s+nr_writeback\s+(?P<nr_writeback>\d+)'
                       r'(?:\n|\r\n?)'
                       r'(^(.+)\n)+?'
                       r'^\s+nr_anon_transparent_hugepages\s+(?P<nr_anon_transparent_hugepages>\d+)'
                       , re.MULTILINE)

def get_host_type():
   for i in host_types:
      if i in host_name:
         host_type = i

def init_stats_cache():
   if os.path.exists(zoneinfo_fname):
      with open(zoneinfo_fname) as f:
         match = re.finditer(re_zoneinfo, f.read())
         if not match:
            collectd.error('zoneinfo: init: pattern not found')
            return
         for m in match:
            node = m.group('node')
            zone = m.group('zone')
            if node not in node_list:
               node_list.append(node)
            if zone not in zone_list:
               zone_list.append(zone)

            zone_pages = []
            for i in white_list:
               zone_pages.append(m.group(i))

            stats_cache[(node, zone, 'val')] = zone_pages
            stats_cache[(node, zone, 'ts')] = time.time()

         f.close()
         collectd.info('node_list: %s' % (node_list))
         collectd.info('zone_list: %s' % (zone_list))
         collectd.info('white_list: %s' % (white_list))
   else:
      collectd.error('zoneinfo: init: procfs path: %s does not exist' 
                     % (zoneinfo_fname))

def collect_zoneinfo():
   if os.path.exists(zoneinfo_fname):
      with open(zoneinfo_fname) as f:
         match = re.finditer(re_zoneinfo, f.read())
         if not match:
            collectd.error('zoneinfo: collect: pattern not found')
            return
         for m in match:
            zone_pages = []
            node = m.group('node')
            zone = m.group('zone')
            for i in white_list:
               zone_pages.append(m.group(i))
            stats_current[(node, zone, 'val')] = zone_pages
            stats_current[(node, zone, 'ts')] = time.time()
            metric = collectd.Values()
            metric.host = host_name
            metric.plugin = 'zoneinfo'
            metric.plugin_instance = node
            metric.type = 'gauge'
            for k in range(0, len(white_list)):
               metric.type_instance = 'zone_' + zone + '_' 
               metric.type_instance += white_list[k]
               metric.values = [zone_pages[k]]
               metric.dispatch()

      f.close()
   else:
      collectd.error('zoneinfo: collect: procfs path: %s does not exist' 
                     % (zoneinfo_fname))

def swap_current_cache():
   stats_cache = stats_current.copy()

def configer(ObjConfiguration):
   collectd.info('zoneinfo plugin: configuring host: %s' % (host_name)) 

def initer():
   get_host_type()
   collectd.info('zoneinfo plugin: host of type: %s' % (host_type))
   collectd.info('zoneinfo initer: white list: %s ' % (white_list))
   init_stats_cache()
   collectd.info('zoneinfo init: stats_cache: %s' % (stats_cache))

def reader(input_data=None):
   collect_zoneinfo()
   swap_current_cache()

def writer(metric, data=None):
   for i in metric.values:
      collectd.debug("%s (%s): %f" % (metric.plugin, metric.type, i))

def shutdown():
   collectd.info("zoneinfo plugin shutting down")

#== Callbacks ==#
if (os_name == 'Linux'):
   collectd.register_config(configer)
   collectd.register_init(initer)
   collectd.register_read(reader)
   collectd.register_write(writer)
   collectd.register_shutdown(shutdown)
else:
   collectd.warning('zoneinfo plugin currently works for Linux only')

