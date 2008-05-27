#! /bin/sh

if ! which lex > /dev/null 2>&1; then
	echo "WARNING: lex not found!" >&2
	echo "Make sure that you have a flex compatible tool available." >&2
fi

if ! which yacc > /dev/null 2>&1; then
	echo "WARNING: yacc not found!" >&2
	echo "Make sure that you have a GNU bison compatible tool available." >&2
fi

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
