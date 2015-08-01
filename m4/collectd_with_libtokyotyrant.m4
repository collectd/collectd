AC_DEFUN([COLLECTD_WITH_LIBTOKYOTYRANT], [
	with_libtokyotyrant_cppflags=""
	with_libtokyotyrant_ldflags=""
	with_libtokyotyrant_libs=""
	AC_ARG_WITH(libtokyotyrant, [AS_HELP_STRING([--with-libtokyotyrant@<:@=PREFIX@:>@], [Path to libtokyotyrant.])],
	[
	  if test "x$withval" = "xno"
	  then
	    with_libtokyotyrant="no"
	  else if test "x$withval" = "xyes"
	  then
	    with_libtokyotyrant="yes"
	  else
	    with_libtokyotyrant_cppflags="-I$withval/include"
	    with_libtokyotyrant_ldflags="-L$withval/include"
	    with_libtokyotyrant_libs="-ltokyotyrant"
	    with_libtokyotyrant="yes"
	  fi; fi
	],
	[
	  with_libtokyotyrant="yes"
	])

	if test "x$with_libtokyotyrant" = "xyes"
	then
	  if $PKG_CONFIG --exists tokyotyrant
	  then
	    with_libtokyotyrant_cppflags="$with_libtokyotyrant_cppflags `$PKG_CONFIG --cflags tokyotyrant`"
	    with_libtokyotyrant_ldflags="$with_libtokyotyrant_ldflags `$PKG_CONFIG --libs-only-L tokyotyrant`"
	    with_libtokyotyrant_libs="$with_libtokyotyrant_libs `$PKG_CONFIG --libs-only-l tokyotyrant`"
	  fi
	fi

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libtokyotyrant_cppflags"
	LDFLAGS="$LDFLAGS $with_libtokyotyrant_ldflags"

	if test "x$with_libtokyotyrant" = "xyes"
	then
	  AC_CHECK_HEADERS(tcrdb.h,
	  [
		  AC_DEFINE(HAVE_TCRDB_H, 1,
			    [Define to 1 if you have the <tcrdb.h> header file.])
	  ], [with_libtokyotyrant="no (tcrdb.h not found)"])
	fi

	if test "x$with_libtokyotyrant" = "xyes"
	then
	  AC_CHECK_LIB(tokyotyrant, tcrdbrnum,
	  [
		  AC_DEFINE(HAVE_LIBTOKYOTYRANT, 1,
			    [Define to 1 if you have the tokyotyrant library (-ltokyotyrant).])
	  ],
	  [with_libtokyotyrant="no (symbol tcrdbrnum not found)"],
	  [$with_libtokyotyrant_libs])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libtokyotyrant" = "xyes"
	then
	  BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS="$with_libtokyotyrant_cppflags"
	  BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS="$with_libtokyotyrant_ldflags"
	  BUILD_WITH_LIBTOKYOTYRANT_LIBS="$with_libtokyotyrant_libs"
	  AC_SUBST(BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS)
	  AC_SUBST(BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS)
	  AC_SUBST(BUILD_WITH_LIBTOKYOTYRANT_LIBS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBTOKYOTYRANT, test "x$with_libtokyotyrant" = "xyes")
])
