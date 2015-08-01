AC_DEFUN([COLLECTD_WITH_LIBLVM2APP], [
	with_liblvm2app_cppflags=""
	with_liblvm2app_ldflags=""
	AC_ARG_WITH(liblvm2app, [AS_HELP_STRING([--with-liblvm2app@<:@=PREFIX@:>@], [Path to liblvm2app.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_liblvm2app_cppflags="-I$withval/include"
			with_liblvm2app_ldflags="-L$withval/lib"
			with_liblvm2app="yes"
		else
			with_liblvm2app="$withval"
		fi
	],
	[
		with_liblvm2app="yes"
	])
	if test "x$with_liblvm2app" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_liblvm2app_cppflags"

		AC_CHECK_HEADERS(lvm2app.h, [with_liblvm2app="yes"], [with_liblvm2app="no (lvm2app.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi

	if test "x$with_liblvm2app" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_liblvm2app_cppflags"
		LDFLAGS="$LDFLAGS $with_liblvm2app_ldflags"

		AC_CHECK_LIB(lvm2app, lvm_lv_get_property, [with_liblvm2app="yes"], [with_liblvm2app="no (Symbol 'lvm_lv_get_property' not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_liblvm2app" = "xyes"
	then
		BUILD_WITH_LIBLVM2APP_CPPFLAGS="$with_liblvm2app_cppflags"
		BUILD_WITH_LIBLVM2APP_LDFLAGS="$with_liblvm2app_ldflags"
		BUILD_WITH_LIBLVM2APP_LIBS="-llvm2app"
		AC_SUBST(BUILD_WITH_LIBLVM2APP_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBLVM2APP_LDFLAGS)
		AC_SUBST(BUILD_WITH_LIBLVM2APP_LIBS)
		AC_DEFINE(HAVE_LIBLVM2APP, 1, [Define if liblvm2app is present and usable.])
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBLVM2APP, test "x$with_liblvm2app" = "xyes")
])
