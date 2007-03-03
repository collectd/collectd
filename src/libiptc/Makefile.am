AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall
endif

EXTRA_DIST = libiptc.c

noinst_LTLIBRARIES = libiptc.la

#libiptc_la_CFLAGS = 
libiptc_la_LDFLAGS = -avoid-version
libiptc_la_SOURCES = libip4tc.c