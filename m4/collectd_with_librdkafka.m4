AC_DEFUN([COLLECTD_WITH_LIBRDKAFKA], [
	AC_ARG_WITH(librdkafka, [AS_HELP_STRING([--with-librdkafka@<:@=PREFIX@:>@], [Path to librdkafka.])],
	[
	  if test "x$withval" != "xno" && test "x$withval" != "xyes"
	  then
	    with_librdkafka_cppflags="-I$withval/include"
	    with_librdkafka_ldflags="-L$withval/lib"
	    with_librdkafka_rpath="$withval/lib"
	    with_librdkafka="yes"
	  else
	    with_librdkafka="$withval"
	  fi
	],
	[
	  with_librdkafka="yes"
	])
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $with_librdkafka_cppflags"
	LDFLAGS="$LDFLAGS $with_librdkafka_ldflags"

	if test "x$with_librdkafka" = "xyes"
	then
		AC_CHECK_HEADERS(librdkafka/rdkafka.h, [with_librdkafka="yes"], [with_librdkafka="no (librdkafka/rdkafka.h not found)"])
	fi

	if test "x$with_librdkafka" = "xyes"
	then
		AC_CHECK_LIB(rdkafka, rd_kafka_new, [with_librdkafka="yes"], [with_librdkafka="no (Symbol 'rd_kafka_new' not found)"])
	  AC_CHECK_LIB(rdkafka, rd_kafka_conf_set_log_cb, [with_librdkafka_log_cb="yes"], [with_librdkafka_log_cb="no"])
	  AC_CHECK_LIB(rdkafka, rd_kafka_set_logger, [with_librdkafka_logger="yes"], [with_librdkafka_logger="no"])
	fi
	if test "x$with_librdkafka" = "xyes"
	then
		BUILD_WITH_LIBRDKAFKA_CPPFLAGS="$with_librdkafka_cppflags"
		BUILD_WITH_LIBRDKAFKA_LDFLAGS="$with_librdkafka_ldflags"
		if test "x$with_librdkafka_rpath" != "x"
		then
			BUILD_WITH_LIBRDKAFKA_LIBS="-Wl,-rpath,$with_librdkafka_rpath -lrdkafka"
		else
			BUILD_WITH_LIBRDKAFKA_LIBS="-lrdkafka"
		fi
		AC_SUBST(BUILD_WITH_LIBRDKAFKA_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBRDKAFKA_LDFLAGS)
		AC_SUBST(BUILD_WITH_LIBRDKAFKA_LIBS)
		AC_DEFINE(HAVE_LIBRDKAFKA, 1, [Define if librdkafka is present and usable.])
	  if test "x$with_librdkafka_log_cb" = "xyes"
	  then
		AC_DEFINE(HAVE_LIBRDKAFKA_LOG_CB, 1, [Define if librdkafka log facility is present and usable.])
	  fi
	  if test "x$with_librdkafka_logger" = "xyes"
	  then
		AC_DEFINE(HAVE_LIBRDKAFKA_LOGGER, 1, [Define if librdkafka log facility is present and usable.])
	  fi
	fi
	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
	AM_CONDITIONAL(BUILD_WITH_LIBRDKAFKA, test "x$with_librdkafka" = "xyes")
])
