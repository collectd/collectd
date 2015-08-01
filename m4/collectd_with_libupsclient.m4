AC_DEFUN([COLLECTD_WITH_LIBUPSCLIENT], [
	with_libupsclient_config=""
	with_libupsclient_cflags=""
	with_libupsclient_libs=""
	AC_ARG_WITH(libupsclient, [AS_HELP_STRING([--with-libupsclient@<:@=PREFIX@:>@], [Path to the upsclient library.])],
	[
		if test "x$withval" = "xno"
		then
			with_libupsclient="no"
		else if test "x$withval" = "xyes"
		then
			with_libupsclient="use_pkgconfig"
		else
			if test -x "$withval"
			then
				with_libupsclient_config="$withval"
				with_libupsclient="use_libupsclient_config"
			else if test -x "$withval/bin/libupsclient-config"
			then
				with_libupsclient_config="$withval/bin/libupsclient-config"
				with_libupsclient="use_libupsclient_config"
			else
				AC_MSG_NOTICE([Not checking for libupsclient: Manually configured])
				with_libupsclient_cflags="-I$withval/include"
				with_libupsclient_libs="-L$withval/lib -lupsclient"
				with_libupsclient="yes"
			fi; fi
		fi; fi
	],
	[with_libupsclient="use_pkgconfig"])

	# configure using libupsclient-config
	if test "x$with_libupsclient" = "xuse_libupsclient_config"
	then
		AC_MSG_NOTICE([Checking for libupsclient using $with_libupsclient_config])
		with_libupsclient_cflags="`$with_libupsclient_config --cflags`"
		if test $? -ne 0
		then
			with_libupsclient="no ($with_libupsclient_config failed)"
		fi
		with_libupsclient_libs="`$with_libupsclient_config --libs`"
		if test $? -ne 0
		then
			with_libupsclient="no ($with_libupsclient_config failed)"
		fi
	fi
	if test "x$with_libupsclient" = "xuse_libupsclient_config"
	then
		with_libupsclient="yes"
	fi

	# configure using pkg-config
	if test "x$with_libupsclient" = "xuse_pkgconfig"
	then
		if test "x$PKG_CONFIG" = "x"
		then
			with_libupsclient="no (Don't have pkg-config)"
		fi
	fi
	if test "x$with_libupsclient" = "xuse_pkgconfig"
	then
		AC_MSG_NOTICE([Checking for libupsclient using $PKG_CONFIG])
		$PKG_CONFIG --exists 'libupsclient' 2>/dev/null
		if test $? -ne 0
		then
			with_libupsclient="no (pkg-config doesn't know libupsclient)"
		fi
	fi
	if test "x$with_libupsclient" = "xuse_pkgconfig"
	then
		with_libupsclient_cflags="`$PKG_CONFIG --cflags 'libupsclient'`"
		if test $? -ne 0
		then
			with_libupsclient="no ($PKG_CONFIG failed)"
		fi
		with_libupsclient_libs="`$PKG_CONFIG --libs 'libupsclient'`"
		if test $? -ne 0
		then
			with_libupsclient="no ($PKG_CONFIG failed)"
		fi
	fi
	if test "x$with_libupsclient" = "xuse_pkgconfig"
	then
		with_libupsclient="yes"
	fi

	# with_libupsclient_cflags and with_libupsclient_libs are set up now, let's do
	# the actual checks.
	if test "x$with_libupsclient" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"

		AC_CHECK_HEADERS(upsclient.h, [], [with_libupsclient="no (upsclient.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libupsclient" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"

		CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"
		LDFLAGS="$LDFLAGS $with_libupsclient_libs"

		AC_CHECK_LIB(upsclient, upscli_connect,
			     [with_libupsclient="yes"],
			     [with_libupsclient="no (symbol upscli_connect not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libupsclient" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"

		AC_CHECK_TYPES([UPSCONN_t, UPSCONN], [], [],
	[#include <stdlib.h>
	#include <stdio.h>
	#include <upsclient.h>])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libupsclient" = "xyes"
	then
		BUILD_WITH_LIBUPSCLIENT_CFLAGS="$with_libupsclient_cflags"
		BUILD_WITH_LIBUPSCLIENT_LIBS="$with_libupsclient_libs"
		AC_SUBST(BUILD_WITH_LIBUPSCLIENT_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBUPSCLIENT_LIBS)
	fi
])
