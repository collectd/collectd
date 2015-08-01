AC_DEFUN([COLLECTD_WITH_LIBAQUAERO5], [
	AC_ARG_WITH(libaquaero5, [AS_HELP_STRING([--with-libaquaero5@<:@=PREFIX@:>@], [Path to aquatools-ng source code.])],
	[
	 if test "x$withval" = "xyes"
	 then
		 with_libaquaero5="yes"
	 else if test "x$withval" = "xno"
	 then
		 with_libaquaero5="no"
	 else
		 with_libaquaero5="yes"
		 LIBAQUAERO5_CFLAGS="$LIBAQUAERO5_CFLAGS -I$withval/src"
		 LIBAQUAERO5_LDFLAGS="$LIBAQUAERO5_LDFLAGS -L$withval/obj"
	 fi; fi
	],
	[with_libaquaero5="yes"])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $LIBAQUAERO5_CFLAGS"
	LDFLAGS="$LDFLAGS $LIBAQUAERO5_LDFLAGS"

	if test "x$with_libaquaero5" = "xyes"
	then
		if test "x$LIBAQUAERO5_CFLAGS" != "x"
		then
			AC_MSG_NOTICE([libaquaero5 CPPFLAGS: $LIBAQUAERO5_CFLAGS])
		fi
		AC_CHECK_HEADERS(libaquaero5.h,
		[with_libaquaero5="yes"],
		[with_libaquaero5="no (libaquaero5.h not found)"])
	fi
	if test "x$with_libaquaero5" = "xyes"
	then
		if test "x$LIBAQUAERO5_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([libaquaero5 LDFLAGS: $LIBAQUAERO5_LDFLAGS])
		fi
		AC_CHECK_LIB(aquaero5, libaquaero5_poll,
		[with_libaquaero5="yes"],
		[with_libaquaero5="no (symbol 'libaquaero5_poll' not found)"])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libaquaero5" = "xyes"
	then
		BUILD_WITH_LIBAQUAERO5_CFLAGS="$LIBAQUAERO5_CFLAGS"
		BUILD_WITH_LIBAQUAERO5_LDFLAGS="$LIBAQUAERO5_LDFLAGS"
		AC_SUBST(BUILD_WITH_LIBAQUAERO5_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBAQUAERO5_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBAQUAERO5, test "x$with_libaquaero5" = "xyes")
])
