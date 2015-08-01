AC_DEFUN([COLLECTD_WITH_LIBVIRT], [
	with_libvirt="no (pkg-config isn't available)"
	with_libvirt_cflags=""
	with_libvirt_ldflags=""
	if test "x$PKG_CONFIG" != "x"
	then
		$PKG_CONFIG --exists libvirt 2>/dev/null
		if test "$?" = "0"
		then
			with_libvirt="yes"
		else
			with_libvirt="no (pkg-config doesn't know libvirt)"
		fi
	fi
	if test "x$with_libvirt" = "xyes"
	then
		with_libvirt_cflags="`$PKG_CONFIG --cflags libvirt`"
		if test $? -ne 0
		then
			with_libvirt="no"
		fi
		with_libvirt_ldflags="`$PKG_CONFIG --libs libvirt`"
		if test $? -ne 0
		then
			with_libvirt="no"
		fi
	fi
	if test "x$with_libvirt" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libvirt_cflags"

		AC_CHECK_HEADERS(libvirt/libvirt.h, [],
			      [with_libvirt="no (libvirt/libvirt.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libvirt" = "xyes"
	then
		SAVE_CFLAGS="$CFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"

		CFLAGS="$CFLAGS $with_libvirt_cflags"
		LDFLAGS="$LDFLAGS $with_libvirt_ldflags"

		AC_CHECK_LIB(virt, virDomainBlockStats,
			     [with_libvirt="yes"],
			     [with_libvirt="no (symbol virDomainBlockStats not found)"])

		CFLAGS="$SAVE_CFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	dnl Add the right compiler flags and libraries.
	if test "x$with_libvirt" = "xyes"; then
		BUILD_WITH_LIBVIRT_CFLAGS="$with_libvirt_cflags"
		BUILD_WITH_LIBVIRT_LIBS="$with_libvirt_ldflags"
		AC_SUBST(BUILD_WITH_LIBVIRT_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBVIRT_LIBS)
	fi
])
