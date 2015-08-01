AC_DEFUN([COLLECTD_WITH_LIBPERL], [
	perl_interpreter="perl"
	AC_ARG_WITH(libperl, [AS_HELP_STRING([--with-libperl@<:@=PREFIX@:>@], [Path to libperl.])],
	[
		if test -f "$withval" && test -x "$withval"
		then
			perl_interpreter="$withval"
			with_libperl="yes"
		else if test "x$withval" != "xno" && test "x$withval" != "xyes"
		then
			LDFLAGS="$LDFLAGS -L$withval/lib"
			CPPFLAGS="$CPPFLAGS -I$withval/include"
			perl_interpreter="$withval/bin/perl"
			with_libperl="yes"
		else
			with_libperl="$withval"
		fi; fi
	],
	[
		with_libperl="yes"
	])

	AC_MSG_CHECKING([for perl])
	perl_interpreter=`which "$perl_interpreter" 2> /dev/null`
	if test -x "$perl_interpreter"
	then
		AC_MSG_RESULT([yes ($perl_interpreter)])
	else
		perl_interpreter=""
		AC_MSG_RESULT([no])
	fi

	AC_SUBST(PERL, "$perl_interpreter")

	if test "x$with_libperl" = "xyes" \
		&& test -n "$perl_interpreter"
	then
	  SAVE_CFLAGS="$CFLAGS"
	  SAVE_LIBS="$LIBS"
	dnl ARCHFLAGS="" -> disable multi -arch on OSX (see Config_heavy.pl:fetch_string)
	  PERL_CFLAGS=`ARCHFLAGS="" $perl_interpreter -MExtUtils::Embed -e ccopts`
	  PERL_LIBS=`ARCHFLAGS="" $perl_interpreter -MExtUtils::Embed -e ldopts`
	  CFLAGS="$CFLAGS $PERL_CFLAGS"
	  LIBS="$LIBS $PERL_LIBS"

	  AC_CACHE_CHECK([for libperl],
	    [c_cv_have_libperl],
	    AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[[
	#define PERL_NO_GET_CONTEXT
	#include <EXTERN.h>
	#include <perl.h>
	#include <XSUB.h>
	]]],
	[[[
	       dTHX;
	       load_module (PERL_LOADMOD_NOIMPORT,
				 newSVpv ("Collectd::Plugin::FooBar", 24),
				 Nullsv);
	]]]
	      )],
	      [c_cv_have_libperl="yes"],
	      [c_cv_have_libperl="no"]
	    )
	  )

	  if test "x$c_cv_have_libperl" = "xyes"
	  then
		  AC_DEFINE(HAVE_LIBPERL, 1, [Define if libperl is present and usable.])
		  AC_SUBST(PERL_CFLAGS)
		  AC_SUBST(PERL_LIBS)
	  else
		  with_libperl="no"
	  fi

	  CFLAGS="$SAVE_CFLAGS"
	  LIBS="$SAVE_LIBS"
	else if test -z "$perl_interpreter"; then
	  with_libperl="no (no perl interpreter found)"
	  c_cv_have_libperl="no"
	fi; fi
	AM_CONDITIONAL(BUILD_WITH_LIBPERL, test "x$with_libperl" = "xyes")

	if test "x$with_libperl" = "xyes"
	then
		SAVE_CFLAGS="$CFLAGS"
		SAVE_LIBS="$LIBS"
		CFLAGS="$CFLAGS $PERL_CFLAGS"
		LIBS="$LIBS $PERL_LIBS"

		AC_CACHE_CHECK([if perl supports ithreads],
			[c_cv_have_perl_ithreads],
			AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[[
	#include <EXTERN.h>
	#include <perl.h>
	#include <XSUB.h>

	#if !defined(USE_ITHREADS)
	# error "Perl does not support ithreads!"
	#endif /* !defined(USE_ITHREADS) */
	]]],
	[[[ ]]]
				)],
				[c_cv_have_perl_ithreads="yes"],
				[c_cv_have_perl_ithreads="no"]
			)
		)

		if test "x$c_cv_have_perl_ithreads" = "xyes"
		then
			AC_DEFINE(HAVE_PERL_ITHREADS, 1, [Define if Perl supports ithreads.])
		fi

		CFLAGS="$SAVE_CFLAGS"
		LIBS="$SAVE_LIBS"
	fi

	if test "x$with_libperl" = "xyes"
	then
		SAVE_CFLAGS="$CFLAGS"
		SAVE_LIBS="$LIBS"
		# trigger an error if Perl_load_module*() uses __attribute__nonnull__(3)
		# (see issues #41 and #42)
		CFLAGS="$CFLAGS $PERL_CFLAGS -Wall -Werror"
		LIBS="$LIBS $PERL_LIBS"

		AC_CACHE_CHECK([for broken Perl_load_module()],
			[c_cv_have_broken_perl_load_module],
			AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[[
	#define PERL_NO_GET_CONTEXT
	#include <EXTERN.h>
	#include <perl.h>
	#include <XSUB.h>
	]]],
	[[[
				 dTHX;
				 load_module (PERL_LOADMOD_NOIMPORT,
				     newSVpv ("Collectd::Plugin::FooBar", 24),
				     Nullsv);
	]]]
				)],
				[c_cv_have_broken_perl_load_module="no"],
				[c_cv_have_broken_perl_load_module="yes"]
			)
		)

		CFLAGS="$SAVE_CFLAGS"
		LIBS="$SAVE_LIBS"
	fi
	AM_CONDITIONAL(HAVE_BROKEN_PERL_LOAD_MODULE,
			test "x$c_cv_have_broken_perl_load_module" = "xyes")

	if test "x$with_libperl" = "xyes"
	then
		SAVE_CFLAGS="$CFLAGS"
		SAVE_LIBS="$LIBS"
		CFLAGS="$CFLAGS $PERL_CFLAGS"
		LIBS="$LIBS $PERL_LIBS"

		AC_CHECK_MEMBER(
			[struct mgvtbl.svt_local],
			[have_struct_mgvtbl_svt_local="yes"],
			[have_struct_mgvtbl_svt_local="no"],
			[
	#include <EXTERN.h>
	#include <perl.h>
	#include <XSUB.h>
			])

		if test "x$have_struct_mgvtbl_svt_local" = "xyes"
		then
			AC_DEFINE(HAVE_PERL_STRUCT_MGVTBL_SVT_LOCAL, 1,
					  [Define if Perl's struct mgvtbl has member svt_local.])
		fi

		CFLAGS="$SAVE_CFLAGS"
		LIBS="$SAVE_LIBS"
	fi
])
