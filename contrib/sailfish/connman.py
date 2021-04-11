# -*- coding: utf-8 -*-

# Copyright (C) 2021 Rinigus
#
# SPDX-License-Identifier: MIT
#
# This plugin finds the strength of the connected network strength and
# logs it. Used for connections handled by connman.

import collectd
import dbus

bus = None
main = None
main_iface = None

def init():
    global bus, main, main_iface
    
    #collectd.debug('connman init called')
    bus = dbus.SystemBus()
    main = bus.get_object('net.connman', '/')
    main_iface = dbus.Interface(main,
                                dbus_interface = 'net.connman.Manager')

def read():
    global main_iface

    #collectd.debug('connman read')    
    services = main_iface.GetServices()
    for s in services:
        if len(s) == 2:
            prop = s[1]
            if str(prop.get("State", "offline")) == "online":
                t = str(prop.get("Type", ""))
                v = int(prop.get("Strength", -1))
                if len(t) > 0 and v > 0:
                    vl = collectd.Values(type='signal_strength', type_instance=t)
                    vl.plugin='connman'
                    vl.dispatch(values=[v])

                    #collectd.debug('connman readout result: ' + t + ('=%d' % v))
                    return # using data from the first connected modem only

# register with collectd
collectd.register_init(init)
collectd.register_read(read)
