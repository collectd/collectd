#!/bin/sh

DEFAULT_VERSION="5.0.1.git"

VERSION="`git describe 2> /dev/null | sed -e 's/^collectd-//'`"

if test -z "$VERSION"; then
	VERSION="$DEFAULT_VERSION"
fi

VERSION="`echo \"$VERSION\" | sed -e 's/-/./g'`"

if test "x`uname -s`" = "xAIX" || test "x`uname -s`" = "xSunOS" ; then
	echo "$VERSION\c"
else 
	echo -n "$VERSION"
fi
