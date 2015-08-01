AC_DEFUN([COLLECTD_WITH_LIBDBI], [
	with_libdbi_cppflags=""
	with_libdbi_ldflags=""
	AC_ARG_WITH(libdbi, [AS_HELP_STRING([--with-libdbi@<:@=PREFIX@:>@], [Path to libdbi.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_libdbi_cppflags="-I$withval/include"
			with_libdbi_ldflags="-L$withval/lib"
			with_libdbi="yes"
		else
			with_libdbi="$withval"
		fi
	],
	[
		with_libdbi="yes"
	])
	if test "x$with_libdbi" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libdbi_cppflags"

		AC_CHECK_HEADERS(dbi/dbi.h, [with_libdbi="yes"], [with_libdbi="no (dbi/dbi.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libdbi" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libdbi_cppflags"
		LDFLAGS="$LDFLAGS $with_libdbi_ldflags"

		AC_CHECK_LIB(dbi, dbi_initialize, [with_libdbi="yes"], [with_libdbi="no (Symbol 'dbi_initialize' not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libdbi" = "xyes"
	then
		BUILD_WITH_LIBDBI_CPPFLAGS="$with_libdbi_cppflags"
		BUILD_WITH_LIBDBI_LDFLAGS="$with_libdbi_ldflags"
		BUILD_WITH_LIBDBI_LIBS="-ldbi"
		AC_SUBST(BUILD_WITH_LIBDBI_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBDBI_LDFLAGS)
		AC_SUBST(BUILD_WITH_LIBDBI_LIBS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBDBI, test "x$with_libdbi" = "xyes")
])
