#!/bin/bash

set -e

ENVFILE=/etc/collectd.env

if [ -f $ENVFILE ];
then
   . $ENVFILE
else
   TMPDIR=/tmp/collectd
   STRDIR=$HOME/.local/share/collectd
fi

# read parameters

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    exit 2
fi

for i in "$@"
do
case $i in
    --init)
	INIT="yes"
	;;
    --stop)
	STOP="yes"
	;;
    --sync)
	SYNC="yes"
	;;
    *)
	# unknown option
	echo "Unknown option" $i
	exit 1
    ;;
esac
done

###############

if [ "x$INIT" == "xyes" ]; then
    mkdir -p $TMPDIR

    # dir could be absent if its the first run
    if [ -d "$STRDIR" ]; then
	rsync -aI $STRDIR/ $TMPDIR/
    fi

    exit 0
fi

if [ "x$STOP" == "xyes" ]; then
    if [ -d "$TMPDIR" ]; then
	[[ -d $STRDIR ]] || mkdir $STRDIR
	rsync -aI $TMPDIR/ $STRDIR/
	rm -rf $TMPDIR
	exit 0
    else
	echo $TMPDIR does not exist, cannot sync it to storage
	exit 3
    fi
fi

if [ "x$SYNC" == "xyes" ]; then
    if [ -d "$TMPDIR" ]; then
	[[ -d $STRDIR ]] || mkdir $STRDIR
	rsync -aI $TMPDIR/ $STRDIR/
	exit 0
    fi
fi
