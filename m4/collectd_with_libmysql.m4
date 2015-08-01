AC_DEFUN([COLLECTD_WITH_LIBMYSQL], [
	with_mysql_config="mysql_config"
	with_mysql_cflags=""
	with_mysql_libs=""
	AC_ARG_WITH(libmysql, [AS_HELP_STRING([--with-libmysql@<:@=PREFIX@:>@], [Path to libmysql.])],
	[
		if test "x$withval" = "xno"
		then
			with_libmysql="no"
		else if test "x$withval" = "xyes"
		then
			with_libmysql="yes"
		else
			if test -f "$withval" && test -x "$withval";
			then
				with_mysql_config="$withval"
			else if test -x "$withval/bin/mysql_config"
			then
				with_mysql_config="$withval/bin/mysql_config"
			fi; fi
			with_libmysql="yes"
		fi; fi
	],
	[
		with_libmysql="yes"
	])
	if test "x$with_libmysql" = "xyes"
	then
		with_mysql_cflags=`$with_mysql_config --cflags 2>/dev/null`
		mysql_config_status=$?

		if test $mysql_config_status -ne 0
		then
			with_libmysql="no ($with_mysql_config failed)"
		else
			SAVE_CPPFLAGS="$CPPFLAGS"
			CPPFLAGS="$CPPFLAGS $with_mysql_cflags"

			have_mysql_h="no"
			have_mysql_mysql_h="no"
			AC_CHECK_HEADERS(mysql.h, [have_mysql_h="yes"])

			if test "x$have_mysql_h" = "xno"
			then
				AC_CHECK_HEADERS(mysql/mysql.h, [have_mysql_mysql_h="yes"])
			fi

			if test "x$have_mysql_h$have_mysql_mysql_h" = "xnono"
			then
				with_libmysql="no (mysql.h not found)"
			fi

			CPPFLAGS="$SAVE_CPPFLAGS"
		fi
	fi
	if test "x$with_libmysql" = "xyes"
	then
		with_mysql_libs=`$with_mysql_config --libs_r 2>/dev/null`
		mysql_config_status=$?

		if test $mysql_config_status -ne 0
		then
			with_libmysql="no ($with_mysql_config failed)"
		else
			AC_CHECK_LIB(mysqlclient, mysql_init,
			 [with_libmysql="yes"],
			 [with_libmysql="no (symbol 'mysql_init' not found)"],
			 [$with_mysql_libs])

			AC_CHECK_LIB(mysqlclient, mysql_get_server_version,
			 [with_libmysql="yes"],
			 [with_libmysql="no (symbol 'mysql_get_server_version' not found)"],
			 [$with_mysql_libs])
		fi
	fi
	if test "x$with_libmysql" = "xyes"
	then
		BUILD_WITH_LIBMYSQL_CFLAGS="$with_mysql_cflags"
		BUILD_WITH_LIBMYSQL_LIBS="$with_mysql_libs"
		AC_SUBST(BUILD_WITH_LIBMYSQL_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBMYSQL_LIBS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBMYSQL, test "x$with_libmysql" = "xyes")
])
