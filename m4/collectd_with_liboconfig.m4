AC_DEFUN([COLLECTD_WITH_LIBOCONFIG], [
	with_own_liboconfig="no"
	liboconfig_LDFLAGS="$LDFLAGS"
	liboconfig_CPPFLAGS="$CPPFLAGS"
	AC_ARG_WITH(liboconfig, [AS_HELP_STRING([--with-liboconfig@<:@=PREFIX@:>@], [Path to liboconfig.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			if test -d "$withval/lib"
			then
				liboconfig_LDFLAGS="$LDFLAGS -L$withval/lib"
			fi
			if test -d "$withval/include"
			then
				liboconfig_CPPFLAGS="$CPPFLAGS -I$withval/include"
			fi
		fi
		if test "x$withval" = "xno"
		then
			AC_MSG_ERROR("liboconfig is required")
		fi
	],
	[
		with_liboconfig="yes"
	])

	save_LDFLAGS="$LDFLAGS"
	save_CPPFLAGS="$CPPFLAGS"
	LDFLAGS="$liboconfig_LDFLAGS"
	CPPFLAGS="$liboconfig_CPPFLAGS"
	AC_CHECK_LIB(oconfig, oconfig_parse_fh,
	[
		with_liboconfig="yes"
		with_own_liboconfig="no"
	],
	[
		with_liboconfig="yes"
		with_own_liboconfig="yes"
		LDFLAGS="$save_LDFLAGS"
		CPPFLAGS="$save_CPPFLAGS"
	])

	AM_CONDITIONAL(BUILD_WITH_OWN_LIBOCONFIG, test "x$with_own_liboconfig" = "xyes")
	if test "x$with_own_liboconfig" = "xyes"
	then
		with_liboconfig="yes (shipped version)"
	fi
])
