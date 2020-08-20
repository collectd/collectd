#!/usr/bin/bash
ROOT=$(readlink -f `dirname $0`)
/usr/bin/sudo ${ROOT}/zil_iostat.d $COLLECTD_HOSTNAME