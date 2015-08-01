AC_DEFUN([COLLECTD_WITH_LIBRRD], [
	librrd_cflags=""
	librrd_ldflags=""
	librrd_threadsafe="yes"
	librrd_rrdc_update="no"
	AC_ARG_WITH(librrd, [AS_HELP_STRING([--with-librrd@<:@=PREFIX@:>@], [Path to rrdtool.])],
	[	if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			librrd_cflags="-I$withval/include"
			librrd_ldflags="-L$withval/lib"
			with_librrd="yes"
		else
			with_librrd="$withval"
		fi
	], [with_librrd="yes"])
	if test "x$with_librrd" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"

		CPPFLAGS="$CPPFLAGS $librrd_cflags"
		LDFLAGS="$LDFLAGS $librrd_ldflags"

		AC_CHECK_HEADERS(rrd.h,, [with_librrd="no (rrd.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_librrd" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LDFLAGS="$LDFLAGS"

		CPPFLAGS="$CPPFLAGS $librrd_cflags"
		LDFLAGS="$LDFLAGS $librrd_ldflags"

		AC_CHECK_LIB(rrd_th, rrd_update_r,
		[with_librrd="yes"
		 librrd_ldflags="$librrd_ldflags -lrrd_th -lm"
		],
		[librrd_threadsafe="no"
		 AC_CHECK_LIB(rrd, rrd_update,
		 [with_librrd="yes"
		  librrd_ldflags="$librrd_ldflags -lrrd -lm"
		 ],
		 [with_librrd="no (symbol 'rrd_update' not found)"],
		 [-lm])
		],
		[-lm])

		if test "x$librrd_threadsafe" = "xyes"
		then
			AC_CHECK_LIB(rrd_th, rrdc_update, [librrd_rrdc_update="yes"], [librrd_rrdc_update="no"])
		else
			AC_CHECK_LIB(rrd, rrdc_update, [librrd_rrdc_update="yes"], [librrd_rrdc_update="no"])
		fi

		CPPFLAGS="$SAVE_CPPFLAGS"
		LDFLAGS="$SAVE_LDFLAGS"
	fi
	if test "x$with_librrd" = "xyes"
	then
		BUILD_WITH_LIBRRD_CFLAGS="$librrd_cflags"
		BUILD_WITH_LIBRRD_LDFLAGS="$librrd_ldflags"
		AC_SUBST(BUILD_WITH_LIBRRD_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBRRD_LDFLAGS)
	fi
	if test "x$librrd_threadsafe" = "xyes"
	then
		AC_DEFINE(HAVE_THREADSAFE_LIBRRD, 1, [Define to 1 if you have the threadsafe rrd library (-lrrd_th).])
	fi
])
