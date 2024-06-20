#!/bin/sh -x

CONF_DIR=/etc/collectd.d
PLUGIN_DIR=/usr/share/collectd/plugins/python


# create conf and plugin dirs if do not exist already
mkdir -p $CONF_DIR
mkdir -p $PLUGIN_DIR

# copy all conf files
cp *.conf $CONF_DIR/.

# copy all plugins
cp *.py $PLUGIN_DIR/.

# restart collectd
/sbin/service collectd restart
/sbin/chkconfig collectd on
