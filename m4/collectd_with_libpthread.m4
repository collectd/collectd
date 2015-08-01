AC_DEFUN([COLLECTD_WITH_LIBPTHREAD], [
	AC_ARG_WITH(libpthread, [AS_HELP_STRING([--with-libpthread=@<:@=PREFIX@:>@], [Path to libpthread.])],
	[	if test "x$withval" != "xno" \
			&& test "x$withval" != "xyes"
		then
			LDFLAGS="$LDFLAGS -L$withval/lib"
			CPPFLAGS="$CPPFLAGS -I$withval/include"
			with_libpthread="yes"
		else
			if test "x$withval" = "xno"
			then
				with_libpthread="no (disabled)"
			fi
		fi
	], [with_libpthread="yes"])
	if test "x$with_libpthread" = "xyes"
	then
		AC_CHECK_LIB(pthread, pthread_create, [with_libpthread="yes"], [with_libpthread="no (libpthread not found)"], [])
	fi

	if test "x$with_libpthread" = "xyes"
	then
		AC_CHECK_HEADERS(pthread.h,, [with_libpthread="no (pthread.h not found)"])
	fi
	if test "x$with_libpthread" = "xyes"
	then
		collect_pthread=1
	else
		collect_pthread=0
	fi
	AC_DEFINE_UNQUOTED(HAVE_LIBPTHREAD, [$collect_pthread],
		[Wether or not to use pthread (POSIX threads) library])
	AM_CONDITIONAL(BUILD_WITH_LIBPTHREAD, test "x$with_libpthread" = "xyes")
])
