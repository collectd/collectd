#!/bin/sh

DEFAULT_VERSION="4.6.3.git"

VERSION="`git describe 2> /dev/null | sed -e 's/^collectd-//'`"

if test -z "$VERSION"; then
	VERSION="$DEFAULT_VERSION"
fi

VERSION="`echo \"$VERSION\" | sed -e 's/-/./g'`"

echo -n "$VERSION"
