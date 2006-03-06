#!/bin/sh
#
# collectd	Initscript for collectd
#		http://verplant.org/collectd/
# Author:	Florian Forster <octo@verplant.org>
# Extended to support multiple running instances of collectd:
#		Sebastian Harl <sh@tokkee.org>
#

set -e

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DESC="Statistics collection daemon"
NAME=collectd
DAEMON=/usr/sbin/$NAME
SCRIPTNAME=/etc/init.d/$NAME
ARGS=""

CONFIGDIR=/etc/collectd
# for backward compatibility
FALLBACKCONF=/etc/collectd.conf

# Gracefully exit if the package has been removed.
test -x $DAEMON || exit 0

if [ -r /etc/default/$NAME ]
then
	. /etc/default/$NAME
fi

#
#	Function that starts the daemon/service.
#
d_start() {
	i=1
	
	if [[ ! -d "$CONFIGDIR" && -e "$FALLBACKCONF" ]]
	then
		start-stop-daemon --start --quiet --exec $DAEMON \
			-- -C "$FALLBACKCONF"
	else
		echo -n " ("
		for CONFIG in `cd $CONFIGDIR; ls *.conf 2> /dev/null`; do
			CONF="$CONFIGDIR/$CONFIG"
			NAME=${CONFIG%%.conf}
			PIDFILE=$( grep PIDFile $CONF | awk '{print $2}' )

			if [ 1 != $i ]; then
				echo -n " "
			fi

			start-stop-daemon --start --quiet \
				--pidfile $PIDFILE --startas $DAEMON \
				-- -C "$CONFIGDIR/$CONFIG"
			echo -n "$NAME"

			let i++
		done
		echo -n ")"
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
