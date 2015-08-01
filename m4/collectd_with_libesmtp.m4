AC_DEFUN([COLLECTD_WITH_LIBESMTP], [
	AC_ARG_WITH(libesmtp, [AS_HELP_STRING([--with-libesmtp@<:@=PREFIX@:>@], [Path to libesmtp.])],
	[
		if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			LDFLAGS="$LDFLAGS -L$withval/lib"
			CPPFLAGS="$CPPFLAGS -I$withval/include -D_THREAD_SAFE"
			with_libesmtp="yes"
		else
			with_libesmtp="$withval"
		fi
	],
	[
		with_libesmtp="yes"
	])
	if test "x$with_libesmtp" = "xyes"
	then
		AC_CHECK_LIB(esmtp, smtp_create_session,
		[
			AC_DEFINE(HAVE_LIBESMTP, 1, [Define to 1 if you have the esmtp library (-lesmtp).])
		], [with_libesmtp="no (libesmtp not found)"])
	fi
	if test "x$with_libesmtp" = "xyes"
	then
		AC_CHECK_HEADERS(libesmtp.h,
		[
			AC_DEFINE(HAVE_LIBESMTP_H, 1, [Define to 1 if you have the <libesmtp.h> header file.])
		], [with_libesmtp="no (libesmtp.h not found)"])
	fi
	if test "x$with_libesmtp" = "xyes"
	then
		collect_libesmtp=1
	else
		collect_libesmtp=0
	fi
	AC_DEFINE_UNQUOTED(COLLECT_LIBESMTP, [$collect_libesmtp],
		[Wether or not to use the esmtp library])
	AM_CONDITIONAL(BUILD_WITH_LIBESMTP, test "x$with_libesmtp" = "xyes")
])
