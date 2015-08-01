AC_DEFUN([COLLECTD_WITH_LIBOPING], [
	AC_ARG_WITH(liboping, [AS_HELP_STRING([--with-liboping@<:@=PREFIX@:>@], [Path to liboping.])],
	[
	 if test "x$withval" = "xyes"
	 then
		 with_liboping="yes"
	 else if test "x$withval" = "xno"
	 then
		 with_liboping="no"
	 else
		 with_liboping="yes"
		 LIBOPING_CPPFLAGS="$LIBOPING_CPPFLAGS -I$withval/include"
		 LIBOPING_LDFLAGS="$LIBOPING_LDFLAGS -L$withval/lib"
	 fi; fi
	],
	[with_liboping="yes"])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $LIBOPING_CPPFLAGS"
	LDFLAGS="$LDFLAGS $LIBOPING_LDFLAGS"

	if test "x$with_liboping" = "xyes"
	then
		if test "x$LIBOPING_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([liboping CPPFLAGS: $LIBOPING_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(oping.h,
		[with_liboping="yes"],
		[with_liboping="no (oping.h not found)"])
	fi
	if test "x$with_liboping" = "xyes"
	then
		if test "x$LIBOPING_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([liboping LDFLAGS: $LIBOPING_LDFLAGS])
		fi
		AC_CHECK_LIB(oping, ping_construct,
		[with_liboping="yes"],
		[with_liboping="no (symbol 'ping_construct' not found)"])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_liboping" = "xyes"
	then
		BUILD_WITH_LIBOPING_CPPFLAGS="$LIBOPING_CPPFLAGS"
		BUILD_WITH_LIBOPING_LDFLAGS="$LIBOPING_LDFLAGS"
		AC_SUBST(BUILD_WITH_LIBOPING_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBOPING_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBOPING, test "x$with_liboping" = "xyes")
])
