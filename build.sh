#! /bin/sh

set -x

true \
&& aclocal --force \
&& libtoolize --ltdl --force --copy \
&& automake --add-missing --copy \
&& autoconf --force

