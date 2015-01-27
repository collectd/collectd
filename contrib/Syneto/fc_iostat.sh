#!/usr/bin/bash
RETRY_TIME=1800
ROOT=$(readlink -f `dirname $0`)
/usr/bin/sudo dtrace -l | grep "fc.*xfer-start" 1>/dev/null
if [ $? -eq 0 ]; then
	/usr/bin/logger -t fc_iostat_collectd_plugin -p daemon.warning "Fibre channel module loaded. Started monitoring."
	/usr/bin/sudo ${ROOT}/fc_iostat.d $COLLECTD_HOSTNAME
else
	/usr/bin/logger -t fc_iostat_collectd_plugin -p daemon.warning "No fibre channel module loaded. Will retry in $RETRY_TIME seconds."
	sleep $RETRY_TIME
fi
