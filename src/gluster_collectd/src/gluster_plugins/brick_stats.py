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
import shlex
import socket
import subprocess
from subprocess import Popen
import threading
import traceback
import psutil


from gluster_utils import GlusterStats, CollectdValue


class BrickStats(GlusterStats):
    def __init__(self):
        GlusterStats.__init__(self)
        # volname is key and value is list of dict
        self.local_bricks = {}
        self._init_bricks()
        self.local_disks = {}
        self._init_disks()

    def _get_mount_point(self, path):
        mount = os.path.realpath(path)
        while not os.path.ismount(mount):
            mount = os.path.dirname(mount)
        return mount

    def _init_bricks(self):
        volumes = self.CLUSTER_TOPOLOGY.get('volumes', [])
        for volume in volumes:
            vol_name = volume['name']
            self.local_bricks[vol_name] = []
            for sub_volume_index, sub_volume_bricks in volume.get(
                'bricks',
                {}
            ).iteritems():
                for brick in sub_volume_bricks:
                    brick_hostname = brick['hostname']
                    if (
                        brick_hostname == socket.gethostbyname(
                            self.CONFIG['peer_name']
                        ) or
                        brick_hostname == self.CONFIG['peer_name']
                    ):
                        self.local_bricks[vol_name].append(brick)

    def _init_disks(self):
        if len(self.local_bricks) == 0:
            self._init_bricks()

        disk_partitions = psutil.disk_partitions()
        for key in self.local_bricks:
            bricks = self.local_bricks[key]
            for brick in bricks:
                brick_path = brick.get('path', "")
                mount_point = self._get_mount_point(brick_path)
                for partition in disk_partitions:
                    if partition.mountpoint == mount_point:
                        self.local_disks[brick_path] = partition.device

    def push_io_stats(self):
        io_stats = psutil.disk_io_counters(perdisk=True)
        for brick, disk in self.local_disks.iteritems():
            device = disk.replace('/dev/', '').strip()
            collectd.info("VENKAT - device is %s" % (device))
            if device in io_stats:
                disk_io = io_stats[device]
                # push stats to collectd
                read_bytes, write_bytes = disk_io.read_bytes,\
                    disk_io.write_bytes
                cvalue = CollectdValue(self.plugin, brick,
                                       "disk_octets",
                                       [read_bytes, write_bytes], None)
                cvalue.dispatch()

                read_time, write_time = disk_io.read_time,\
                    disk_io.write_time
                cvalue = CollectdValue(self.plugin, brick, "disk_time",
                                       [read_time, write_time], None)
                cvalue.dispatch()

                read_count, write_count = disk_io.read_count,\
                    disk_io.write_count
                cvalue = CollectdValue(self.plugin, brick, "disk_ops",
                                       [read_count, write_count], None
                                       )
                cvalue.dispatch()

    def _parse_proc_mounts(self, filter=True):
        mount_points = {}
        with open('/proc/mounts', 'r') as f:
            for line in f:
                if line.startswith("/") or not filter:
                    mount = {}
                    tokens = line.split()
                    mount['device'] = tokens[0]
                    mount['fsType'] = tokens[2]
                    mount['mountOptions'] = tokens[3]
                    mount_points[tokens[1]] = mount
        return mount_points

    def _get_stats(self, mount_point):
        data = os.statvfs(mount_point)
        total = (data.f_blocks * data.f_bsize)
        free = (data.f_bfree * data.f_bsize)
        used_percent = 100 - (100.0 * free / total)
        total_inode = data.f_files
        free_inode = data.f_ffree
        used_percent_inode = 100 - (100.0 * free_inode / total_inode)
        used = total - free
        used_inode = total_inode - free_inode
        return {'total': total,
                'free': free,
                'used_percent': used_percent,
                'total_inode': total_inode,
                'free_inode': free_inode,
                'used_inode': used_inode,
                'used': used,
                'used_percent_inode': used_percent_inode}

    def get_lvs(self):
        _lvm_cmd = (
            "lvm vgs --unquoted --noheading --nameprefixes "
            "--separator $ --nosuffix --units m -o lv_uuid,"
            "lv_name,data_percent,pool_lv,lv_attr,lv_size,"
            "lv_path,lv_metadata_size,metadata_percent,vg_name"
        )
        try:
            cmd = Popen(
                shlex.split(
                    _lvm_cmd
                ),
                stdin=open(os.devnull, "r"),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                close_fds=True
            )
            stdout, stderr = cmd.communicate()
            if stderr:
                collectd.error(
                    'Failed to fetch lvs. Error %s' % stderr
                )
            else:
                out = stdout.split('\n')[:-1]
                l = map(
                    lambda x: dict(x), map(
                        lambda x: [
                            e.split('=') for e in x
                        ],
                        map(lambda x: x.strip().split('$'), out)
                    )
                )
                d = {}
                for i in l:
                    if i['LVM2_LV_ATTR'][0] == 't':
                        k = "%s/%s" % (i['LVM2_VG_NAME'], i['LVM2_LV_NAME'])
                    else:
                        k = os.path.realpath(i['LVM2_LV_PATH'])
                    d.update({k: i})
                return d
        except (
            OSError,
            ValueError,
            KeyError,
            subprocess.CalledProcessError
        ) as ex:
            collectd.error('Failed to fetch lvs. Error %s' % str(ex))
            return None

    def get_mount_stats(
        self,
        mount_path
    ):
        def _get_mounts(mount_path=[]):
            mount_list = self._get_mount_point(mount_path)
            mount_points = self._parse_proc_mounts()
            if isinstance(mount_list, basestring):
                mount_list = [mount_list]
            outList = set(mount_points).intersection(set(mount_list))
            # list comprehension to build dictionary does not work in
            # python 2.6.6
            mounts = {}
            for k in outList:
                mounts[k] = mount_points[k]
            return mounts

        def _get_thin_pool_stat(device):
            out = {'thinpool_size': None,
                   'thinpool_used_percent': None,
                   'metadata_size': None,
                   'metadata_used_percent': None,
                   'thinpool_free': None,
                   'metadata_free': None,
                   'thinpool_used': None,
                   'metadata_used': None}
            if lvs and device in lvs and \
               lvs[device]['LVM2_LV_ATTR'][0] == 'V':
                thinpool = "%s/%s" % (lvs[device]['LVM2_VG_NAME'],
                                      lvs[device]['LVM2_POOL_LV'])
                out['thinpool_size'] = float(
                    lvs[thinpool]['LVM2_LV_SIZE']) / 1024
                out['thinpool_used_percent'] = float(
                    lvs[thinpool]['LVM2_DATA_PERCENT'])
                out['metadata_size'] = float(
                    lvs[thinpool]['LVM2_LV_METADATA_SIZE']) / 1024
                out['metadata_used_percent'] = float(
                    lvs[thinpool]['LVM2_METADATA_PERCENT'])
                out['thinpool_free'] = out['thinpool_size'] * (
                    1 - out['thinpool_used_percent'] / 100.0)
                out['thinpool_used'] = \
                    out['thinpool_size'] - out['thinpool_free']
                out['metadata_free'] = out['metadata_size'] * (
                    1 - out['metadata_used_percent'] / 100.0)
                out['metadata_used'] = \
                    out['metadata_size'] - out['metadata_free']
            return out
        mount_points = _get_mounts(mount_path)
        lvs = self.get_lvs()
        mount_detail = {}
        if not mount_points:
            return mount_detail
        for mount, info in mount_points.iteritems():
            mount_detail[mount] = self._get_stats(mount)
            mount_detail[mount].update(
                _get_thin_pool_stat(os.path.realpath(info['device']))
            )
            mount_detail[mount].update({'mount_point': mount})
        return mount_detail

    def brick_utilization(self, path):
        """{
             'connections_count' : 1,

             'used_percent': 0.6338674168297445,

             'used': 0.0316314697265625,

             'free_inode': 2621390,

             'used_inode': 50,

             'free': 4.9586029052734375,

             'total_inode': 2621440,

             'mount_point': u'/bricks/brick2',

             'metadata_used_percent': None,

             'total': 4.990234375,

             'thinpool_free': None,

             'metadata_used': None,

             'thinpool_used_percent': None,

             'used_percent_inode': 0.0019073486328125,

             'thinpool_used': None,

             'metadata_size': None,

             'metadata_free': None,

             'thinpool_size': None

        }
        """
        # Below logic will find mount_path from path
        mount_stats = self.get_mount_stats(path)
        if not mount_stats:
            return None
        return mount_stats.values()[0]

    def get_brick_utilization(self):
        self.brick_utilizations = {}
        threads = []
        for vol in self.local_bricks:
            bricks = self.local_bricks[vol]
            for brick in bricks:
                thread = threading.Thread(
                    target=self.calc_brick_utilization,
                    args=(vol, brick,)
                )
                thread.start()
                threads.append(thread)
        for thread in threads:
            thread.join(1)
        for thread in threads:
            del thread
        return self.brick_utilizations
        '''
        volumes = self.CLUSTER_TOPOLOGY.get('volumes', [])
        threads = []
        for volume in volumes:
            for sub_volume_index, sub_volume_bricks in volume.get(
                'bricks',
                {}
            ).iteritems():
                for brick in sub_volume_bricks:
                    brick_hostname = brick['hostname']
                    # Check if current brick is from localhost else utilization
                    # of brick from some other host can't be computed here..
                    if (
                        brick_hostname == socket.gethostbyname(
                            self.CONFIG['peer_name']
                        ) or
                        brick_hostname == self.CONFIG['peer_name']
                    ):
                        thread = threading.Thread(
                            target=self.calc_brick_utilization,
                            args=(volume['name'], brick,)
                        )
                        thread.start()
                        threads.append(
                            thread
                        )
        for thread in threads:
            thread.join(1)
        for thread in threads:
            del thread
        return self.brick_utilizations
    '''

    def calc_brick_utilization(self, vol_name, brick):
        try:
            brick_path = brick['path']
            # mount = self._get_mount_point(brick_path)
            brick_hostname = brick['hostname']
            utilization = self.brick_utilization(
                brick['path']
            )
            utilization['hostname'] = brick_hostname
            utilization['brick_path'] = brick_path
            utilization['connections_count'] = brick['connections_count']
            utilizations = self.brick_utilizations.get(vol_name, [])
            utilizations.append(utilization)
            self.brick_utilizations[vol_name] = utilizations
        except (
            AttributeError,
            KeyError,
            ValueError
        ):
            collectd.error(
                'Failed to fetch utilization of brick %s of'
                ' host %s. Error %s' % (
                    brick['path'],
                    brick['hostname'],
                    traceback.format_exc()
                )
            )

    def run(self):
        self.push_brick_stats()
        self.push_io_stats()

    def push_brick_stats(self):
        list_values = []
        stats = self.get_brick_utilization()
        for vol, brick_usages in stats.iteritems():
            num_bricks = len(brick_usages)
            cvalue = CollectdValue(self.plugin,  vol, "num_bricks",
                                   [num_bricks], None)
            list_values.append(cvalue)

            for brick_usage in brick_usages:
                brick_path = brick_usage.get('brick_path')
                t_name = "brick_connections"
                connections_count = brick_usage.get('connections_count', 0)
                if connections_count:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [connections_count], None)
                    list_values.append(cvalue)

                t_name = "brick_bytes"
                used = brick_usage.get('used')
                cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                       [used], "used")
                list_values.append(cvalue)

                t_name = "brick_bytes"
                total = brick_usage.get('total')
                cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                       [total], "total")
                list_values.append(cvalue)

                t_name = "brick_used_percent"
                used_percent = brick_usage.get('used_percent')
                cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                       [used_percent], None)
                list_values.append(cvalue)

                t_name = "brick_thinpool_used_percent"
                thinpool_used_percent = brick_usage.get('thinpool_used_percent')
                if thinpool_used_percent:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [thinpool_used_percent], None)
                    list_values.append(cvalue)

                t_name = "brick_metadata_used_percent"
                metadata_used_percent = brick_usage.get('metadata_used_percent')
                if metadata_used_percent:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [metadata_used_percent], None)
                    list_values.append(cvalue)

                t_name = "brick_metadata_used"
                metadata_used = brick_usage.get('metadata_used')
                if metadata_used:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [metadata_used], None)
                    list_values.append(cvalue)

                t_name = "brick_inode"
                used_inode = brick_usage.get('used_inode')
                if used_inode:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [used_inode], "used")
                    list_values.append(cvalue)

                t_name = "brick_inode"
                total_inode = brick_usage.get('total_inode')
                if total_inode:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [total_inode], "total")
                    list_values.append(cvalue)

                t_name = "brick_inode_used_percent"
                used_percent_inode = brick_usage.get('used_percent_inode')
                if used_percent_inode:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [used_percent_inode], None)
                    list_values.append(cvalue)

                t_name = "brick_thinpool"
                thinpool_used = brick_usage.get('thinpool_used')
                if thinpool_used:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [thinpool_used], "used")
                    list_values.append(cvalue)

                t_name = "brick_thinpool"
                thinpool_size = brick_usage.get('thinpool_size')
                if thinpool_size:
                    cvalue = CollectdValue(self.plugin, brick_path, t_name,
                                           [thinpool_size], "size")
                    list_values.append(cvalue)

        for value in list_values:
            value.dispatch()
