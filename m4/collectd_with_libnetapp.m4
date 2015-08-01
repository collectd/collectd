AC_DEFUN([COLLECTD_WITH_LIBNETAPP], [
	AC_ARG_VAR([LIBNETAPP_CPPFLAGS], [C preprocessor flags required to build with libnetapp])
	AC_ARG_VAR([LIBNETAPP_LDFLAGS],  [Linker flags required to build with libnetapp])
	AC_ARG_VAR([LIBNETAPP_LIBS],     [Other libraries required to link against libnetapp])
	LIBNETAPP_CPPFLAGS="$LIBNETAPP_CPPFLAGS"
	LIBNETAPP_LDFLAGS="$LIBNETAPP_LDFLAGS"
	LIBNETAPP_LIBS="$LIBNETAPP_LIBS"
	AC_ARG_WITH(libnetapp, [AS_HELP_STRING([--with-libnetapp@<:@=PREFIX@:>@], [Path to libnetapp.])],
	[
	 if test -d "$withval"
	 then
		 LIBNETAPP_CPPFLAGS="$LIBNETAPP_CPPFLAGS -I$withval/include"
		 LIBNETAPP_LDFLAGS="$LIBNETAPP_LDFLAGS -L$withval/lib"
		 with_libnetapp="yes"
	 else
		 with_libnetapp="$withval"
	 fi
	],
	[
	 with_libnetapp="yes"
	])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $LIBNETAPP_CPPFLAGS"
	LDFLAGS="$LDFLAGS $LIBNETAPP_LDFLAGS"

	if test "x$with_libnetapp" = "xyes"
	then
		if test "x$LIBNETAPP_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([netapp CPPFLAGS: $LIBNETAPP_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(netapp_api.h,
			[with_libnetapp="yes"],
			[with_libnetapp="no (netapp_api.h not found)"])
	fi

	if test "x$with_libnetapp" = "xyes"
	then
		if test "x$LIBNETAPP_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([netapp LDFLAGS: $LIBNETAPP_LDFLAGS])
		fi

		if test "x$LIBNETAPP_LIBS" = "x"
		then
			LIBNETAPP_LIBS="-lpthread -lxml -ladt -lssl -lm -lcrypto -lz"
		fi
		AC_MSG_NOTICE([netapp LIBS: $LIBNETAPP_LIBS])

		AC_CHECK_LIB(netapp, na_server_invoke_elem,
			[with_libnetapp="yes"],
			[with_libnetapp="no (symbol na_server_invoke_elem not found)"],
			[$LIBNETAPP_LIBS])
		LIBNETAPP_LIBS="-lnetapp $LIBNETAPP_LIBS"
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libnetapp" = "xyes"
	then
		AC_DEFINE(HAVE_LIBNETAPP, 1, [Define to 1 if you have the netapp library (-lnetapp).])
	fi

	AC_SUBST(LIBNETAPP_CPPFLAGS)
	AC_SUBST(LIBNETAPP_LDFLAGS)
	AC_SUBST(LIBNETAPP_LIBS)
	AM_CONDITIONAL(BUILD_WITH_LIBNETAPP, test "x$with_libnetapp" = "xyes")
])
