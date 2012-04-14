#!/usr/bin/env bash

DEFAULT_VERSION="5.1.0.git"

VERSION="`git describe 2> /dev/null | sed -e 's/^collectd-//'`"

if test -z "$VERSION"; then
	VERSION="$DEFAULT_VERSION"
fi

VERSION="`echo \"$VERSION\" | sed -e 's/-/./g'`"

if test "x`uname -s`" = "xAIX" ; then
	echo "$VERSION\c"
else 
	echo -n "$VERSION"
fi
