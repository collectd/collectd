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

COMMON_LDFLAGS =

if BUILD_WIN32
COMMON_LDFLAGS += -no-undefined -shared
endif

# Link to these libraries..
COMMON_LIBS =
if BUILD_WIN32
COMMON_LIBS += -lws2_32
endif
if BUILD_WITH_GNULIB
COMMON_LIBS += -lgnu
endif
if BUILD_WITH_LIBRT
COMMON_LIBS += -lrt
endif
if BUILD_WITH_LIBPOSIX4
COMMON_LIBS += -lposix4
endif
if BUILD_WITH_LIBSOCKET
COMMON_LIBS += -lsocket
endif
if BUILD_WITH_LIBRESOLV
COMMON_LIBS += -lresolv
endif
if BUILD_WITH_LIBPTHREAD
COMMON_LIBS += -lpthread
endif
if BUILD_WITH_LIBKSTAT
COMMON_LIBS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
COMMON_LIBS += -ldevinfo
endif

AUTOMAKE_OPTIONS = subdir-objects

sbin_PROGRAMS = collectd

lib_LTLIBRARIES = libcollectd.la
noinst_LTLIBRARIES = libmetadata.la libplugin_mock.la


libcollectd_la_SOURCES =  \
			common.c common.h \
			utils_heap.c utils_heap.h \
			utils_avltree.c utils_avltree.h \
			configfile.c configfile.h \
			filter_chain.c filter_chain.h \
			globals.c globals.h \
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
libcollectd_la_CPPFLAGS = $(AM_CPPFLAGS) $(LTDLINCL)
libcollectd_la_CFLAGS = $(AM_CFLAGS)
libcollectd_la_LDFLAGS = $(COMMON_LDFLAGS)
libcollectd_la_LIBADD = -lm $(COMMON_LIBS) -lltdl
libcollectd_la_DEPENDENCIES =

libmetadata_la_SOURCES = meta_data.c meta_data.h

libplugin_mock_la_SOURCES = plugin_mock.c utils_cache_mock.c \
			    utils_time.c utils_time.h
libplugin_mock_la_CPPFLAGS = $(AM_CPPFLAGS) -DMOCK_TIME
libplugin_mock_la_LIBADD = $(COMMON_LIBS)

if BUILD_WITH_OWN_LIBOCONFIG
libcollectd_la_LIBADD += $(top_builddir)/src/liboconfig/liboconfig.la
libcollectd_la_DEPENDENCIES += $(top_builddir)/src/liboconfig/liboconfig.la
else
libcollectd_la_LIBADD += -loconfig
endif


collectd_SOURCES = collectd.c collectd.h
collectd_CPPFLAGS =  $(AM_CPPFLAGS) $(LTDLINCL)
collectd_CFLAGS = $(AM_CFLAGS)
collectd_LDFLAGS = -export-dynamic
collectd_LDADD = libcollectd.la -lm $(COMMON_LIBS)
collectd_DEPENDENCIES = libmetadata.la

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

check_PROGRAMS = test_common test_meta_data test_utils_avltree test_utils_heap test_utils_time test_utils_subst
TESTS          = test_common test_meta_data test_utils_avltree test_utils_heap test_utils_time test_utils_subst

test_common_SOURCES = common_test.c ../testing.h
test_common_LDADD = libcollectd.la libplugin_mock.la

test_meta_data_SOURCES = meta_data_test.c ../testing.h
test_meta_data_LDADD = libmetadata.la libplugin_mock.la

test_utils_avltree_SOURCES = utils_avltree_test.c ../testing.h
test_utils_avltree_LDADD = libcollectd.la libavltree.la $(COMMON_LIBS)

test_utils_heap_SOURCES = utils_heap_test.c ../testing.h
test_utils_heap_LDADD = libcollectd.a libheap.la $(COMMON_LIBS)

test_utils_time_SOURCES = utils_time_test.c ../testing.h

test_utils_subst_SOURCES = utils_subst_test.c ../testing.h \
			   utils_subst.c utils_subst.h
test_utils_subst_LDADD = libplugin_mock.la
