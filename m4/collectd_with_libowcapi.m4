AC_DEFUN([COLLECTD_WITH_LIBOWCAPI], [
	with_libowcapi_cppflags=""
	with_libowcapi_libs="-lowcapi"
	AC_ARG_WITH(libowcapi, [AS_HELP_STRING([--with-libowcapi@<:@=PREFIX@:>@], [Path to libowcapi.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_libowcapi_cppflags="-I$withval/include"
			with_libowcapi_libs="-L$withval/lib -lowcapi"
			with_libowcapi="yes"
		else
			with_libowcapi="$withval"
		fi
	],
	[
		with_libowcapi="yes"
	])
	if test "x$with_libowcapi" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$with_libowcapi_cppflags"

		AC_CHECK_HEADERS(owcapi.h, [with_libowcapi="yes"], [with_libowcapi="no (owcapi.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libowcapi" = "xyes"
	then
		SAVE_LDFLAGS="$LDFLAGS"
		SAVE_CPPFLAGS="$CPPFLAGS"
		LDFLAGS="$with_libowcapi_libs"
		CPPFLAGS="$with_libowcapi_cppflags"

		AC_CHECK_LIB(owcapi, OW_get, [with_libowcapi="yes"], [with_libowcapi="no (libowcapi not found)"])

		LDFLAGS="$SAVE_LDFLAGS"
		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libowcapi" = "xyes"
	then
		BUILD_WITH_LIBOWCAPI_CPPFLAGS="$with_libowcapi_cppflags"
		BUILD_WITH_LIBOWCAPI_LIBS="$with_libowcapi_libs"
		AC_SUBST(BUILD_WITH_LIBOWCAPI_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBOWCAPI_LIBS)
	fi
])
