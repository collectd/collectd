BUILT_SOURCES = parser.h
#CLEANFILES = parser.[ch] scanner.c
AM_YFLAGS = -d

noinst_LTLIBRARIES = liboconfig.la

liboconfig_la_LDFLAGS = -avoid-version $(LEXLIB)
liboconfig_la_SOURCES = oconfig.c oconfig.h aux_types.h scanner.l parser.y
