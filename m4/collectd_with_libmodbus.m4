AC_DEFUN([COLLECTD_WITH_LIBMODBUS], [
	with_libmodbus_config=""
	with_libmodbus_cflags=""
	with_libmodbus_libs=""
	AC_ARG_WITH(libmodbus, [AS_HELP_STRING([--with-libmodbus@<:@=PREFIX@:>@], [Path to the modbus library.])],
	[
		if test "x$withval" = "xno"
		then
			with_libmodbus="no"
		else if test "x$withval" = "xyes"
		then
			with_libmodbus="use_pkgconfig"
		else if test -d "$with_libmodbus/lib"
		then
			AC_MSG_NOTICE([Not checking for libmodbus: Manually configured])
			with_libmodbus_cflags="-I$withval/include"
			with_libmodbus_libs="-L$withval/lib -lmodbus"
			with_libmodbus="yes"
		fi; fi; fi
	],
	[with_libmodbus="use_pkgconfig"])

	# configure using pkg-config
	if test "x$with_libmodbus" = "xuse_pkgconfig"
	then
		if test "x$PKG_CONFIG" = "x"
		then
			with_libmodbus="no (Don't have pkg-config)"
		fi
	fi
	if test "x$with_libmodbus" = "xuse_pkgconfig"
	then
		AC_MSG_NOTICE([Checking for libmodbus using $PKG_CONFIG])
		$PKG_CONFIG --exists 'libmodbus' 2>/dev/null
		if test $? -ne 0
		then
			with_libmodbus="no (pkg-config doesn't know libmodbus)"
		fi
	fi
	if test "x$with_libmodbus" = "xuse_pkgconfig"
	then
		with_libmodbus_cflags="`$PKG_CONFIG --cflags 'libmodbus'`"
		if test $? -ne 0
		then
			with_libmodbus="no ($PKG_CONFIG failed)"
		fi
		with_libmodbus_libs="`$PKG_CONFIG --libs 'libmodbus'`"
		if test $? -ne 0
		then
			with_libmodbus="no ($PKG_CONFIG failed)"
		fi
	fi
	if test "x$with_libmodbus" = "xuse_pkgconfig"
	then
		with_libmodbus="yes"
	fi

	# with_libmodbus_cflags and with_libmodbus_libs are set up now, let's do
	# the actual checks.
	if test "x$with_libmodbus" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libmodbus_cflags"

		AC_CHECK_HEADERS(modbus/modbus.h, [], [with_libmodbus="no (modbus/modbus.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libmodbus" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"

		CPPFLAGS="$CPPFLAGS $with_libmodbus_cflags"
		LDFLAGS="$LDFLAGS $with_libmodbus_libs"

		AC_CHECK_LIB(modbus, modbus_connect,
			     [with_libmodbus="yes"],
			     [with_libmodbus="no (symbol modbus_connect not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libmodbus" = "xyes"
	then
		BUILD_WITH_LIBMODBUS_CFLAGS="$with_libmodbus_cflags"
		BUILD_WITH_LIBMODBUS_LIBS="$with_libmodbus_libs"
		AC_SUBST(BUILD_WITH_LIBMODBUS_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBMODBUS_LIBS)
	fi
])
