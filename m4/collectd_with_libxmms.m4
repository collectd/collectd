AC_DEFUN([COLLECTD_WITH_LIBXMMS], [
	with_xmms_config="xmms-config"
	with_xmms_cflags=""
	with_xmms_libs=""
	AC_ARG_WITH(libxmms, [AS_HELP_STRING([--with-libxmms@<:@=PREFIX@:>@], [Path to libxmms.])],
	[
		if test "x$withval" != "xno" \
			&& test "x$withval" != "xyes"
		then
			if test -f "$withval" && test -x "$withval";
			then
				with_xmms_config="$withval"
			else if test -x "$withval/bin/xmms-config"
			then
				with_xmms_config="$withval/bin/xmms-config"
			fi; fi
			with_libxmms="yes"
		else if test "x$withval" = "xno"
		then
			with_libxmms="no"
		else
			with_libxmms="yes"
		fi; fi
	],
	[
		with_libxmms="yes"
	])
	if test "x$with_libxmms" = "xyes"
	then
		with_xmms_cflags=`$with_xmms_config --cflags 2>/dev/null`
		xmms_config_status=$?

		if test $xmms_config_status -ne 0
		then
			with_libxmms="no"
		fi
	fi
	if test "x$with_libxmms" = "xyes"
	then
		with_xmms_libs=`$with_xmms_config --libs 2>/dev/null`
		xmms_config_status=$?

		if test $xmms_config_status -ne 0
		then
			with_libxmms="no"
		fi
	fi
	if test "x$with_libxmms" = "xyes"
	then
		AC_CHECK_LIB(xmms, xmms_remote_get_info,
		[
			BUILD_WITH_LIBXMMS_CFLAGS="$with_xmms_cflags"
			BUILD_WITH_LIBXMMS_LIBS="$with_xmms_libs"
			AC_SUBST(BUILD_WITH_LIBXMMS_CFLAGS)
			AC_SUBST(BUILD_WITH_LIBXMMS_LIBS)
		],
		[
			with_libxmms="no"
		],
		[$with_xmms_libs])
	fi
	with_libxmms_numeric=0
	if test "x$with_libxmms" = "xyes"
	then
		with_libxmms_numeric=1
	fi
	AC_DEFINE_UNQUOTED(HAVE_LIBXMMS, [$with_libxmms_numeric], [Define to 1 if you have the 'xmms' library (-lxmms).])
	AM_CONDITIONAL(BUILD_WITH_LIBXMMS, test "x$with_libxmms" = "xyes")
])
