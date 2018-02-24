#!/bin/sh

GLOBAL_ERROR_INDICATOR=0

check_for_application()
{
    for PROG in "$@"
    do
        which "$PROG" >/dev/null 2>&1
        if test $? -ne 0; then
            cat >&2 <<EOF
WARNING: \`$PROG' not found!
    Please make sure that \`$PROG' is installed and is in one of the
    directories listed in the PATH environment variable.
EOF
            GLOBAL_ERROR_INDICATOR=1
        fi
    done
}

check_for_application lex bison autoheader aclocal automake autoconf pkg-config

libtoolize=""
libtoolize --version >/dev/null 2>/dev/null
if test $? -eq 0; then
    libtoolize=libtoolize
else
    glibtoolize --version >/dev/null 2>/dev/null
    if test $? -eq 0; then
        libtoolize=glibtoolize
    else
        cat >&2 <<EOF
WARNING: Neither \`libtoolize' nor \`glibtoolize' have been found!
    Please make sure that one of them is installed and is in one of the
    directories listed in the PATH environment variable.
EOF
        GLOBAL_ERROR_INDICATOR=1
    fi
 fi

if test "$GLOBAL_ERROR_INDICATOR" != "0"; then
    exit 1
fi

set -x

autoheader \
&& aclocal -I m4 \
&& $libtoolize --copy --force \
&& automake --add-missing --copy \
&& autoconf
