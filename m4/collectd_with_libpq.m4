AC_DEFUN([COLLECTD_WITH_LIBPQ], [
	with_pg_config="pg_config"
	with_libpq_includedir=""
	with_libpq_libdir=""
	with_libpq_cppflags=""
	with_libpq_ldflags=""
	AC_ARG_WITH(libpq, [AS_HELP_STRING([--with-libpq@<:@=PREFIX@:>@],
		[Path to libpq.])],
	[
		if test "x$withval" = "xno"
		then
			with_libpq="no"
		else if test "x$withval" = "xyes"
		then
			with_libpq="yes"
		else
			if test -f "$withval" && test -x "$withval";
			then
				with_pg_config="$withval"
			else if test -x "$withval/bin/pg_config"
			then
				with_pg_config="$withval/bin/pg_config"
			fi; fi
			with_libpq="yes"
		fi; fi
	],
	[
		with_libpq="yes"
	])
	if test "x$with_libpq" = "xyes"
	then
		with_libpq_includedir=`$with_pg_config --includedir 2> /dev/null`
		pg_config_status=$?

		if test $pg_config_status -eq 0
		then
			if test -n "$with_libpq_includedir"; then
				for dir in $with_libpq_includedir; do
					with_libpq_cppflags="$with_libpq_cppflags -I$dir"
				done
			fi
		else
			AC_MSG_WARN([$with_pg_config returned with status $pg_config_status])
		fi

		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libpq_cppflags"

		AC_CHECK_HEADERS(libpq-fe.h, [],
			[with_libpq="no (libpq-fe.h not found)"], [])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libpq" = "xyes"
	then
		with_libpq_libdir=`$with_pg_config --libdir 2> /dev/null`
		pg_config_status=$?

		if test $pg_config_status -eq 0
		then
			if test -n "$with_libpq_libdir"; then
				for dir in $with_libpq_libdir; do
					with_libpq_ldflags="$with_libpq_ldflags -L$dir"
				done
			fi
		else
			AC_MSG_WARN([$with_pg_config returned with status $pg_config_status])
		fi

		SAVE_LDFLAGS="$LDFLAGS"
		LDFLAGS="$LDFLAGS $with_libpq_ldflags"

		AC_CHECK_LIB(pq, PQconnectdb,
			[with_libpq="yes"],
			[with_libpq="no (symbol 'PQconnectdb' not found)"])

		AC_CHECK_LIB(pq, PQserverVersion,
			[with_libpq="yes"],
			[with_libpq="no (symbol 'PQserverVersion' not found)"])

		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libpq" = "xyes"
	then
		BUILD_WITH_LIBPQ_CPPFLAGS="$with_libpq_cppflags"
		BUILD_WITH_LIBPQ_LDFLAGS="$with_libpq_ldflags"
		AC_SUBST(BUILD_WITH_LIBPQ_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBPQ_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBPQ, test "x$with_libpq" = "xyes")
])
