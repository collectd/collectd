#!/usr/bin/env python
# vim: sts=4 sw=4 et

# Simple unicast proxy to send collectd traffic to another host/port.
# Copyright (C) 2007  Pavel Shramov <shramov at mexmat.net>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; only version 2 of the License is applicable.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA  02111-1307  USA

"""
Simple unicast proxy for collectd (>= 4.0).
Binds to 'local' address and forwards all traffic to 'remote'.
"""

import socket
import struct

""" Local multicast group/port"""
local  = ("239.192.74.66", 25826)
""" Address to send packets """
remote = ("grid.pp.ru", 35826)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
mreq = struct.pack("4sl", socket.inet_aton(local[0]), socket.INADDR_ANY)

sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
sock.bind(local)

out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

if __name__ == "__main__":
    while True:
        (buf, addr) = sock.recvfrom(2048)
        sock.sendto(buf, remote)
