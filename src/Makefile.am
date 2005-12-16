if BUILD_MODULE_PING
SUBDIRS = libping .
endif

sbin_PROGRAMS = collectd

collectd_SOURCES = collectd.c collectd.h \
		   utils_debug.c utils_debug.h \
		   utils_mount.c utils_mount.h \
		   common.c common.h \
		   multicast.c multicast.h \
		   plugin.c plugin.h
#collectd_CFLAGS = -DPLUGINDIR='"$(pkglibdir)"'
collectd_CPPFLAGS =
collectd_CPPFLAGS += -DCONFIGFILE='"${sysconfdir}/${PACKAGE_NAME}.conf"'
collectd_CPPFLAGS += -DPKGLOCALSTATEDIR='"${localstatedir}/lib/${PACKAGE_NAME}"'
collectd_CPPFLAGS += -DPIDFILE='"${localstatedir}/run/${PACKAGE_NAME}.pid"'
collectd_CPPFLAGS += -DPLUGINDIR='"${pkglibdir}"'
collectd_LDFLAGS = -export-dynamic
collectd_CFLAGS = -Werror
collectd_LDADD = $(LIBLTDL) "-dlopen" self
collectd_DEPENDENCIES = $(LIBLTDL)
if BUILD_WITH_RRDTOOL
collectd_LDADD += -lrrd
endif

pkglib_LTLIBRARIES = 

if BUILD_MODULE_CPU
pkglib_LTLIBRARIES += cpu.la
cpu_la_SOURCES = cpu.c cpu.h
cpu_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" cpu.la
collectd_DEPENDENCIES += cpu.la
endif

if BUILD_MODULE_CPUFREQ
pkglib_LTLIBRARIES += cpufreq.la
cpufreq_la_SOURCES = cpufreq.c cpufreq.h
cpufreq_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" cpufreq.la
collectd_DEPENDENCIES += cpufreq.la
endif

if BUILD_MODULE_DISK
pkglib_LTLIBRARIES += disk.la
disk_la_SOURCES = disk.c disk.h
disk_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" disk.la
collectd_DEPENDENCIES += disk.la
endif

if BUILD_MODULE_QUOTA
pkglib_LTLIBRARIES += quota.la
quota_la_SOURCES = quota_plugin.c quota_plugin.h
quota_la_SOURCES += quota_fs.c quota_fs.h
quota_la_SOURCES += quota_mnt.c quota_mnt.h
quota_la_LDFLAGS = -module -avoid-version
quota_la_CFLAGS = -Werror
collectd_LDADD += "-dlopen" quota.la
collectd_DEPENDENCIES += quota.la
endif

if BUILD_MODULE_HDDTEMP
pkglib_LTLIBRARIES += hddtemp.la
hddtemp_la_SOURCES = hddtemp.c hddtemp.h
hddtemp_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" hddtemp.la
collectd_DEPENDENCIES += hddtemp.la
endif

if BUILD_MODULE_LOAD
pkglib_LTLIBRARIES += load.la
load_la_SOURCES = load.c load.h
load_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" load.la
collectd_DEPENDENCIES += load.la
endif

if BUILD_MODULE_MEMORY
pkglib_LTLIBRARIES += memory.la
memory_la_SOURCES = memory.c memory.h
memory_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" memory.la
collectd_DEPENDENCIES += memory.la
endif

if BUILD_MODULE_NFS
pkglib_LTLIBRARIES += nfs.la
nfs_la_SOURCES = nfs.c nfs.h
nfs_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" nfs.la
collectd_DEPENDENCIES += nfs.la
endif

if BUILD_MODULE_PING
pkglib_LTLIBRARIES += ping.la
ping_la_SOURCES = ping.c ping.h
ping_la_LDFLAGS = -module -avoid-version
ping_la_LIBADD  = libping/libping.la
ping_la_DEPENDENCIES = libping/libping.la
collectd_LDADD += "-dlopen" ping.la
collectd_DEPENDENCIES += ping.la
endif

if BUILD_MODULE_PROCESSES
pkglib_LTLIBRARIES += processes.la
processes_la_SOURCES = processes.c processes.h
processes_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" processes.la
collectd_DEPENDENCIES += processes.la
endif

if BUILD_MODULE_SENSORS
pkglib_LTLIBRARIES += sensors.la
sensors_la_SOURCES = sensors.c sensors.h
sensors_la_LDFLAGS = -module -avoid-version -lsensors
sensors_la_LIBADD = -lsensors
collectd_LDADD += "-dlopen" sensors.la
collectd_DEPENDENCIES += sensors.la
endif

if BUILD_MODULE_SERIAL
pkglib_LTLIBRARIES += serial.la
serial_la_SOURCES = serial.c serial.h
serial_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" serial.la
collectd_DEPENDENCIES += serial.la
endif

if BUILD_MODULE_SWAP
pkglib_LTLIBRARIES += swap.la
swap_la_SOURCES = swap.c swap.h
swap_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" swap.la
collectd_DEPENDENCIES += swap.la
endif

if BUILD_MODULE_TAPE
pkglib_LTLIBRARIES += tape.la
tape_la_SOURCES = tape.c tape.h
tape_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" tape.la
collectd_DEPENDENCIES += tape.la
endif

if BUILD_MODULE_TRAFFIC
pkglib_LTLIBRARIES += traffic.la
traffic_la_SOURCES = traffic.c traffic.h
traffic_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" traffic.la
collectd_DEPENDENCIES += traffic.la
endif

if BUILD_MODULE_USERS
pkglib_LTLIBRARIES += users.la
users_la_SOURCES = users.c users.h
users_la_LDFLAGS = -module -avoid-version
users_la_CFLAGS = -Werror
collectd_LDADD += "-dlopen" users.la
collectd_DEPENDENCIES += users.la
endif

man_MANS = collectd.1
#collectd_1_SOURCES = collectd.pod

EXTRA_DIST = $(man_MANS)

.pod.1:
	pod2man --release=$(VERSION) --center=$(PACKAGE) $< >$@
