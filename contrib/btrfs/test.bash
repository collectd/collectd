#!/bin/bash

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
EOF
fi

if [ ! -f /tmp/btrfstest.image ]; then
    dd if=/dev/zero bs=1M count=150 of=/tmp/btrfstest.image
    mkfs.btrfs /tmp/btrfstest.image
fi

umount /tmp/btrfstest
mkdir -p /tmp/btrfstest
mount /tmp/btrfstest.image /tmp/btrfstest
dd if=/dev/urandom of=/tmp/btrfstest/random.file
dd if=/dev/urandom bs=1M count=1 seek=50 of=/tmp/btrfstest.image


#./collectd -C contrib/btrfs/collectd_btrfs.conf -f
