AC_DEFUN([COLLECTD_WITH_LIBVARNISH], [
	with_libvarnish_cppflags=""
	with_libvarnish_cflags=""
	with_libvarnish_libs=""
	AC_ARG_WITH(libvarnish, [AS_HELP_STRING([--with-libvarnish@<:@=PREFIX@:>@], [Path to libvarnish.])],
	[
		if test "x$withval" = "xno"
		then
			with_libvarnish="no"
		else if test "x$withval" = "xyes"
		then
			with_libvarnish="use_pkgconfig"
		else if test -d "$with_libvarnish/lib"
		then
			AC_MSG_NOTICE([Not checking for libvarnish: Manually configured])
			with_libvarnish_cflags="-I$withval/include"
			with_libvarnish_libs="-L$withval/lib -lvarnishapi"
			with_libvarnish="yes"
		fi; fi; fi
	],
	[with_libvarnish="use_pkgconfig"])

	# configure using pkg-config
	if test "x$with_libvarnish" = "xuse_pkgconfig"
	then
		if test "x$PKG_CONFIG" = "x"
		then
			with_libvarnish="no (Don't have pkg-config)"
		fi
	fi
	if test "x$with_libvarnish" = "xuse_pkgconfig"
	then
		AC_MSG_NOTICE([Checking for varnishapi using $PKG_CONFIG])
		$PKG_CONFIG --exists 'varnishapi' 2>/dev/null
		if test $? -ne 0
		then
			with_libvarnish="no (pkg-config doesn't know varnishapi)"
		fi
	fi
	if test "x$with_libvarnish" = "xuse_pkgconfig"
	then
		with_libvarnish_cflags="`$PKG_CONFIG --cflags 'varnishapi'`"
		if test $? -ne 0
		then
			with_libvarnish="no ($PKG_CONFIG failed)"
		fi
		with_libvarnish_libs="`$PKG_CONFIG --libs 'varnishapi'`"
		if test $? -ne 0
		then
			with_libvarnish="no ($PKG_CONFIG failed)"
		fi
	fi
	if test "x$with_libvarnish" = "xuse_pkgconfig"
	then
		with_libvarnish="yes"
	fi

	# with_libvarnish_cflags and with_libvarnish_libs are set up now, let's do
	# the actual checks.
	if test "x$with_libvarnish" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"

		CPPFLAGS="$CPPFLAGS $with_libvarnish_cflags"

		AC_CHECK_HEADERS(varnish/vapi/vsc.h,
			[AC_DEFINE([HAVE_VARNISH_V4], [1], [Varnish 4 API support])],
			[AC_CHECK_HEADERS(varnish/vsc.h,
				[AC_DEFINE([HAVE_VARNISH_V3], [1], [Varnish 3 API support])],
				[AC_CHECK_HEADERS(varnish/varnishapi.h,
					[AC_DEFINE([HAVE_VARNISH_V2], [1], [Varnish 2 API support])],
					[with_libvarnish="no (found none of the varnish header files)"])])])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libvarnish" = "xyes"
	then
		BUILD_WITH_LIBVARNISH_CFLAGS="$with_libvarnish_cflags"
		BUILD_WITH_LIBVARNISH_LIBS="$with_libvarnish_libs"
		AC_SUBST(BUILD_WITH_LIBVARNISH_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBVARNISH_LIBS)
	fi
])
