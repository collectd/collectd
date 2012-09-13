#!/usr/bin/python

###############################################################################
#         WARNING! Importing this script will break the exec plugin!          #
###############################################################################
# Use this if you want to create new processes from your python scripts.      #
# Normally you will get a OSError exception when the new process terminates   #
# because collectd will ignore the SIGCHLD python is waiting for.             #
# This script will restore the default SIGCHLD behavior so python scripts can #
# create new processes without errors.                                        #
###############################################################################
#         WARNING! Importing this script will break the exec plugin!          #
###############################################################################

import signal
import collectd

def init():
	signal.signal(signal.SIGCHLD, signal.SIG_DFL)

collectd.register_init(init)
