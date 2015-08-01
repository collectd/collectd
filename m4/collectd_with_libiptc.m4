AC_DEFUN([COLLECTD_WITH_LIBIPTC], [
	AC_ARG_WITH(libiptc, [AS_HELP_STRING([--with-libiptc@<:@=PREFIX@:>@], [Path to libiptc.])],
	[
		if test "x$withval" = "xyes"
		then
			with_libiptc="pkgconfig"
		else if test "x$withval" = "xno"
		then
			with_libiptc="no"
		else
			with_libiptc="yes"
			with_libiptc_cflags="-I$withval/include"
			with_libiptc_libs="-L$withval/lib"
		fi; fi
	],
	[
		if test "x$ac_system" = "xLinux"
		then
			with_libiptc="pkgconfig"
		else
			with_libiptc="no (Linux only)"
		fi
	])

	if test "x$with_libiptc" = "xpkgconfig" && test "x$PKG_CONFIG" = "x"
	then
		with_libiptc="no (Don't have pkg-config)"
	fi

	if test "x$with_libiptc" = "xpkgconfig"
	then
		$PKG_CONFIG --exists 'libiptc' 2>/dev/null
		if test $? -ne 0
		then
			with_libiptc="no (pkg-config doesn't know libiptc)"
		fi
	fi
	if test "x$with_libiptc" = "xpkgconfig"
	then
		with_libiptc_cflags="`$PKG_CONFIG --cflags 'libiptc'`"
		if test $? -ne 0
		then
			with_libiptc="no ($PKG_CONFIG failed)"
		fi
		with_libiptc_libs="`$PKG_CONFIG --libs 'libiptc'`"
		if test $? -ne 0
		then
			with_libiptc="no ($PKG_CONFIG failed)"
		fi
	fi

	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libiptc_cflags"

	# check whether the header file for libiptc is available.
	if test "x$with_libiptc" = "xpkgconfig"
	then
		AC_CHECK_HEADERS(libiptc/libiptc.h libiptc/libip6tc.h, ,
				[with_libiptc="no (header file missing)"])
	fi
	# If the header file is available, check for the required type declaractions.
	# They may be missing in old versions of libiptc. In that case, they will be
	# declared in the iptables plugin.
	if test "x$with_libiptc" = "xpkgconfig"
	then
		AC_CHECK_TYPES([iptc_handle_t, ip6tc_handle_t], [], [])
	fi
	# Check for the iptc_init symbol in the library.
	# This could be in iptc or ip4tc
	if test "x$with_libiptc" = "xpkgconfig"
	then
		SAVE_LIBS="$LIBS"
		AC_SEARCH_LIBS(iptc_init, [iptc ip4tc],
				[with_libiptc="pkgconfig"],
				[with_libiptc="no"],
				[$with_libiptc_libs])
		LIBS="$SAVE_LIBS"
	fi
	if test "x$with_libiptc" = "xpkgconfig"
	then
		with_libiptc="yes"
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"

	AM_CONDITIONAL(BUILD_WITH_LIBIPTC, test "x$with_libiptc" = "xyes")
	if test "x$with_libiptc" = "xyes"
	then
		BUILD_WITH_LIBIPTC_CPPFLAGS="$with_libiptc_cflags"
		BUILD_WITH_LIBIPTC_LDFLAGS="$with_libiptc_libs"
		AC_SUBST(BUILD_WITH_LIBIPTC_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBIPTC_LDFLAGS)
	fi
])
