#! /bin/bash
#
# collectd -- testwrapper.sh
#
# A wrapper script for running tests. If valgrind is available, memory
# checking will be enabled for all tests.

set -e

MEMCHECK=""

if test -n "$VALGRIND"; then
	MEMCHECK="$VALGRIND --quiet --tool=memcheck --error-exitcode=1"
	MEMCHECK="$MEMCHECK --trace-children=yes"
	MEMCHECK="$MEMCHECK --leak-check=full"
	MEMCHECK="$MEMCHECK --gen-suppressions=all"
fi

exec $MEMCHECK "$@"

# vim: set tw=78 sw=4 ts=4 noexpandtab :

