AC_DEFUN([COLLECTD_WITH_LIBMONGOC], [
	AC_ARG_WITH(libmongoc, [AS_HELP_STRING([--with-libmongoc@<:@=PREFIX@:>@], [Path to libmongoc.])],
	[
	 if test "x$withval" = "xyes"
	 then
		 with_libmongoc="yes"
	 else if test "x$withval" = "xno"
	 then
		 with_libmongoc="no"
	 else
		 with_libmongoc="yes"
		 LIBMONGOC_CPPFLAGS="$LIBMONGOC_CPPFLAGS -I$withval/include"
		 LIBMONGOC_LDFLAGS="$LIBMONGOC_LDFLAGS -L$withval/lib"
	 fi; fi
	],
	[with_libmongoc="yes"])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $LIBMONGOC_CPPFLAGS"
	LDFLAGS="$LDFLAGS $LIBMONGOC_LDFLAGS"

	if test "x$with_libmongoc" = "xyes"
	then
		if test "x$LIBMONGOC_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([libmongoc CPPFLAGS: $LIBMONGOC_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(mongo.h,
		[with_libmongoc="yes"],
		[with_libmongoc="no ('mongo.h' not found)"],
	[#if HAVE_STDINT_H
	# define MONGO_HAVE_STDINT 1
	#else
	# define MONGO_USE_LONG_LONG_INT 1
	#endif
	])
	fi
	if test "x$with_libmongoc" = "xyes"
	then
		if test "x$LIBMONGOC_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([libmongoc LDFLAGS: $LIBMONGOC_LDFLAGS])
		fi
		AC_CHECK_LIB(mongoc, mongo_run_command,
		[with_libmongoc="yes"],
		[with_libmongoc="no (symbol 'mongo_run_command' not found)"])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libmongoc" = "xyes"
	then
		BUILD_WITH_LIBMONGOC_CPPFLAGS="$LIBMONGOC_CPPFLAGS"
		BUILD_WITH_LIBMONGOC_LDFLAGS="$LIBMONGOC_LDFLAGS"
		AC_SUBST(BUILD_WITH_LIBMONGOC_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBMONGOC_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBMONGOC, test "x$with_libmongoc" = "xyes")
])
