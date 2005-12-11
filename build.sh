#! /bin/sh

set -x

true \
&& autoheader --force \
&& aclocal --force \
&& libtoolize --ltdl --force --copy \
&& automake --add-missing --copy \
&& autoconf --force

