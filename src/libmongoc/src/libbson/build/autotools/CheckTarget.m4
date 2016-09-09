AC_CANONICAL_SYSTEM

enable_crosscompile=no

AC_SUBST(BSON_OS, 1)
if test "$TARGET_OS" = "windows"; then
    AC_SUBST(BSON_OS, 2)
fi
