AC_DEFUN([COLLECTD_WITH_LIBNOTIFY], [
	PKG_CHECK_MODULES([LIBNOTIFY], [libnotify],
			[with_libnotify="yes"],
			[if test "x$LIBNOTIFY_PKG_ERRORS" = "x"; then
				 with_libnotify="no"
			 else
				 with_libnotify="no ($LIBNOTIFY_PKG_ERRORS)"
			 fi])
])
