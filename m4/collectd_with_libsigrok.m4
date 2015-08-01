AC_DEFUN([COLLECTD_WITH_LIBSIGROK], [
	with_libsigrok_cflags=""
	with_libsigrok_ldflags=""
	AC_ARG_WITH(libsigrok, [AS_HELP_STRING([--with-libsigrok@<:@=PREFIX@:>@], [Path to libsigrok.])],
	[
		if test "x$withval" = "xno"
		then
			with_libsigrok="no"
		else
			with_libsigrok="yes"
			if test "x$withval" != "xyes"
			then
				with_libsigrok_cflags="-I$withval/include"
				with_libsigrok_ldflags="-L$withval/lib"
			fi
		fi
	],[with_libsigrok="yes"])

	# libsigrok has a glib dependency
	if test "x$with_libsigrok" = "xyes"
	then
	m4_ifdef([AM_PATH_GLIB_2_0],
		[
		 AM_PATH_GLIB_2_0([2.28.0],
			[with_libsigrok_cflags="$with_libsigrok_cflags $GLIB_CFLAGS"; with_libsigrok_ldflags="$with_libsigrok_ldflags $GLIB_LIBS"])
		],
		[
		 with_libsigrok="no (glib not available)"
		]
	)
	fi

	# libsigrok headers
	if test "x$with_libsigrok" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libsigrok_cflags"

		AC_CHECK_HEADERS(libsigrok/libsigrok.h, [], [with_libsigrok="no (libsigrok/libsigrok.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi

	# libsigrok library
	if test "x$with_libsigrok" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libsigrok_cflags"
		LDFLAGS="$LDFLAGS $with_libsigrok_ldflags"

		AC_CHECK_LIB(sigrok, sr_init,
		[
			AC_DEFINE(HAVE_LIBSIGROK, 1, [Define to 1 if you have the sigrok library (-lsigrok).])
		],
		[with_libsigrok="no (libsigrok not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libsigrok" = "xyes"
	then
		BUILD_WITH_LIBSIGROK_CFLAGS="$with_libsigrok_cflags"
		BUILD_WITH_LIBSIGROK_LDFLAGS="$with_libsigrok_ldflags"
		AC_SUBST(BUILD_WITH_LIBSIGROK_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBSIGROK_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBSIGROK, test "x$with_libsigrok" = "xyes")
])
