AC_DEFUN([COLLECTD_WITH_LIBKVM], [
	with_libkvm="no"
	AC_CHECK_LIB(kvm, kvm_getprocs, [with_kvm_getprocs="yes"], [with_kvm_getprocs="no"])
	if test "x$with_kvm_getprocs" = "xyes"
	then
		AC_DEFINE(HAVE_LIBKVM_GETPROCS, 1,
			  [Define to 1 if you have the 'kvm' library with the 'kvm_getprocs' symbol (-lkvm)])
		with_libkvm="yes"
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBKVM_GETPROCS, test "x$with_kvm_getprocs" = "xyes")

	AC_CHECK_LIB(kvm, kvm_getswapinfo, [with_kvm_getswapinfo="yes"], [with_kvm_getswapinfo="no"])
	if test "x$with_kvm_getswapinfo" = "xyes"
	then
		AC_DEFINE(HAVE_LIBKVM_GETSWAPINFO, 1,
			  [Define to 1 if you have the 'kvm' library with the 'kvm_getswapinfo' symbol (-lkvm)])
		with_libkvm="yes"
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBKVM_GETSWAPINFO, test "x$with_kvm_getswapinfo" = "xyes")

	AC_CHECK_LIB(kvm, kvm_nlist, [with_kvm_nlist="yes"], [with_kvm_nlist="no"])
	if test "x$with_kvm_nlist" = "xyes"
	then
		AC_CHECK_HEADERS(bsd/nlist.h nlist.h)
		AC_DEFINE(HAVE_LIBKVM_NLIST, 1,
			  [Define to 1 if you have the 'kvm' library with the 'kvm_nlist' symbol (-lkvm)])
		with_libkvm="yes"
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBKVM_NLIST, test "x$with_kvm_nlist" = "xyes")

	AC_CHECK_LIB(kvm, kvm_openfiles, [with_kvm_openfiles="yes"], [with_kvm_openfiles="no"])
	if test "x$with_kvm_openfiles" = "xyes"
	then
		AC_DEFINE(HAVE_LIBKVM_NLIST, 1,
			  [Define to 1 if you have the 'kvm' library with the 'kvm_openfiles' symbol (-lkvm)])
		with_libkvm="yes"
	fi
	AM_CONDITIONAL(BUILD_WITH_LIBKVM_OPENFILES, test "x$with_kvm_openfiles" = "xyes")
])
