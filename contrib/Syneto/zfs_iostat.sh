#!/usr/bin/bash
ROOT=$(readlink -f `dirname $0`)
/usr/bin/sudo ${ROOT}/zfs_iostat.d $COLLECTD_HOSTNAME