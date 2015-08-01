AC_DEFUN([COLLECTD_WITH_LIBMEMCACHED], [
	with_libmemcached_cppflags=""
	with_libmemcached_ldflags=""
	AC_ARG_WITH(libmemcached, [AS_HELP_STRING([--with-libmemcached@<:@=PREFIX@:>@], [Path to libmemcached.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			with_libmemcached_cppflags="-I$withval/include"
			with_libmemcached_ldflags="-L$withval/lib"
			with_libmemcached="yes"
		else
			with_libmemcached="$withval"
		fi
	],
	[
		with_libmemcached="yes"
	])
	if test "x$with_libmemcached" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libmemcached_cppflags"

		AC_CHECK_HEADERS(libmemcached/memcached.h, [with_libmemcached="yes"], [with_libmemcached="no (libmemcached/memcached.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_libmemcached" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"
		CPPFLAGS="$CPPFLAGS $with_libmemcached_cppflags"
		LDFLAGS="$LDFLAGS $with_libmemcached_ldflags"

		AC_CHECK_LIB(memcached, memcached_create, [with_libmemcached="yes"], [with_libmemcached="no (Symbol 'memcached_create' not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_libmemcached" = "xyes"
	then
		BUILD_WITH_LIBMEMCACHED_CPPFLAGS="$with_libmemcached_cppflags"
		BUILD_WITH_LIBMEMCACHED_LDFLAGS="$with_libmemcached_ldflags"
		BUILD_WITH_LIBMEMCACHED_LIBS="-lmemcached"
		AC_SUBST(BUILD_WITH_LIBMEMCACHED_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBMEMCACHED_LDFLAGS)
		AC_SUBST(BUILD_WITH_LIBMEMCACHED_LIBS)
		AC_DEFINE(HAVE_LIBMEMCACHED, 1, [Define if libmemcached is present and usable.])
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBMEMCACHED, test "x$with_libmemcached" = "xyes")
])
