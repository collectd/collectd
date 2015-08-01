AC_DEFUN([COLLECTD_WITH_LIBXML2], [
	with_libxml2="no (pkg-config isn't available)"
	with_libxml2_cflags=""
	with_libxml2_ldflags=""
	if test "x$PKG_CONFIG" != "x"
	then
		$PKG_CONFIG --exists 'libxml-2.0' 2>/dev/null
		if test "$?" = "0"
		then
			with_libxml2="yes"
		else
			with_libxml2="no (pkg-config doesn't know libxml-2.0)"
		fi
	fi
	if test "x$with_libxml2" = "xyes"
	then
		with_libxml2_cflags="`$PKG_CONFIG --cflags libxml-2.0`"
		if test $? -ne 0
		then
			with_libxml2="no"
		fi
		with_libxml2_ldflags="`$PKG_CONFIG --libs libxml-2.0`"
		if test $? -ne 0
		then
			with_libxml2="no"
		fi
	fi
	if test "x$with_libxml2" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libxml2_cflags"

		AC_CHECK_HEADERS(libxml/parser.h, [],
			      [with_libxml2="no (libxml/parser.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libxml2" = "xyes"
	then
		SAVE_CFLAGS="$CFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"

		CFLAGS="$CFLAGS $with_libxml2_cflags"
		LDFLAGS="$LDFLAGS $with_libxml2_ldflags"

		AC_CHECK_LIB(xml2, xmlXPathEval,
			     [with_libxml2="yes"],
			     [with_libxml2="no (symbol xmlXPathEval not found)"])

		CFLAGS="$SAVE_CFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	dnl Add the right compiler flags and libraries.
	if test "x$with_libxml2" = "xyes"; then
		BUILD_WITH_LIBXML2_CFLAGS="$with_libxml2_cflags"
		BUILD_WITH_LIBXML2_LIBS="$with_libxml2_ldflags"
		AC_SUBST(BUILD_WITH_LIBXML2_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBXML2_LIBS)
	fi
])
