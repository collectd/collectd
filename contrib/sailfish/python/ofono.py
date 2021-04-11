# -*- coding: utf-8 -*-

# Copyright (C) 2021 Rinigus
#
# SPDX-License-Identifier: MIT
#
# This plugin finds the strength of the cellular network and
# logs it. Used for connections handled by ofono.

import collectd
import dbus

bus = None
ofono_main = None
ofono_main_iface = None

def init():
    global bus, ofono_main, ofono_main_iface
    
    #collectd.debug('ofono init called')
    bus = dbus.SystemBus()
    ofono_main = bus.get_object('org.ofono', '/')
    ofono_main_iface = dbus.Interface(ofono_main,
                                      dbus_interface = 'org.ofono.Manager')

def read():
    global bus, ofono_main_iface

    #collectd.debug('ofono read')    
    modems = ofono_main_iface.GetModems()
    for m in modems:
        if len(m) == 2:
            mname = m[0]
            prop = m[1]
            online = bool(prop.get('Online', False))

            if online:
                modem = bus.get_object('org.ofono', mname)
                modem_prop = modem.GetProperties(dbus_interface = 'org.ofono.NetworkRegistration')
                tech = modem_prop.get('Technology', 'unknown')
                strength = int(modem_prop.get('Strength', 0))

                vl = collectd.Values(type='signal_strength', type_instance=tech)
                vl.plugin='ofono'
                vl.dispatch(values=[strength])

                #collectd.debug('ofono readout result: ' + tech + ('=%d' % strength))
                return # using data from the first connected modem only

# register with collectd
collectd.register_init(init)
collectd.register_read(read)
