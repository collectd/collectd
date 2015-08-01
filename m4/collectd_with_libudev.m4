AC_DEFUN([COLLECTD_WITH_LIBUDEV], [
	with_libudev_cflags=""
	with_libudev_ldflags=""
	AC_ARG_WITH(libudev, [AS_HELP_STRING([--with-libudev@<:@=PREFIX@:>@], [Path to libudev.])],
	[
		if test "x$withval" = "xno"
		then
			with_libudev="no"
		else
			with_libudev="yes"
			if test "x$withval" != "xyes"
			then
				with_libudev_cflags="-I$withval/include"
				with_libudev_ldflags="-L$withval/lib"
				with_libudev="yes"
			fi
		fi
	],
	[
		if test "x$ac_system" = "xLinux"
		then
			with_libudev="yes"
		else
			with_libudev="no (Linux only library)"
		fi
	])
	if test "x$with_libudev" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libudev_cflags"

		AC_CHECK_HEADERS(libudev.h, [], [with_libudev="no (libudev.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libudev" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libudev_cflags"
		LDFLAGS="$LDFLAGS $with_libudev_ldflags"

		AC_CHECK_LIB(udev, udev_new,
		[
			AC_DEFINE(HAVE_LIBUDEV, 1, [Define to 1 if you have the udev library (-ludev).])
		],
		[with_libudev="no (libudev not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libudev" = "xyes"
	then
		BUILD_WITH_LIBUDEV_CFLAGS="$with_libudev_cflags"
		BUILD_WITH_LIBUDEV_LDFLAGS="$with_libudev_ldflags"
		AC_SUBST(BUILD_WITH_LIBUDEV_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBUDEV_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBUDEV, test "x$with_libudev" = "xyes")
])
