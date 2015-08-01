AC_DEFUN([COLLECTD_WITH_LIBROUTEROS], [
	AC_ARG_WITH(librouteros, [AS_HELP_STRING([--with-librouteros@<:@=PREFIX@:>@], [Path to librouteros.])],
	[
	 if test "x$withval" = "xyes"
	 then
		 with_librouteros="yes"
	 else if test "x$withval" = "xno"
	 then
		 with_librouteros="no"
	 else
		 with_librouteros="yes"
		 LIBROUTEROS_CPPFLAGS="$LIBROUTEROS_CPPFLAGS -I$withval/include"
		 LIBROUTEROS_LDFLAGS="$LIBROUTEROS_LDFLAGS -L$withval/lib"
	 fi; fi
	],
	[with_librouteros="yes"])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $LIBROUTEROS_CPPFLAGS"
	LDFLAGS="$LDFLAGS $LIBROUTEROS_LDFLAGS"

	if test "x$with_librouteros" = "xyes"
	then
		if test "x$LIBROUTEROS_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([librouteros CPPFLAGS: $LIBROUTEROS_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(routeros_api.h,
		[with_librouteros="yes"],
		[with_librouteros="no (routeros_api.h not found)"])
	fi
	if test "x$with_librouteros" = "xyes"
	then
		if test "x$LIBROUTEROS_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([librouteros LDFLAGS: $LIBROUTEROS_LDFLAGS])
		fi
		AC_CHECK_LIB(routeros, ros_interface,
		[with_librouteros="yes"],
		[with_librouteros="no (symbol 'ros_interface' not found)"])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_librouteros" = "xyes"
	then
		BUILD_WITH_LIBROUTEROS_CPPFLAGS="$LIBROUTEROS_CPPFLAGS"
		BUILD_WITH_LIBROUTEROS_LDFLAGS="$LIBROUTEROS_LDFLAGS"
		AC_SUBST(BUILD_WITH_LIBROUTEROS_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBROUTEROS_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBROUTEROS, test "x$with_librouteros" = "xyes")
])
