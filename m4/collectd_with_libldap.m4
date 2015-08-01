AC_DEFUN([COLLECTD_WITH_LIBLDAP], [
	# --with-libldap {{{
	AC_ARG_WITH(libldap, [AS_HELP_STRING([--with-libldap@<:@=PREFIX@:>@], [Path to libldap.])],
	[
	 if test "x$withval" = "xyes"
	 then
		 with_libldap="yes"
	 else if test "x$withval" = "xno"
	 then
		 with_libldap="no"
	 else
		 with_libldap="yes"
		 LIBLDAP_CPPFLAGS="$LIBLDAP_CPPFLAGS -I$withval/include"
		 LIBLDAP_LDFLAGS="$LIBLDAP_LDFLAGS -L$withval/lib"
	 fi; fi
	],
	[with_libldap="yes"])

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $LIBLDAP_CPPFLAGS"
	LDFLAGS="$LDFLAGS $LIBLDAP_LDFLAGS"

	if test "x$with_libldap" = "xyes"
	then
		if test "x$LIBLDAP_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([libldap CPPFLAGS: $LIBLDAP_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(ldap.h,
		[with_libldap="yes"],
		[with_libldap="no ('ldap.h' not found)"])
	fi
	if test "x$with_libldap" = "xyes"
	then
		if test "x$LIBLDAP_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([libldap LDFLAGS: $LIBLDAP_LDFLAGS])
		fi
		AC_CHECK_LIB(ldap, ldap_initialize,
		[with_libldap="yes"],
		[with_libldap="no (symbol 'ldap_initialize' not found)"])

	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libldap" = "xyes"
	then
		BUILD_WITH_LIBLDAP_CPPFLAGS="$LIBLDAP_CPPFLAGS"
		BUILD_WITH_LIBLDAP_LDFLAGS="$LIBLDAP_LDFLAGS"
		AC_SUBST(BUILD_WITH_LIBLDAP_CPPFLAGS)
		AC_SUBST(BUILD_WITH_LIBLDAP_LDFLAGS)
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBLDAP, test "x$with_libldap" = "xyes")
])
