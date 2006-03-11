#!/bin/bash
#
# collectd	Initscript for collectd
#		http://verplant.org/collectd/
# Authors:	Florian Forster <octo@verplant.org>
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
	i=0
	
	if [ ! -d "$CONFIGDIR" -a -e "$FALLBACKCONF" ]
	then
		start-stop-daemon --start --quiet --exec $DAEMON \
			-- -C "$FALLBACKCONF"
	else
		for FILE in `ls $CONFIGDIR/*.conf 2>/dev/null`
		do
			NAME=`basename "$FILE" .conf`

			if [ $i == 0 ]
			then
				echo -n " (";
			else
				echo -n ", ";
			fi
			
			$DAEMON -C "$FILE" 2>/dev/null
			if [ $? == 0 ]
			then
				echo -n "$NAME";
			else
				echo -n "$NAME failed";
			fi

			i=$(($i+1))
		done

		if [ $i == 0 ]
		then
			echo -n "[no config found]";
		else
			echo -n ")"
		fi
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
