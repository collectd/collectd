if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CPPFLAGS += -DPREFIX='"${prefix}"'
AM_CPPFLAGS += -DCONFIGFILE='"${sysconfdir}/${PACKAGE_NAME}.conf"'
AM_CPPFLAGS += -DLOCALSTATEDIR='"${localstatedir}"'
AM_CPPFLAGS += -DPKGLOCALSTATEDIR='"${localstatedir}/lib/${PACKAGE_NAME}"'
if BUILD_FEATURE_DAEMON
AM_CPPFLAGS += -DPIDFILE='"${localstatedir}/run/${PACKAGE_NAME}.pid"'
endif
AM_CPPFLAGS += -DPLUGINDIR='"${pkglibdir}"'
AM_CPPFLAGS += -DPKGDATADIR='"${pkgdatadir}"'

AUTOMAKE_OPTIONS = subdir-objects

sbin_PROGRAMS = collectd

noinst_LTLIBRARIES = libavltree.la libcommon.la libheap.la libplugin_mock.la

libavltree_la_SOURCES = utils_avltree.c utils_avltree.h

libcommon_la_SOURCES = common.c common.h

libheap_la_SOURCES = utils_heap.c utils_heap.h

libplugin_mock_la_SOURCES = plugin_mock.c utils_cache_mock.c utils_time_mock.c

collectd_SOURCES = collectd.c collectd.h \
		   configfile.c configfile.h \
		   filter_chain.c filter_chain.h \
		   meta_data.c meta_data.h \
		   plugin.c plugin.h \
		   utils_cache.c utils_cache.h \
		   utils_complain.c utils_complain.h \
		   utils_llist.c utils_llist.h \
		   utils_random.c utils_random.h \
		   utils_tail_match.c utils_tail_match.h \
		   utils_match.c utils_match.h \
		   utils_subst.c utils_subst.h \
		   utils_tail.c utils_tail.h \
		   utils_time.c utils_time.h \
		   types_list.c types_list.h \
		   utils_threshold.c utils_threshold.h


collectd_CPPFLAGS =  $(AM_CPPFLAGS) $(LTDLINCL)
collectd_CFLAGS = $(AM_CFLAGS)
collectd_LDFLAGS = -export-dynamic
collectd_LDADD = libavltree.la libcommon.la libheap.la -lm
collectd_DEPENDENCIES =

# Link to these libraries..
if BUILD_WITH_LIBRT
collectd_LDADD += -lrt
endif
if BUILD_WITH_LIBPOSIX4
collectd_LDADD += -lposix4
endif
if BUILD_WITH_LIBSOCKET
collectd_LDADD += -lsocket
endif
if BUILD_WITH_LIBRESOLV
collectd_LDADD += -lresolv
endif
if BUILD_WITH_LIBPTHREAD
collectd_LDADD += -lpthread
endif
if BUILD_WITH_LIBKSTAT
collectd_LDADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
collectd_LDADD += -ldevinfo
endif
if BUILD_AIX
collectd_LDFLAGS += -Wl,-bexpall,-brtllib
endif

# The daemon needs to call sg_init, so we need to link it against libstatgrab,
# too. -octo
if BUILD_WITH_LIBSTATGRAB
collectd_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
collectd_LDADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif

if BUILD_WITH_OWN_LIBOCONFIG
collectd_LDADD += $(LIBLTDL) $(top_builddir)/src/liboconfig/liboconfig.la
collectd_DEPENDENCIES += $(top_builddir)/src/liboconfig/liboconfig.la
else
collectd_LDADD += -loconfig
endif

check_PROGRAMS = test_common test_utils_avltree test_utils_heap
TESTS = test_common test_utils_avltree test_utils_heap

test_common_SOURCES = common_test.c ../testing.h
test_common_LDADD = libcommon.la libplugin_mock.la

test_utils_avltree_SOURCES = utils_avltree_test.c ../testing.h
test_utils_avltree_LDADD = libavltree.la

test_utils_heap_SOURCES = utils_heap_test.c ../testing.h
test_utils_heap_LDADD = libheap.la
