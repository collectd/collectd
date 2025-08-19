#!/usr/bin/python

##########################################################
# fusionio.py
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
# Collectd plugin for fusion-io device measurement. It
# currently measures:
# 1. physical blocks read
# 2. physical blocks written
# 3. total blocks
# 4. min block erases count
# 5. max block erases count
# 6. average block erases count
#
# physical bytes written are obtained as:
#  cmd_bytes_written = '/usr/bin/fio-status -a | '
#  cmd_bytes_written += 'grep \"Physical bytes written:\" | '
#  cmd_bytes_written += 'awk \'{print $4}\''
#
# physical bytes read are obtained as:
#  cmd_bytes_read = '/usr/bin/fio-status -a | '
#  cmd_bytes_read += 'grep \"Physical bytes read   :\" | '
#  cmd_bytes_read += 'awk \'{print $5}\''
#
# Block erase counts (total, min, max, avg) are obtained as:
#  cmd_erased_blocks = '/usr/bin/fio-get-erase-count /dev/fct0 | '
#  cmd_erased_blocks += 'tail -n 5 | cut -d: -f 2 | sed \'s/^ *//\''
# 
##########################################################

import collectd
import platform
import os
import socket
import time
import re
import subprocess

os_name = platform.system()
host_name = socket.gethostbyaddr(socket.gethostname())[0]
host_types = ['app', 'db', 'ffx', 'indexer', 'search', 'other']
host_type = 'other'

cmd_fio_status = '/usr/bin/fio-status'
args_fio_status = '-a'
key_bytes_read = 'Physical bytes read   :'
key_bytes_written = 'Physical bytes written:'

cmd_fio_get_erase_blocks = '/usr/bin/fio-get-erase-count'
args_fio_get_erase_blocks = '-s /dev/fct0'
key_blocks_total = 'Total blocks:'
key_blocks_min = 'Min:'
key_blocks_max = 'Max:'
key_blocks_avg = 'Avg:'

fio_fname = '/proc/fusion/fio/fioa/data/groomer/stats'
fio_fields = ['Blocks']
              #'Data copied:']
fio_white_list = ['phy_bytes_written',
                  'phy_bytes_read',
                  'erased_blocks_total',
                  'erased_blocks_min',
                  'erased_blocks_max',
                  'erased_blocks_avg']

fio_metrics = ['physical_bytes_read_per_sec',
               'physical_bytes_written_per_sec',
               'blocks_erased_per_sec']

stats_cache = {}
stats_current = {}
fiostats_cache = {}
fiostats_current = {}

def get_host_type():
   global host_type
   for i in host_types:
      if i in host_name:
         host_type = i
   collectd.info('fusionio: get_host_by_type: %s' % (host_type))

def is_fio_device():
   if os.path.exists(fio_fname):
      if os.path.exists(cmd_fio_status):
         if os.path.exists(cmd_fio_get_erase_blocks):
            return True
         else:
            collectd.error('%s not found:' % (cmd_fio_get_erase_blocks))
      else:
         collectd.error('%s not found:' % (cmd_fio_status))
   else:
      collectd.error('%s not found:' % (fio_fname))

   return False

def run_cmd (cmd):
   s = ''
   p = subprocess.Popen(cmd, 
                        shell=True, 
                        stdout=subprocess.PIPE, 
                        stderr=subprocess.STDOUT)
   for line in p.stdout.readlines():
      s += line

   return s

def grep(s, pattern):
   return '\n'.join(re.findall(r'^.*%s.*?$'%pattern,s,flags=re.M))

def remove_start(s, word_to_remove):
   return s[len(word_to_remove):] if s.startswith(word_to_remove) else ""

def extract_val(out, key):
   val = 0
   if (out):
      key_line = grep(out, key).lstrip()
      if (key_line):
         collectd.debug('key: %s key_line: %s' % (key, key_line))
         val_str = remove_start(key_line, key)
         collectd.debug('val_str: %s' % (val_str))
         if '.' in val_str:
            val = long(float(val_str.replace(',', '')))
         else:
            val = long(val_str.replace(',', ''))
         collectd.debug('value: %d' % (val))
      else:
         collectd.debug('no line found with key: %s' % (key))
   else:
      collectd.debug('nothing to process in provided output str: %s' % (out))

   return val

def get_physical_bytes():
   bytes_written = 0
   bytes_read = 0

   if os.path.exists(cmd_fio_status):
      cmd = cmd_fio_status + ' ' + args_fio_status
      out = run_cmd(cmd)
      if (out):
         bytes_written = extract_val(out, key_bytes_written)
         bytes_read = extract_val(out, key_bytes_read)
      else:
         collectd.error('get_physical_bytes: failed to run cmd: %s' % (cmd))
   else:
      collectd.warning('get_physical_bytes: %s not found' % (cmd_fio_status))

   return (bytes_read, bytes_written)

def get_block_erases():
   b_total = 0
   b_min = 0
   b_max = 0
   b_avg = 0

   if os.path.exists(cmd_fio_get_erase_blocks):
      cmd = cmd_fio_get_erase_blocks + ' ' + args_fio_get_erase_blocks
      out = run_cmd(cmd)
      if (out):
         ebs = out.split('\n')
         b_total = extract_val(out, key_blocks_total)
         b_min = extract_val(out, key_blocks_min)
         b_max = extract_val(out, key_blocks_max)
         b_avg = extract_val(out, key_blocks_avg)
      else:
         collectd.error('get_block_erases: failed to run cmd: %s' %(cmd))
   else:
      collectd.warning('get_block_erases: %s missing?' % (cmd_get_erase_blocks))

   return (b_total, b_min, b_max, b_avg)


def get_fiostats():
   phy_b_r, phy_b_w = get_physical_bytes()
   eb_total, eb_min, eb_max, eb_avg = get_block_erases()

   for m in fio_white_list:
      fiostats_current[(m, 'ts')] = time.time()
   fiostats_current[('phy_bytes_written', 'val')] = phy_b_w
   fiostats_current[('phy_bytes_read', 'val')] = phy_b_r
   fiostats_current[('erased_blocks_total', 'val')] = eb_total
   fiostats_current[('erased_blocks_min', 'val')] = eb_min
   fiostats_current[('erased_blocks_max', 'val')] = eb_max
   fiostats_current[('erased_blocks_avg', 'val')] = eb_avg

   for m in fio_white_list:
      metric = collectd.Values()
      metric.host = host_name
      metric.plugin = 'fusionio'
      metric.type = 'gauge'
      metric.type_instance = m
      metric.values = [fiostats_current[(m, 'val')]]
      metric.dispatch()



def init_stats_cache():
    if os.path.exists(fio_fname):
        with open(fio_fname) as f:
            for line in f:
                fields = line.split()
                fields = [fl.strip() for fl in fields]
                key_name = fields[0]
                key_val = int(fields[2])
                if any(key_name in s for s in fio_fields):
                    stats_cache[(key_name, 'val')] = key_val
                    stats_cache[(key_name, 'ts')] = time.time()
            f.close()
    else:
        collectd.info('fusionio: init_stats_cache: path: %s does not exist' 
                      % (fio_fname))

def collect_fiostats():
    if os.path.exists(fio_fname):
        with open(fio_fname) as f:
            for line in f:
                fields = line.split()
                fields = [fl.strip() for fl in fields]
                key_name = fields[0]
                key_val = int(fields[2])
                if any(key_name in s for s in fio_fields):
                    metric = collectd.Values()
                    metric.host = host_name
                    metric.plugin = 'fusionio'
                    metric.type = 'gauge'
                    metric.type_instance = key_name
                    metric.values = [key_val]
                    stats_current[(key_name, 'val')] = key_val
                    metric.dispatch()
                    stats_current[(key_name, 'ts')] = time.time()
            f.close()
    else:
        collectd.info('fusionio: procfs path: %s does not exist' % (fio_fname))

def swap_current_cache():
   for i in fio_fields:
       stats_cache[(i, 'val')] = stats_current[(i, 'val')]
       stats_cache[(i, 'ts')] = stats_current[(i, 'ts')]


def configer(ObjConfiguration):
   collectd.info('fusionio plugin: configuring host: %s' % (host_name)) 

def initer():
   collectd.info('fusionio plugin: host of type: %s' % (host_type))
   collectd.info('fusionio initer: fields list: %s ' % (fio_fields))
   init_stats_cache()
   collectd.info('fusionio init: stats_cache: %s ' % (stats_cache))

def reader(input_data=None):
   get_fiostats()
   collect_fiostats()
   #dispatch_metrics()
   swap_current_cache()

def writer(metric, data=None):
   for i in metric.values:
      collectd.debug("%s (%s): %f" % (metric.plugin, metric.type, i))

def shutdown():
   collectd.info("fusionio plugin shutting down")

#== Callbacks ==#
get_host_type()
if ((host_type == 'search') and (os_name == 'Linux') and (is_fio_device())):
   collectd.register_config(configer)
   collectd.register_init(initer)
   collectd.register_read(reader)
   collectd.register_write(writer)
   collectd.register_shutdown(shutdown)
else:
   collectd.error('fio plugin works for search hosts only; type: %s os: %s'
                  % (host_type, os_name))

