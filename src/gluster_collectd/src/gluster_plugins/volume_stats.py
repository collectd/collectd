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
import collectd
import os
# import threading
from gluster_utils import GlusterStats, CollectdValue


ret_val = {}


class VolumeStats(GlusterStats):
    def __init__(self):
        GlusterStats.__init__(self)

    def get_heal_entries(self, brick_path):
        heal_dir = os.path.join(brick_path, ".glusterfs/indices/xattrop")
        heal_entries = 0
        try:
            for entry in os.listdir(heal_dir):
                if "xattrop" not in entry:
                    heal_entries += 1
        except OSError:
            collectd.info("%s doesn't exist, is gluster running" % (heal_dir))
        return heal_entries

    def get_volume_heal_info(self, volume):
        entries = {}
        for sub_volume_index, sub_volume_bricks in volume.get(
            'bricks',
            {}
        ).iteritems():
            for brick in sub_volume_bricks:
                if brick['hostname'] == self.CONFIG['peer_name']:
                    brick_path = brick['path']
                    num_entries = self.get_heal_entries(brick_path)
                    entries[brick_path] = num_entries
        return entries

    def get_heal_info(self, volume):
        heal_entries = self.get_volume_heal_info(volume)
        volname = volume['name']
        hostname = self.CONFIG['peer_name']
        list_values = []
        for brick_path in heal_entries:

            t_name = "brick_healed_cnt"
            healed_cnt = heal_entries[brick_path]
            cvalue = CollectdValue(self.plugin, volname, t_name,
                                   [healed_cnt], brick_path)
            cvalue.hostname = hostname
            list_values.append(cvalue)
        return list_values

    def run(self):
        for volume in self.CLUSTER_TOPOLOGY.get('volumes', []):
            if 'Replicate' in volume.get('type', ''):
                list_values = self.get_heal_info(volume)
                for collectd_value in list_values:
                    collectd_value.dispatch()

                # thread = threading.Thread(
                # target=get_heal_info,
                # args=(volume, CONFIG['integration_id'],)
                # )
                # thread.start()
                # threads.append(
                # thread
                # )
                # for thread in threads:
                #    thread.join(1)
                # for thread in threads:
                #    del thread
