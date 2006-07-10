AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

EXTRA_DIST = AUTHORS COPYING ChangeLog README

noinst_LTLIBRARIES = liboping.la

#liboping_la_CFLAGS = 
liboping_la_LDFLAGS = -avoid-version
if BUILD_WITH_LIBSOCKET
liboping_la_LDFLAGS += -lsocket
endif
liboping_la_SOURCES = liboping.c oping.h
