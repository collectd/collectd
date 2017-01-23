#!/bin/sh

DEFAULT_VERSION="5.7.1.git"

if [ -d .git ]; then
	VERSION="`git describe --dirty=+ --abbrev=7 2> /dev/null | grep collectd | sed -e 's/^collectd-//' -e 's/-/./g'`"
fi

if test -z "$VERSION"; then
	VERSION="$DEFAULT_VERSION"
fi

printf "%s" "$VERSION"
