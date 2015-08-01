AC_DEFUN([COLLECTD_WITH_LIBGANGLIA], [
	AC_ARG_WITH(libganglia, [AS_HELP_STRING([--with-libganglia@<:@=PREFIX@:>@], [Path to libganglia.])],
	[
	 if test -f "$withval" && test -x "$withval"
	 then
		 with_libganglia_config="$withval"
		 with_libganglia="yes"
	 else if test -f "$withval/bin/ganglia-config" && test -x "$withval/bin/ganglia-config"
	 then
		 with_libganglia_config="$withval/bin/ganglia-config"
		 with_libganglia="yes"
	 else if test -d "$withval"
	 then
		 GANGLIA_CPPFLAGS="-I$withval/include"
		 GANGLIA_LDFLAGS="-L$withval/lib"
		 with_libganglia="yes"
	 else
		 with_libganglia_config="ganglia-config"
		 with_libganglia="$withval"
	 fi; fi; fi
	],
	[
	 with_libganglia_config="ganglia-config"
	 with_libganglia="yes"
	])

	if test "x$with_libganglia" = "xyes" && test "x$with_libganglia_config" != "x"
	then
		if test "x$GANGLIA_CPPFLAGS" = "x"
		then
			GANGLIA_CPPFLAGS=`"$with_libganglia_config" --cflags 2>/dev/null`
		fi

		if test "x$GANGLIA_LDFLAGS" = "x"
		then
			GANGLIA_LDFLAGS=`"$with_libganglia_config" --ldflags 2>/dev/null`
		fi

		if test "x$GANGLIA_LIBS" = "x"
		then
			GANGLIA_LIBS=`"$with_libganglia_config" --libs 2>/dev/null`
		fi
	fi

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $GANGLIA_CPPFLAGS"
	LDFLAGS="$LDFLAGS $GANGLIA_LDFLAGS"

	if test "x$with_libganglia" = "xyes"
	then
		AC_CHECK_HEADERS(gm_protocol.h,
		[
			AC_DEFINE(HAVE_GM_PROTOCOL_H, 1,
				  [Define to 1 if you have the <gm_protocol.h> header file.])
		], [with_libganglia="no (gm_protocol.h not found)"])
	fi

	if test "x$with_libganglia" = "xyes"
	then
		AC_CHECK_LIB(ganglia, xdr_Ganglia_value_msg,
		[
			AC_DEFINE(HAVE_LIBGANGLIA, 1,
				  [Define to 1 if you have the ganglia library (-lganglia).])
		], [with_libganglia="no (symbol xdr_Ganglia_value_msg not found)"])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	AC_SUBST(GANGLIA_CPPFLAGS)
	AC_SUBST(GANGLIA_LDFLAGS)
	AC_SUBST(GANGLIA_LIBS)
	AM_CONDITIONAL(BUILD_WITH_LIBGANGLIA, test "x$with_libganglia" = "xyes")
])
