AC_DEFUN([COLLECTD_WITH_LIBSTATGRAB], [
	with_libstatgrab_cflags=""
	with_libstatgrab_ldflags=""
	AC_ARG_WITH(libstatgrab, [AS_HELP_STRING([--with-libstatgrab@<:@=PREFIX@:>@], [Path to libstatgrab.])],
	[
	 if test "x$withval" != "xno" \
	   && test "x$withval" != "xyes"
	 then
	   with_libstatgrab_cflags="-I$withval/include"
	   with_libstatgrab_ldflags="-L$withval/lib -lstatgrab"
	   with_libstatgrab="yes"
	   with_libstatgrab_pkg_config="no"
	 else
	   with_libstatgrab="$withval"
	   with_libstatgrab_pkg_config="yes"
	 fi
	 ],
	[
	 with_libstatgrab="yes"
	 with_libstatgrab_pkg_config="yes"
	])

	if test "x$with_libstatgrab" = "xyes" \
	  && test "x$with_libstatgrab_pkg_config" = "xyes"
	then
	  if test "x$PKG_CONFIG" != "x"
	  then
	    AC_MSG_CHECKING([pkg-config for libstatgrab])
	    temp_result="found"
	    $PKG_CONFIG --exists libstatgrab 2>/dev/null
	    if test "$?" != "0"
	    then
	      with_libstatgrab_pkg_config="no"
	      with_libstatgrab="no (pkg-config doesn't know libstatgrab)"
	      temp_result="not found"
	    fi
	    AC_MSG_RESULT([$temp_result])
	  else
	    AC_MSG_NOTICE([pkg-config not available, trying to guess flags for the statgrab library.])
	    with_libstatgrab_pkg_config="no"
	    with_libstatgrab_ldflags="$with_libstatgrab_ldflags -lstatgrab"
	  fi
	fi

	if test "x$with_libstatgrab" = "xyes" \
	  && test "x$with_libstatgrab_pkg_config" = "xyes" \
	  && test "x$with_libstatgrab_cflags" = "x"
	then
	  AC_MSG_CHECKING([for libstatgrab CFLAGS])
	  temp_result="`$PKG_CONFIG --cflags libstatgrab`"
	  if test "$?" = "0"
	  then
	    with_libstatgrab_cflags="$temp_result"
	  else
	    with_libstatgrab="no ($PKG_CONFIG --cflags libstatgrab failed)"
	    temp_result="$PKG_CONFIG --cflags libstatgrab failed"
	  fi
	  AC_MSG_RESULT([$temp_result])
	fi

	if test "x$with_libstatgrab" = "xyes" \
	  && test "x$with_libstatgrab_pkg_config" = "xyes" \
	  && test "x$with_libstatgrab_ldflags" = "x"
	then
	  AC_MSG_CHECKING([for libstatgrab LDFLAGS])
	  temp_result="`$PKG_CONFIG --libs libstatgrab`"
	  if test "$?" = "0"
	  then
	    with_libstatgrab_ldflags="$temp_result"
	  else
	    with_libstatgrab="no ($PKG_CONFIG --libs libstatgrab failed)"
	    temp_result="$PKG_CONFIG --libs libstatgrab failed"
	  fi
	  AC_MSG_RESULT([$temp_result])
	fi

	if test "x$with_libstatgrab" = "xyes"
	then
	  SAVE_CPPFLAGS="$CPPFLAGS"
	  CPPFLAGS="$CPPFLAGS $with_libstatgrab_cflags"

	  AC_CHECK_HEADERS(statgrab.h,
			   [with_libstatgrab="yes"],
			   [with_libstatgrab="no (statgrab.h not found)"])

	  CPPFLAGS="$SAVE_CPPFLAGS"
	fi

	if test "x$with_libstatgrab" = "xyes"
	then
	  SAVE_CFLAGS="$CFLAGS"
	  SAVE_LDFLAGS="$LDFLAGS"

	  CFLAGS="$CFLAGS $with_libstatgrab_cflags"
	  LDFLAGS="$LDFLAGS $with_libstatgrab_ldflags"

	  AC_CHECK_LIB(statgrab, sg_init,
		       [with_libstatgrab="yes"],
		       [with_libstatgrab="no (symbol sg_init not found)"])

	  CFLAGS="$SAVE_CFLAGS"
	  LDFLAGS="$SAVE_LDFLAGS"
	fi

	if test "x$with_libstatgrab" = "xyes"
	then
	  SAVE_CFLAGS="$CFLAGS"
	  SAVE_LIBS="$LIBS"

	  CFLAGS="$CFLAGS $with_libstatgrab_cflags"
	  LDFLAGS="$LDFLAGS $with_libstatgrab_ldflags"
	  LIBS="-lstatgrab $LIBS"

	  AC_CACHE_CHECK([if libstatgrab >= 0.90],
		  [c_cv_have_libstatgrab_0_90],
		  AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[[
	#include <stdio.h>
	#include <statgrab.h>
	]]],
	[[[
	      if (sg_init()) return 0;
	]]]
	    )],
	    [c_cv_have_libstatgrab_0_90="no"],
	    [c_cv_have_libstatgrab_0_90="yes"]
		  )
	  )

	  CFLAGS="$SAVE_CFLAGS"
	  LDFLAGS="$SAVE_LDFLAGS"
	  LIBS="$SAVE_LIBS"
	fi

	AM_CONDITIONAL(BUILD_WITH_LIBSTATGRAB, test "x$with_libstatgrab" = "xyes")
	if test "x$with_libstatgrab" = "xyes"
	then
	  AC_DEFINE(HAVE_LIBSTATGRAB, 1, [Define to 1 if you have the 'statgrab' library (-lstatgrab)])
	  BUILD_WITH_LIBSTATGRAB_CFLAGS="$with_libstatgrab_cflags"
	  BUILD_WITH_LIBSTATGRAB_LDFLAGS="$with_libstatgrab_ldflags"
	  AC_SUBST(BUILD_WITH_LIBSTATGRAB_CFLAGS)
	  AC_SUBST(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
	  if test "x$c_cv_have_libstatgrab_0_90" = "xyes"
	  then
		AC_DEFINE(HAVE_LIBSTATGRAB_0_90, 1, [Define to 1 if libstatgrab version >= 0.90])
	  fi
	fi
])
