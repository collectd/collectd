AC_DEFUN([COLLECTD_WITH_LIBGCRYPT], [
	# --with-libgcrypt {{{
	GCRYPT_CPPFLAGS="$GCRYPT_CPPFLAGS"
	GCRYPT_LDFLAGS="$GCRYPT_LDFLAGS"
	GCRYPT_LIBS="$GCRYPT_LIBS"
	AC_ARG_WITH(libgcrypt, [AS_HELP_STRING([--with-libgcrypt@<:@=PREFIX@:>@], [Path to libgcrypt.])],
	[
	 if test -f "$withval" && test -x "$withval"
	 then
		 with_libgcrypt_config="$withval"
		 with_libgcrypt="yes"
	 else if test -f "$withval/bin/gcrypt-config" && test -x "$withval/bin/gcrypt-config"
	 then
		 with_libgcrypt_config="$withval/bin/gcrypt-config"
		 with_libgcrypt="yes"
	 else if test -d "$withval"
	 then
		 GCRYPT_CPPFLAGS="$GCRYPT_CPPFLAGS -I$withval/include"
		 GCRYPT_LDFLAGS="$GCRYPT_LDFLAGS -L$withval/lib"
		 with_libgcrypt="yes"
	 else
		 with_libgcrypt_config="gcrypt-config"
		 with_libgcrypt="$withval"
	 fi; fi; fi
	],
	[
	 with_libgcrypt_config="libgcrypt-config"
	 with_libgcrypt="yes"
	])

	if test "x$with_libgcrypt" = "xyes" && test "x$with_libgcrypt_config" != "x"
	then
		if test "x$GCRYPT_CPPFLAGS" = "x"
		then
			GCRYPT_CPPFLAGS=`"$with_libgcrypt_config" --cflags 2>/dev/null`
		fi

		if test "x$GCRYPT_LDFLAGS" = "x"
		then
			gcrypt_exec_prefix=`"$with_libgcrypt_config" --exec-prefix 2>/dev/null`
			GCRYPT_LDFLAGS="-L$gcrypt_exec_prefix/lib"
		fi

		if test "x$GCRYPT_LIBS" = "x"
		then
			GCRYPT_LIBS=`"$with_libgcrypt_config" --libs 2>/dev/null`
		fi
	fi

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $GCRYPT_CPPFLAGS"
	LDFLAGS="$LDFLAGS $GCRYPT_LDFLAGS"

	if test "x$with_libgcrypt" = "xyes"
	then
		if test "x$GCRYPT_CPPFLAGS" != "x"
		then
			AC_MSG_NOTICE([gcrypt CPPFLAGS: $GCRYPT_CPPFLAGS])
		fi
		AC_CHECK_HEADERS(gcrypt.h,
			[with_libgcrypt="yes"],
			[with_libgcrypt="no (gcrypt.h not found)"])
	fi

	if test "x$with_libgcrypt" = "xyes"
	then
		if test "x$GCRYPT_LDFLAGS" != "x"
		then
			AC_MSG_NOTICE([gcrypt LDFLAGS: $GCRYPT_LDFLAGS])
		fi
		AC_CHECK_LIB(gcrypt, gcry_md_hash_buffer,
			[with_libgcrypt="yes"],
			[with_libgcrypt="no (symbol gcry_md_hash_buffer not found)"])

		if test "$with_libgcrypt" != "no"; then
			m4_ifdef([AM_PATH_LIBGCRYPT],[AM_PATH_LIBGCRYPT(1:1.2.0,,with_libgcrypt="no (version 1.2.0+ required)")])
			GCRYPT_CPPFLAGS="$LIBGCRYPT_CPPFLAGS $LIBGCRYPT_CFLAGS"
			GCRYPT_LIBS="$LIBGCRYPT_LIBS"
		fi
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	if test "x$with_libgcrypt" = "xyes"
	then
		AC_DEFINE(HAVE_LIBGCRYPT, 1, [Define to 1 if you have the gcrypt library (-lgcrypt).])
	fi

	AC_SUBST(GCRYPT_CPPFLAGS)
	AC_SUBST(GCRYPT_LDFLAGS)
	AC_SUBST(GCRYPT_LIBS)
	AM_CONDITIONAL(BUILD_WITH_LIBGCRYPT, test "x$with_libgcrypt" = "xyes")
])
