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
import psutil
import os
import ConfigParser
import uuid
import subprocess
import traceback
import shlex
import collectd
import ini2json


GLUSTERD_ERROR_MSG = 'Connection failed. '\
    'Please check if gluster daemon is operational.'


def exec_command(cmd_str):
    try:
        cmd = subprocess.Popen(shlex.split(cmd_str),
                               stdin=open(os.devnull, "r"),
                               stderr=subprocess.PIPE,
                               stdout=subprocess.PIPE,
                               close_fds=True)

        stdout, stderr = cmd.communicate()
        if stderr:
            return None, stderr
        return stdout, None
    except (subprocess.CalledProcessError, ValueError):
        except_traceback = traceback.format_exc()
        return None, except_traceback


def get_pids(name):
    output, error = exec_command("pidof %s" % (name))
    pids = []
    if len(output) > 0:
        for proc in output.split():
            pid = int(proc.strip())
            pids.append(pid)
    return pids


def get_cpu_memory_metrics(pid):
    ps = psutil.Process(pid)
    cpu = ps.cpu_percent()
    memory = ps.memory_info()
    return (cpu, memory)


def get_gluster_state_dump():
    global GLUSTERD_ERROR_MSG
    ret_val = {}
    try:
        file_name = "collectd_gstate_%s" % str(uuid.uuid4())
        gluster_state_dump_op, gluster_state_dump_err = exec_command(
            'gluster get-state glusterd odir /var/run file %s detail' % (
                file_name
            )
        )
        if (
            gluster_state_dump_err or
            GLUSTERD_ERROR_MSG in gluster_state_dump_op
        ):
            return ret_val, gluster_state_dump_err
        gluster_state_dump = ini2json.ini_to_dict(
            '/var/run/%s' % file_name
        )
        rm_state_dump, rm_state_dump_err = exec_command(
            'rm -rf /var/run/%s' % file_name
        )
        if rm_state_dump_err:
            return ret_val, rm_state_dump_err
        return gluster_state_dump, None
    except (
        IOError,
        AttributeError,
        ConfigParser.MissingSectionHeaderError,
        ConfigParser.ParsingError,
        ValueError
    ):
        return ret_val, traceback.format_exc()


def parse_get_state(get_state_json):
    cluster_topology = {}
    if "Peers" in get_state_json:
        cluster_peers = []
        processed_peer_indexes = []
        peers = get_state_json['Peers']
        for key, value in peers.iteritems():
            peer_index = key.split('.')[0].split('peer')[1]
            if peer_index not in processed_peer_indexes:
                cluster_peers.append({'host_name': peers[
                    'peer%s.primary_hostname' % peer_index
                     ],
                    'uuid': peers[
                        'peer%s.uuid' % peer_index
                    ],
                    'state': peers[
                        'peer%s.state' % peer_index
                    ],
                    'connected': peers[
                        'peer%s.connected' % peer_index
                    ]
                })
                processed_peer_indexes.append(peer_index)
                cluster_topology['peers'] = cluster_peers

    if "Volumes" in get_state_json:
        processed_vol_indexes = []
        vols = []
        volumes = get_state_json['Volumes']
        for key, value in volumes.iteritems():
            vol_index = key.split('.')[0].split('volume')[1]
            if vol_index not in processed_vol_indexes:
                volume = {
                     'name': volumes['volume%s.name' % vol_index],
                     'id': volumes['volume%s.id' % vol_index],
                     'type': volumes['volume%s.type' % vol_index],
                     'transport_type': volumes[
                         'volume%s.transport_type' % vol_index
                     ],
                     'status': volumes['volume%s.status' % vol_index],
                     'brick_count': volumes['volume%s.brickcount' % vol_index],
                     'snap_count': volumes['volume%s.snap_count' % vol_index],
                     'stripe_count': volumes[
                         'volume%s.stripe_count' % vol_index
                     ],
                     'replica_count': volumes[
                         'volume%s.replica_count' % vol_index
                     ],
                     'subvol_count': volumes[
                         'volume%s.subvol_count' % vol_index
                     ],
                     'arbiter_count': volumes[
                         'volume%s.arbiter_count' % vol_index
                     ],
                     'disperse_count': volumes[
                         'volume%s.disperse_count' % vol_index
                     ],
                     'redundancy_count': volumes[
                         'volume%s.redundancy_count' % vol_index
                     ],
                     'quorum_status': volumes[
                         'volume%s.quorum_status' % vol_index
                     ],
                     'rebalance_status': volumes[
                         'volume%s.rebalance.status' % vol_index
                     ],
                     'rebalance_failures': volumes[
                         'volume%s.rebalance.failures' % vol_index
                     ],
                     'rebalance_skipped': volumes[
                         'volume%s.rebalance.skipped' % vol_index
                     ],
                     'rebalance_files': volumes[
                         'volume%s.rebalance.files' % vol_index
                     ],
                     'rebalance_data': volumes[
                         'volume%s.rebalance.data' % vol_index
                        ]
                 }
                sub_volumes = {}
                no_of_bricks = int(volume['brick_count'])
                for brick_index in range(1, no_of_bricks + 1):
                    client_count = 0
                    if (
                         'volume%s.brick%s.client_count' % (
                             vol_index,
                             brick_index
                         ) in volumes
                     ):
                        client_count = volumes[
                            'volume%s.brick%s.client_count' % (
                                vol_index,
                                brick_index
                            )
                        ]
                    brick = {
                       'hostname': volumes[
                          'volume%s.brick%s.hostname' % (
                              vol_index,
                              brick_index
                           )
                        ],
                       'path': volumes[
                            'volume%s.brick%s.path' % (
                                vol_index,
                                brick_index
                            )
                        ].split(':')[1],
                       'connections_count': client_count,
                       'brick_index': brick_index
                    }
                    brick_status_key = 'volume%s.brick%s.status' % (
                        vol_index,
                        brick_index
                    )
                    if brick_status_key in volumes:
                        brick['status'] = volumes[brick_status_key]
                    sub_vol_index = (
                        (brick_index - 1) * int(volume['subvol_count'])
                    ) / no_of_bricks
                    sub_vol_bricks = sub_volumes.get(str(sub_vol_index), [])
                    sub_vol_bricks.append(brick)
                    sub_volumes[str(sub_vol_index)] = sub_vol_bricks
                volume['bricks'] = sub_volumes
                vols.append(volume)
                cluster_topology['volumes'] = vols
                processed_vol_indexes.append(vol_index)
    return cluster_topology


def get_gluster_cluster_topology():
    gluster_state_dump_json, gluster_state_dump_err = get_gluster_state_dump()
    if gluster_state_dump_err:
        collectd.error('Failed to fetch cluster topology. Error %s' %
                       (gluster_state_dump_err)
                       )
        return {}
    try:
        cluster_topology = parse_get_state(gluster_state_dump_json)
        return cluster_topology
    except (KeyError, AttributeError):
        collectd.error('Failed to fetch cluster topology. Error %s' %
                       (traceback.format_exc())
                       )
        return {}


class GlusterStats(object):
    CONFIG = {}
    CLUSTER_TOPOLOGY = {}

    def __init__(self):
        self.plugin = "gluster"

    def run(self):
        raise NotImplementedError


class CollectdValue(object):
    def __init__(self, plugin, plugin_instance, plugin_type, value,
                 type_instance=None):
        self.plugin = plugin
        self.plugin_instance = plugin_instance
        self.plugin_type = plugin_type
        self.value = value
        self.type_instance = type_instance
        self.hostname = GlusterStats.CONFIG['peer_name']

    def dispatch(self):
        value = collectd.Values(plugin=self.plugin, type=self.plugin_type)
        if self.type_instance:
            value.type_instance = self.type_instance
        value.plugin_instance = self.plugin_instance
        value.host = self.hostname
        value.dispatch(values=self.value)


if __name__ == "__main__":
    topology = get_gluster_state_dump()
    print (topology)
