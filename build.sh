#! /bin/sh

libtoolize=libtoolize

if which glibtoolize > /dev/null 2>&1; then
	libtoolize=glibtoolize
fi

set -x

autoheader \
&& aclocal \
&& $libtoolize --ltdl --copy --force \
&& automake --add-missing --copy \
&& autoconf
