AC_DEFUN([COLLECTD_CHECK_PIE],[
    AC_REQUIRE([gl_UNKNOWN_WARNINGS_ARE_ERRORS])
    PIE_CFLAGS=
    PIE_LDFLAGS=
    SAVE_CFLAGS=$CFLAGS
    CFLAGS="-fPIE -DPIE"
    gl_COMPILER_OPTION_IF([-pie], [
      PIE_CFLAGS="-fPIE -DPIE"
      PIE_LDFLAGS="-pie"
      ], [
        dnl some versions of clang require -Wl,-pie instead of -pie
        gl_COMPILER_OPTION_IF([[-Wl,-pie]], [
          PIE_CFLAGS="-fPIE -DPIE"
          PIE_LDFLAGS="-Wl,-pie"
          ], [],
          [AC_LANG_PROGRAM([[
#include <pthread.h>
__thread unsigned int t_id;
          ]], [[t_id = 1;]])]
      )
    ],
    [AC_LANG_PROGRAM([[
#include <pthread.h>
__thread unsigned int t_id;
            ]], [[t_id = 1;]])]
    )
    CFLAGS=$SAVE_CFLAGS
    AC_SUBST([PIE_CFLAGS])
    AC_SUBST([PIE_LDFLAGS])
])
