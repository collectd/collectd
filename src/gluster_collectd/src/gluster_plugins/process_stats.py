#  Copyright (C) 2015-2016  Red Hat, Inc. <http://www.redhat.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
from gluster_utils import (GlusterStats, get_pids,
                           get_cpu_memory_metrics, CollectdValue)


class ProcessStats(GlusterStats):
    def run(self):
        for process in ["glusterd", "glusterfs"]:
            pids = get_pids(process)
            for pid in pids:
                cpu, memobj = get_cpu_memory_metrics(pid)
                # send cpu metrics
                plugin_instance = process
                cvalue = CollectdValue(self.plugin, plugin_instance, "cpu",
                                       [cpu], str(pid))
                cvalue.dispatch()

                # send rss metrics for memory
                cvalue = CollectdValue(self.plugin, plugin_instance, "ps_rss",
                                       [memobj.rss], str(pid))
                cvalue.dispatch()

                # send virtual memory stats
                cvalue = CollectdValue(self.plugin, plugin_instance, "ps_vm",
                                       [memobj.vms], str(pid))
                cvalue.dispatch()
