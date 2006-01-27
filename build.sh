#! /bin/sh

set -x

autoheader \
&& aclocal \
&& libtoolize --ltdl --copy --force \
&& automake --add-missing --copy \
&& autoconf
