AC_DEFUN([COLLECTD_WITH_LIBYAJL], [
	with_libyajl_cppflags=""
	with_libyajl_ldflags=""
	AC_ARG_WITH(libyajl, [AS_HELP_STRING([--with-libyajl@<:@=PREFIX@:>@], [Path to libyajl.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_libyajl_cppflags="-I$withval/include"
			with_libyajl_ldflags="-L$withval/lib"
			with_libyajl="yes"
		else
			with_libyajl="$withval"
		fi
	],
	[
		with_libyajl="yes"
	])
	if test "x$with_libyajl" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libyajl_cppflags"

		AC_CHECK_HEADERS(yajl/yajl_parse.h, [with_libyajl="yes"], [with_libyajl="no (yajl/yajl_parse.h not found)"])
		AC_CHECK_HEADERS(yajl/yajl_version.h)

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libyajl" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libyajl_cppflags"
		LDFLAGS="$LDFLAGS $with_libyajl_ldflags"

		AC_CHECK_LIB(yajl, yajl_alloc, [with_libyajl="yes"], [with_libyajl="no (Symbol 'yajl_alloc' not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libyajl" = "xyes"
	then
		BUILD_WITH_LIBYAJL_CPPFLAGS="$with_libyajl_cppflags"
		BUILD_WITH_LIBYAJL_LDFLAGS="$with_libyajl_ldflags"
		BUILD_WITH_LIBYAJL_LIBS="-lyajl"
		AC_SUBST(BUILD_WITH_LIBYAJL_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBYAJL_LDFLAGS)
		AC_SUBST(BUILD_WITH_LIBYAJL_LIBS)
		AC_DEFINE(HAVE_LIBYAJL, 1, [Define if libyajl is present and usable.])
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBYAJL, test "x$with_libyajl" = "xyes")
])
