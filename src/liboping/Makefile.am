AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

noinst_LTLIBRARIES = liboping.la

#liboping_la_CFLAGS = 
liboping_la_LDFLAGS = -avoid-version
liboping_la_SOURCES = liboping.h liboping.c
