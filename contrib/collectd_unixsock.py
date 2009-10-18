#-*- coding: ISO-8859-1 -*-
# collect.py: the python collectd-unixsock module.
#
# Requires collectd to be configured with the unixsock plugin, like so:
#
# LoadPlugin unixsock
# <Plugin unixsock>
#   SocketFile "/var/run/collectd-unixsock"
#   SocketPerms "0775"
# </Plugin>
#
# Copyright (C) 2008 Clay Loveless <clay@killersoft.com>
#
# This software is provided 'as-is', without any express or implied
# warranty.  In no event will the author be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.

import socket
import string


class Collect(object):

    def __init__(self, path='/var/run/collectd-unixsock'):
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._path = path
        self._sock.connect(self._path)

    def list(self):
        numvalues = self._cmd('LISTVAL')
        lines = []
        if numvalues:
            lines = self._readlines(numvalues)
        return lines

    def get(self, val, flush=True):
        numvalues = self._cmd('GETVAL "' + val + '"')
        lines = []
        if numvalues:
            lines = self._readlines(numvalues)
        if flush:
            self._cmd('FLUSH identifier="' + val + '"')
        return lines

    def _cmd(self, c):
        self._sock.send(c + "\n")
        stat = string.split(self._readline())
        status = int(stat[0])
        if status:
            return status
        return False

    def _readline(self):
        """Read single line from socket"""
        data = ''
        buf = []
        recv = self._sock.recv
        while data != "\n":
            data = recv(1)
            if not data:
                break
            if data != "\n":
                buf.append(data)
        return ''.join(buf)

    def _readlines(self, sizehint=0):
        """Read multiple lines from socket"""
        total = 0
        list = []
        while True:
            line = self._readline()
            if not line:
                break
            list.append(line)
            total = len(list)
            if sizehint and total >= sizehint:
                break
        return list

    def __del__(self):
        self._sock.close()


if __name__ == '__main__':
    """Collect values from socket and dump to STDOUT"""

    c = Collect('/var/run/collectd-unixsock')
    list = c.list()

    for val in list:
        stamp, key = string.split(val)
        glines = c.get(key)
        print stamp + ' ' + key + ' ' + ', '.join(glines)
