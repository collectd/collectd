#!/bin/bash

# check root
if [ "$(id -u)" != "0" ]; then
  echo "Need to be root !"
  exit 1
fi

if [ ! -f ./contrib/btrfs/collectd_btrfs.conf ]; then
cat <<EOF > ./contrib/btrfs/collectd_btrfs.conf
BaseDir "/var/lib/collectd"
PIDFile "/run/collectd.pid"
PluginDir "${PWD}/.libs"
Interval 10.0

LoadPlugin unixsock
<Plugin unixsock>
	SocketFile "/tmp/collectd.sock"
	SocketGroup "nobody"
	SocketPerms "0777"
</Plugin>


LoadPlugin btrfs
<Plugin btrfs>
  RefreshMounts "on"
</Plugin>

EOF
fi

if [ ! -f /tmp/btrfstest.image ]; then
    dd if=/dev/zero bs=1M count=150 of=/tmp/btrfstest.image
    mkfs.btrfs /tmp/btrfstest.image

    mkdir -p /tmp/btrfstest
    mount /tmp/btrfstest.image /tmp/btrfstest

    if [ "$?" != 0 ]; then
      echo "Mount failed, do nothing ..."
      exit 1
    fi

    dd if=/dev/urandom of=/tmp/btrfstest/random.file
    sync
    dd if=/dev/urandom bs=1M count=10 seek=70 of=/tmp/btrfstest.image
    btrfs scrub start /tmp/btrfstest
fi



./collectd -C contrib/btrfs/collectd_btrfs.conf -f
