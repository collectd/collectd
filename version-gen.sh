#!/usr/bin/env bash

DEFAULT_VERSION="5.4.1.git"

VERSION="`git describe 2> /dev/null | grep collectd | sed -e 's/^collectd-//'`"

if test -z "$VERSION"; then
	VERSION="$DEFAULT_VERSION"
fi

VERSION="`echo \"$VERSION\" | sed -e 's/-/./g'`"

echo -n "$VERSION"
