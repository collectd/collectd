AC_DEFUN([COLLECTD_WITH_LIBATASMART], [
	with_libatasmart_cppflags=""
	with_libatasmart_ldflags=""
	AC_ARG_WITH(libatasmart, [AS_HELP_STRING([--with-libatasmart@<:@=PREFIX@:>@], [Path to libatasmart.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_libatasmart_cppflags="-I$withval/include"
			with_libatasmart_ldflags="-L$withval/lib"
			with_libatasmart="yes"
		else
			with_libatasmart="$withval"
		fi
	],
	[
		if test "x$ac_system" = "xLinux"
		then
			with_libatasmart="yes"
		else
			with_libatasmart="no (Linux only library)"
		fi
	])
	if test "x$with_libatasmart" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libatasmart_cppflags"

		AC_CHECK_HEADERS(atasmart.h, [with_libatasmart="yes"], [with_libatasmart="no (atasmart.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libatasmart" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libatasmart_cppflags"
		LDFLAGS="$LDFLAGS $with_libatasmart_ldflags"

		AC_CHECK_LIB(atasmart, sk_disk_open, [with_libatasmart="yes"], [with_libatasmart="no (Symbol 'sk_disk_open' not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libatasmart" = "xyes"
	then
		BUILD_WITH_LIBATASMART_CPPFLAGS="$with_libatasmart_cppflags"
		BUILD_WITH_LIBATASMART_LDFLAGS="$with_libatasmart_ldflags"
		BUILD_WITH_LIBATASMART_LIBS="-latasmart"
		AC_SUBST(BUILD_WITH_LIBATASMART_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBATASMART_LDFLAGS)
		AC_SUBST(BUILD_WITH_LIBATASMART_LIBS)
		AC_DEFINE(HAVE_LIBATASMART, 1, [Define if libatasmart is present and usable.])
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBATASMART, test "x$with_libatasmart" = "xyes")
])
