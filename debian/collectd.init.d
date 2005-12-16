#!/bin/sh
#
# collectd	Initscript for collectd
#		http://verplant.org/collectd/
# Author:	Florian Forster <octo@verplant.org>
#

set -e

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DESC="Statistics collection daemon"
NAME=collectd
DAEMON=/usr/sbin/$NAME
SCRIPTNAME=/etc/init.d/$NAME
ARGS=""

# Gracefully exit if the package has been removed.
test -x $DAEMON || exit 0

if [ -r /etc/default/$NAME ]
then
	. /etc/default/$NAME
fi

if [ -n "$DATA_DIR" ]; then
	ARGS="-D $DATA_DIR"
fi

if [ -n "$PING_HOST" ]; then
	for HOST in $PING_HOST
	do
		ARGS="$ARGS -p $HOST"
	done
fi

#
#	Function that starts the daemon/service.
#
d_start() {
	if [ "x$START_SERVER" = "xyes" ]
	then
		$DAEMON -s $ARGS
	fi
	if [ "x$START_CLIENT" = "xyes" ]
	then
		$DAEMON -c $ARGS
	fi
	if [ "x$START_SERVER" != "xyes" -a "x$START_CLIENT" != "xyes" ]
	then
		start-stop-daemon --start --quiet --exec $DAEMON -- -l $ARGS
	fi
}

#
#	Function that stops the daemon/service.
#
d_stop() {
	start-stop-daemon --stop --quiet --exec $DAEMON
}

case "$1" in
  start)
	echo -n "Starting $DESC: $NAME"
	d_start
	echo "."
	;;
  stop)
	echo -n "Stopping $DESC: $NAME"
	d_stop
	echo "."
	;;
  restart|force-reload)
	echo -n "Restarting $DESC: $NAME"
	d_stop
	sleep 1
	d_start
	echo "."
	;;
  *)
	echo "Usage: $SCRIPTNAME {start|stop|restart|force-reload}" >&2
	exit 1
	;;
esac

exit 0

# vim:syntax=sh
