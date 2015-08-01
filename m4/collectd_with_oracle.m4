AC_DEFUN([COLLECTD_WITH_ORACLE], [
	with_oracle_cppflags=""
	with_oracle_libs=""
	AC_ARG_WITH(oracle, [AS_HELP_STRING([--with-oracle@<:@=ORACLE_HOME@:>@], [Path to Oracle.])],
	[
		if test "x$withval" = "xyes"
		then
			if test "x$ORACLE_HOME" = "x"
			then
				AC_MSG_WARN([Use of the Oracle library has been forced, but the environment variable ORACLE_HOME is not set.])
			fi
			with_oracle="yes"
		else if test "x$withval" = "xno"
		then
			with_oracle="no"
		else
			with_oracle="yes"
			ORACLE_HOME="$withval"
		fi; fi
	],
	[
		if test "x$ORACLE_HOME" = "x"
		then
			with_oracle="no (ORACLE_HOME is not set)"
		else
			with_oracle="yes"
		fi
	])
	if test "x$ORACLE_HOME" != "x"
	then
		with_oracle_cppflags="-I$ORACLE_HOME/rdbms/public"

		if test -e "$ORACLE_HOME/lib/ldflags"
		then
			with_oracle_libs=`cat "$ORACLE_HOME/lib/ldflags"`
		fi
		#with_oracle_libs="-L$ORACLE_HOME/lib $with_oracle_libs -lclntsh"
		with_oracle_libs="-L$ORACLE_HOME/lib -lclntsh"
	fi
	if test "x$with_oracle" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_oracle_cppflags"

		AC_CHECK_HEADERS(oci.h, [with_oracle="yes"], [with_oracle="no (oci.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
	if test "x$with_oracle" = "xyes"
	then
		SAVE_CPPFLAGS="$CPPFLAGS"
		SAVE_LIBS="$LIBS"
		CPPFLAGS="$CPPFLAGS $with_oracle_cppflags"
		LIBS="$LIBS $with_oracle_libs"

		AC_CHECK_FUNC(OCIEnvCreate, [with_oracle="yes"], [with_oracle="no (Symbol 'OCIEnvCreate' not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
		LIBS="$SAVE_LIBS"
	fi
	if test "x$with_oracle" = "xyes"
	then
		BUILD_WITH_ORACLE_CFLAGS="$with_oracle_cppflags"
		BUILD_WITH_ORACLE_LIBS="$with_oracle_libs"
		AC_SUBST(BUILD_WITH_ORACLE_CFLAGS)
		AC_SUBST(BUILD_WITH_ORACLE_LIBS)
	fi
])
