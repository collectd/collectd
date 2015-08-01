AC_DEFUN([COLLECTD_WITH_LIBMOSQUITTO], [
	with_libmosquitto_cppflags=""
	with_libmosquitto_libs="-lmosquitto"
	AC_ARG_WITH(libmosquitto, [AS_HELP_STRING([--with-libmosquitto@<:@=PREFIX@:>@], [Path to libmosquitto.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_libmosquitto_cppflags="-I$withval/include"
			with_libmosquitto_libs="-L$withval/lib -lmosquitto"
			with_libmosquitto="yes"
		else
			with_libmosquitto="$withval"
		fi
	],
	[
		with_libmosquitto="yes"
	])
	if test "x$with_libmosquitto" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$with_libmosquitto_cppflags"

		AC_CHECK_HEADERS(mosquitto.h, [with_libmosquitto="yes"], [with_libmosquitto="no (mosquitto.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libmosquitto" = "xyes"
	then
		SAVE_LDFLAGS="$LDFLAGS"
		SAVE_CPPFLAGS="$CPPFLAGS"
		LDFLAGS="$with_libmosquitto_libs"
		CPPFLAGS="$with_libmosquitto_cppflags"

		AC_CHECK_LIB(mosquitto, mosquitto_connect, [with_libmosquitto="yes"], [with_libmosquitto="no (libmosquitto not found)"])

		LDFLAGS="$SAVE_LDFLAGS"
		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libmosquitto" = "xyes"
	then
		BUILD_WITH_LIBMOSQUITTO_CPPFLAGS="$with_libmosquitto_cppflags"
		BUILD_WITH_LIBMOSQUITTO_LIBS="$with_libmosquitto_libs"
		AC_SUBST(BUILD_WITH_LIBMOSQUITTO_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBMOSQUITTO_LIBS)
	fi
])
