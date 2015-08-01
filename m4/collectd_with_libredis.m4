AC_DEFUN([COLLECTD_WITH_LIBREDIS], [
	AC_ARG_WITH(libhiredis, [AS_HELP_STRING([--with-libhiredis@<:@=PREFIX@:>@],
	      [Path to libhiredis.])],
	[
	 if test "x$withval" = "xyes"
	 then
		 with_libhiredis="yes"
	 else if test "x$withval" = "xno"
	 then
		 with_libhiredis="no"
	 else
		 with_libhiredis="yes"
		 LIBHIREDIS_CPPFLAGS="$LIBHIREDIS_CPPFLAGS -I$withval/include"
		 LIBHIREDIS_LDFLAGS="$LIBHIREDIS_LDFLAGS -L$withval/lib"
	 fi; fi
	],
	[with_libhiredis="yes"])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $LIBHIREDIS_CPPFLAGS"
	LDFLAGS="$LDFLAGS $LIBHIREDIS_LDFLAGS"

	if test "x$with_libhiredis" = "xyes"
	then
		if test "x$LIBHIREDIS_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([libhiredis CPPFLAGS: $LIBHIREDIS_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(hiredis/hiredis.h,
		[with_libhiredis="yes"],
		[with_libhiredis="no (hiredis.h not found)"])
	fi
	if test "x$with_libhiredis" = "xyes"
	then
		if test "x$LIBHIREDIS_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([libhiredis LDFLAGS: $LIBHIREDIS_LDFLAGS])
		fi
		AC_CHECK_LIB(hiredis, redisCommand,
		[with_libhiredis="yes"],
		[with_libhiredis="no (symbol 'redisCommand' not found)"])

	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libhiredis" = "xyes"
	then
		BUILD_WITH_LIBHIREDIS_CPPFLAGS="$LIBHIREDIS_CPPFLAGS"
		BUILD_WITH_LIBHIREDIS_LDFLAGS="$LIBHIREDIS_LDFLAGS"
		AC_SUBST(BUILD_WITH_LIBHIREDIS_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBHIREDIS_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBHIREDIS, test "x$with_libhiredis" = "xyes")
])
