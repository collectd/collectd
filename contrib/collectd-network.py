#! /usr/bin/env python
# -*- coding: utf-8 -*-
# vim: fileencoding=utf-8
#
# Copyright © 2009 Adrian Perez <aperez@igalia.com>
#
# Distributed under terms of the GPLv2 license.

"""
Collectd network protocol implementation.
"""

import socket
import struct

try:
    from cStringIO import StringIO
except ImportError:
    from StringIO import StringIO

from datetime import datetime
from copy import deepcopy


DEFAULT_PORT = 25826
"""Default port"""

DEFAULT_IPv4_GROUP = "239.192.74.66"
"""Default IPv4 multicast group"""

DEFAULT_IPv6_GROUP = "ff18::efc0:4a42"
"""Default IPv6 multicast group"""



# Message kinds
TYPE_HOST            = 0x0000
TYPE_TIME            = 0x0001
TYPE_PLUGIN          = 0x0002
TYPE_PLUGIN_INSTANCE = 0x0003
TYPE_TYPE            = 0x0004
TYPE_TYPE_INSTANCE   = 0x0005
TYPE_VALUES          = 0x0006
TYPE_INTERVAL        = 0x0007

# For notifications
TYPE_MESSAGE         = 0x0100
TYPE_SEVERITY        = 0x0101

# DS kinds
DS_TYPE_COUNTER      = 0
DS_TYPE_GAUGE        = 1


header = struct.Struct("!2H")
number = struct.Struct("!Q")
short  = struct.Struct("!H")
double = struct.Struct("<d")


def decode_network_values(ptype, plen, buf):
    """Decodes a list of DS values in collectd network format
    """
    nvalues = short.unpack_from(buf, header.size)[0]
    off = header.size + short.size + nvalues
    valskip = double.size

    # Check whether our expected packet size is the reported one
    assert ((valskip + 1) * nvalues + short.size + header.size) == plen
    assert double.size == number.size

    result = []
    for dstype in map(ord, buf[header.size+short.size:off]):
        if dstype == DS_TYPE_COUNTER:
            result.append((dstype, number.unpack_from(buf, off)[0]))
            off += valskip
        elif dstype == DS_TYPE_GAUGE:
            result.append((dstype, double.unpack_from(buf, off)[0]))
            off += valskip
        else:
            raise ValueError("DS type %i unsupported" % dstype)

    return result


def decode_network_number(ptype, plen, buf):
    """Decodes a number (64-bit unsigned) in collectd network format.
    """
    return number.unpack_from(buf, header.size)[0]


def decode_network_string(msgtype, plen, buf):
    """Decodes a floating point number (64-bit) in collectd network format.
    """
    return buf[header.size:plen-1]


# Mapping of message types to decoding functions.
_decoders = {
    TYPE_VALUES         : decode_network_values,
    TYPE_TIME           : decode_network_number,
    TYPE_INTERVAL       : decode_network_number,
    TYPE_HOST           : decode_network_string,
    TYPE_PLUGIN         : decode_network_string,
    TYPE_PLUGIN_INSTANCE: decode_network_string,
    TYPE_TYPE           : decode_network_string,
    TYPE_TYPE_INSTANCE  : decode_network_string,
    TYPE_MESSAGE        : decode_network_string,
    TYPE_SEVERITY       : decode_network_number,
}


def decode_network_packet(buf):
    """Decodes a network packet in collectd format.
    """
    off = 0
    blen = len(buf)
    while off < blen:
        ptype, plen = header.unpack_from(buf, off)

        if plen > blen - off:
            raise ValueError("Packet longer than amount of data in buffer")

        if ptype not in _decoders:
            raise ValueError("Message type %i not recognized" % ptype)

        yield ptype, _decoders[ptype](ptype, plen, buf[off:])
        off += plen





class Data(object):
    time = 0
    host = None
    plugin = None
    plugininstance = None
    type = None
    typeinstance = None

    def __init__(self, **kw):
        [setattr(self, k, v) for k, v in kw.iteritems()]

    @property
    def datetime(self):
        return datetime.fromtimestamp(self.time)

    @property
    def source(self):
        buf = StringIO()
        if self.host:
            buf.write(self.host)
        if self.plugin:
            buf.write("/")
            buf.write(self.plugin)
        if self.plugininstance:
            buf.write("/")
            buf.write(self.plugininstance)
        if self.type:
            buf.write("/")
            buf.write(self.type)
        if self.typeinstance:
            buf.write("/")
            buf.write(self.typeinstance)
        return buf.getvalue()

    def __str__(self):
        return "[%i] %s" % (self.time, self.source)



class Notification(Data):
    FAILURE  = 1
    WARNING  = 2
    OKAY     = 4

    SEVERITY = {
        FAILURE: "FAILURE",
        WARNING: "WARNING",
        OKAY   : "OKAY",
    }

    __severity = 0
    message  = ""

    def __set_severity(self, value):
        if value in (self.FAILURE, self.WARNING, self.OKAY):
            self.__severity = value

    severity = property(lambda self: self.__severity, __set_severity)

    @property
    def severitystring(self):
        return self.SEVERITY.get(self.severity, "UNKNOWN")

    def __str__(self):
        return "%s [%s] %s" % (
                super(Notification, self).__str__(),
                self.severitystring,
                self.message)



class Values(Data, list):
    def __str__(self):
        return "%s %s" % (Data.__str__(self), list.__str__(self))



def interpret_opcodes(iterable):
    vl = Values()
    nt = Notification()

    for kind, data in iterable:
        if kind == TYPE_TIME:
            vl.time = nt.time = data
        elif kind == TYPE_INTERVAL:
            vl.interval = data
        elif kind == TYPE_HOST:
            vl.host = nt.host = data
        elif kind == TYPE_PLUGIN:
            vl.plugin = nt.plugin = data
        elif kind == TYPE_PLUGIN_INSTANCE:
            vl.plugininstance = nt.plugininstance = data
        elif kind == TYPE_TYPE:
            vl.type = nt.type = data
        elif kind == TYPE_TYPE_INSTANCE:
            vl.typeinstance = nt.typeinstance = data
        elif kind == TYPE_SEVERITY:
            nt.severity = data
        elif kind == TYPE_MESSAGE:
            nt.message = data
            yield deepcopy(nt)
        elif kind == TYPE_VALUES:
            vl[:] = data
            yield deepcopy(vl)



class Reader(object):
    """Network reader for collectd data.

    Listens on the network in a given address, which can be a multicast
    group address, and handles reading data when it arrives.
    """
    addr = None
    host = None
    port = DEFAULT_PORT

    BUFFER_SIZE = 1024


    def __init__(self, host=None, port=DEFAULT_PORT, multicast=False):
        if host is None:
            multicast = True
            host = DEFAULT_IPv4_GROUP

        self.host, self.port = host, port
        self.ipv6 = ":" in self.host

        family, socktype, proto, canonname, sockaddr = socket.getaddrinfo(
                None if multicast else self.host, self.port,
                socket.AF_INET6 if self.ipv6 else socket.AF_UNSPEC,
                socket.SOCK_DGRAM, 0, socket.AI_PASSIVE)[0]

        self._sock = socket.socket(family, socktype, proto)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(sockaddr)

        if multicast:
            if hasattr(socket, "SO_REUSEPORT"):
                self._sock.setsockopt(
                        socket.SOL_SOCKET,
                        socket.SO_REUSEPORT, 1)

            val = None
            if family == socket.AF_INET:
                assert "." in self.host
                val = struct.pack("4sl",
                        socket.inet_aton(self.host), socket.INADDR_ANY)
            elif family == socket.AF_INET6:
                raise NotImplementedError("IPv6 support not ready yet")
            else:
                raise ValueError("Unsupported network address family")

            self._sock.setsockopt(
                    socket.IPPROTO_IPV6 if self.ipv6 else socket.IPPROTO_IP,
                    socket.IP_ADD_MEMBERSHIP, val)
            self._sock.setsockopt(
                    socket.IPPROTO_IPV6 if self.ipv6 else socket.IPPROTO_IP,
                    socket.IP_MULTICAST_LOOP, 0)


    def receive(self):
        """Receives a single raw collect network packet.
        """
        return self._sock.recv(self.BUFFER_SIZE)


    def decode(self, buf=None):
        """Decodes a given buffer or the next received packet.
        """
        if buf is None:
            buf = self.receive()
        return decode_network_packet(buf)


    def interpret(self, iterable=None):
        """Interprets a sequence
        """
        if iterable is None:
            iterable = self.decode()
        if isinstance(iterable, basestring):
            iterable = self.decode(iterable)
        return interpret_opcodes(iterable)


