#! /bin/sh

GLOBAL_ERROR_INDICATOR=0

check_for_application ()
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

check_for_application lex yacc autoheader aclocal automake autoconf

# Actually we don't need the pkg-config executable, but we need the M4 macros.
# We check for `pkg-config' here and hope that M4 macros will then be
# available, too.
check_for_application pkg-config

libtoolize=""
libtoolize --version >/dev/null 2>/dev/null
if test $? -eq 0
then
	libtoolize=libtoolize
else
	glibtoolize --version >/dev/null 2>/dev/null
	if test $? -eq 0
	then
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

if test "$GLOBAL_ERROR_INDICATOR" != "0"
then
	exit 1
fi

set -x

autoheader \
&& aclocal \
&& $libtoolize --ltdl --copy --force \
&& automake --add-missing --copy \
&& autoconf
