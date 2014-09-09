AUTOMAKE_OPTIONS = foreign no-dependencies

BUILT_SOURCES = parser.h
#CLEANFILES = parser.[ch] scanner.c
AM_YFLAGS = -d

noinst_LTLIBRARIES = liboconfig.la

liboconfig_la_LDFLAGS = -version-info 0:0:0 $(LEXLIB)
liboconfig_la_SOURCES = oconfig.c oconfig.h aux_types.h scanner.l parser.y
