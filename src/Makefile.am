SUBDIRS = libconfig
if BUILD_WITH_OWN_LIBOPING
SUBDIRS += liboping
endif

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

sbin_PROGRAMS = collectd
bin_PROGRAMS = collectd-nagios

collectd_SOURCES = collectd.c collectd.h \
		   utils_debug.c utils_debug.h \
		   utils_mount.c utils_mount.h \
		   utils_llist.c utils_llist.h \
		   utils_ignorelist.c utils_ignorelist.h \
		   common.c common.h \
		   plugin.c plugin.h \
		   configfile.c configfile.h
collectd_CPPFLAGS = $(LTDLINCL)
collectd_CPPFLAGS += -DPREFIX='"${prefix}"'
collectd_CPPFLAGS += -DCONFIGFILE='"${sysconfdir}/${PACKAGE_NAME}.conf"'
collectd_CPPFLAGS += -DPKGLOCALSTATEDIR='"${localstatedir}/lib/${PACKAGE_NAME}"'
if BUILD_FEATURE_DAEMON
collectd_CPPFLAGS += -DPIDFILE='"${localstatedir}/run/${PACKAGE_NAME}.pid"'
endif
collectd_CPPFLAGS += -DPLUGINDIR='"${pkglibdir}"'
if BUILD_FEATURE_DEBUG
collectd_CPPFLAGS += -DLOGFILE='"${localstatedir}/log/${PACKAGE_NAME}/collectd.log"'
endif

# Link to these libraries..
collectd_LDFLAGS = -export-dynamic
if BUILD_WITH_LIBRT
collectd_LDFLAGS += -lrt
endif
if BUILD_WITH_LIBSOCKET
collectd_LDFLAGS += -lsocket
endif
if BUILD_WITH_LIBRESOLV
collectd_LDFLAGS += -lresolv
endif
if BUILD_WITH_LIBKSTAT
collectd_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
collectd_LDFLAGS += -ldevinfo
endif

if BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_LIBKVM
collectd_LDFLAGS += -lkvm
endif
if BUILD_WITH_LIBDEVSTAT
collectd_LDFLAGS += -ldevstat
endif
collectd_LDFLAGS += -lstatgrab
endif


collectd_LDADD = $(LIBLTDL) libconfig/libconfig.la "-dlopen" self
collectd_DEPENDENCIES = $(LIBLTDL) libconfig/libconfig.la

collectd_nagios_SOURCES = collectd-nagios.c

pkglib_LTLIBRARIES = 

if BUILD_MODULE_APACHE
pkglib_LTLIBRARIES += apache.la
apache_la_SOURCES = apache.c
apache_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBCURL
apache_la_LDFLAGS += $(BUILD_WITH_LIBCURL_LIBS)
endif
collectd_LDADD += "-dlopen" apache.la
collectd_DEPENDENCIES += apache.la
endif

if BUILD_MODULE_APCUPS
pkglib_LTLIBRARIES += apcups.la
apcups_la_SOURCES = apcups.c
apcups_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
apcups_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" apcups.la
collectd_DEPENDENCIES += apcups.la
endif

if BUILD_MODULE_APPLE_SENSORS
pkglib_LTLIBRARIES += apple_sensors.la
apple_sensors_la_SOURCES = apple_sensors.c
apple_sensors_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBIOKIT
apple_sensors_la_LDFLAGS += -lIOKit
endif
collectd_LDADD += "-dlopen" apple_sensors.la
collectd_DEPENDENCIES += apple_sensors.la
endif

if BUILD_MODULE_BATTERY
pkglib_LTLIBRARIES += battery.la
battery_la_SOURCES = battery.c
battery_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBIOKIT
battery_la_LDFLAGS += -lIOKit
endif
collectd_LDADD += "-dlopen" battery.la
collectd_DEPENDENCIES += battery.la
endif

if BUILD_MODULE_CPU
pkglib_LTLIBRARIES += cpu.la
cpu_la_SOURCES = cpu.c
cpu_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBKSTAT
cpu_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
cpu_la_LDFLAGS += -ldevinfo
endif
collectd_LDADD += "-dlopen" cpu.la
collectd_DEPENDENCIES += cpu.la
endif

if BUILD_MODULE_CPUFREQ
pkglib_LTLIBRARIES += cpufreq.la
cpufreq_la_SOURCES = cpufreq.c
cpufreq_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" cpufreq.la
collectd_DEPENDENCIES += cpufreq.la
endif

if BUILD_MODULE_CSV
pkglib_LTLIBRARIES += csv.la
csv_la_SOURCES = csv.c
csv_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" csv.la
collectd_DEPENDENCIES += csv.la
endif

if BUILD_MODULE_DF
pkglib_LTLIBRARIES += df.la
df_la_SOURCES = df.c
df_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" df.la
collectd_DEPENDENCIES += df.la
endif

if BUILD_MODULE_DISK
pkglib_LTLIBRARIES += disk.la
disk_la_SOURCES = disk.c
disk_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBKSTAT
disk_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
disk_la_LDFLAGS += -ldevinfo
endif
if BUILD_WITH_LIBIOKIT
disk_la_LDFLAGS += -lIOKit
endif
collectd_LDADD += "-dlopen" disk.la
collectd_DEPENDENCIES += disk.la
endif

if BUILD_MODULE_DNS
pkglib_LTLIBRARIES += dns.la
dns_la_SOURCES = dns.c utils_dns.c utils_dns.h
dns_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBPCAP
dns_la_LDFLAGS += -lpcap
endif
if BUILD_WITH_LIBPTHREAD
dns_la_LDFLAGS += -lpthread
endif
collectd_LDADD += "-dlopen" dns.la
collectd_DEPENDENCIES += dns.la
endif

if BUILD_MODULE_EMAIL
pkglib_LTLIBRARIES += email.la
email_la_SOURCES = email.c
email_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBPTHREAD
email_la_LDFLAGS += -lpthread
endif
collectd_LDADD += "-dlopen" email.la
collectd_DEPENDENCIES += email.la
endif

if BUILD_MODULE_ENTROPY
pkglib_LTLIBRARIES += entropy.la
entropy_la_SOURCES = entropy.c
entropy_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" entropy.la
collectd_DEPENDENCIES += entropy.la
endif

#if BUILD_MODULE_QUOTA
#pkglib_LTLIBRARIES += quota.la
#quota_la_SOURCES = quota_plugin.c quota_plugin.h
#quota_la_SOURCES += quota_fs.c quota_fs.h
#quota_la_SOURCES += quota_mnt.c quota_mnt.h
#quota_la_LDFLAGS = -module -avoid-version
#quota_la_CFLAGS = -Werror
#collectd_LDADD += "-dlopen" quota.la
#collectd_DEPENDENCIES += quota.la
#endif

if BUILD_MODULE_HDDTEMP
pkglib_LTLIBRARIES += hddtemp.la
hddtemp_la_SOURCES = hddtemp.c
hddtemp_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
hddtemp_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" hddtemp.la
collectd_DEPENDENCIES += hddtemp.la
endif

if BUILD_MODULE_LOAD
pkglib_LTLIBRARIES += load.la
load_la_SOURCES = load.c
load_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" load.la
collectd_DEPENDENCIES += load.la
if BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_LIBKVM
load_la_LDFLAGS += -lkvm
endif
if BUILD_WITH_LIBDEVSTAT
load_la_LDFLAGS += -ldevstat
endif
load_la_LDFLAGS += -lstatgrab
endif
endif

if BUILD_MODULE_MBMON
pkglib_LTLIBRARIES += mbmon.la
mbmon_la_SOURCES = mbmon.c
mbmon_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
mbmon_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" mbmon.la
collectd_DEPENDENCIES += mbmon.la
endif

if BUILD_MODULE_MEMORY
pkglib_LTLIBRARIES += memory.la
memory_la_SOURCES = memory.c
memory_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" memory.la
collectd_DEPENDENCIES += memory.la
if BUILD_WITH_LIBKSTAT
memory_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
memory_la_LDFLAGS += -ldevinfo
endif
if BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_LIBKVM
memory_la_LDFLAGS += -lkvm
endif
if BUILD_WITH_LIBDEVSTAT
memory_la_LDFLAGS += -ldevstat
endif
memory_la_LDFLAGS += -lstatgrab
endif
endif

if BUILD_MODULE_MULTIMETER
pkglib_LTLIBRARIES += multimeter.la
multimeter_la_SOURCES = multimeter.c
multimeter_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" multimeter.la
collectd_DEPENDENCIES += multimeter.la
endif

if BUILD_MODULE_MYSQL
pkglib_LTLIBRARIES += mysql.la
mysql_la_SOURCES = mysql.c
mysql_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBMYSQL
mysql_la_LDFLAGS += -lmysqlclient
endif
collectd_LDADD += "-dlopen" mysql.la
collectd_DEPENDENCIES += mysql.la
endif

if BUILD_MODULE_NETWORK
pkglib_LTLIBRARIES += network.la
network_la_SOURCES = network.c
network_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
network_la_LDFLAGS += -lsocket
endif
if BUILD_WITH_LIBPTHREAD
network_la_LDFLAGS += -lpthread
endif
collectd_LDADD += "-dlopen" network.la
collectd_DEPENDENCIES += network.la
endif

if BUILD_MODULE_NFS
pkglib_LTLIBRARIES += nfs.la
nfs_la_SOURCES = nfs.c
nfs_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" nfs.la
collectd_DEPENDENCIES += nfs.la
endif

if BUILD_MODULE_NTPD
pkglib_LTLIBRARIES += ntpd.la
ntpd_la_SOURCES = ntpd.c
ntpd_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
ntpd_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" ntpd.la
collectd_DEPENDENCIES += ntpd.la
endif

if BUILD_MODULE_PING
pkglib_LTLIBRARIES += ping.la
ping_la_SOURCES = ping.c
ping_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBOPING
if BUILD_WITH_OWN_LIBOPING
ping_la_LIBADD  = liboping/liboping.la
ping_la_DEPENDENCIES = liboping/liboping.la
else
ping_la_LDFLAGS += -loping
endif
endif
collectd_LDADD += "-dlopen" ping.la
collectd_DEPENDENCIES += ping.la
endif

if BUILD_MODULE_PROCESSES
pkglib_LTLIBRARIES += processes.la
processes_la_SOURCES = processes.c
processes_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" processes.la
collectd_DEPENDENCIES += processes.la
endif

if BUILD_WITH_RRDTOOL
pkglib_LTLIBRARIES += rrdtool.la
rrdtool_la_SOURCES = rrdtool.c
rrdtool_la_LDFLAGS = -module -avoid-version -lrrd
collectd_LDADD += "-dlopen" rrdtool.la
collectd_DEPENDENCIES += rrdtool.la
endif

if BUILD_MODULE_SENSORS
pkglib_LTLIBRARIES += sensors.la
sensors_la_SOURCES = sensors.c
sensors_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LM_SENSORS
sensors_la_LDFLAGS += -lsensors
endif
collectd_LDADD += "-dlopen" sensors.la
collectd_DEPENDENCIES += sensors.la
endif

if BUILD_MODULE_SERIAL
pkglib_LTLIBRARIES += serial.la
serial_la_SOURCES = serial.c
serial_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" serial.la
collectd_DEPENDENCIES += serial.la
endif

if BUILD_MODULE_SWAP
pkglib_LTLIBRARIES += swap.la
swap_la_SOURCES = swap.c
swap_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" swap.la
collectd_DEPENDENCIES += swap.la
if BUILD_WITH_LIBKSTAT
swap_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
swap_la_LDFLAGS += -ldevinfo
endif
if BUILD_WITH_LIBKVM
swap_la_LDFLAGS += -lkvm
endif
if BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_LIBDEVSTAT
swap_la_LDFLAGS += -ldevstat
endif
swap_la_LDFLAGS += -lstatgrab
endif
endif

if BUILD_MODULE_TAPE
pkglib_LTLIBRARIES += tape.la
tape_la_SOURCES = tape.c
tape_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBKSTAT
tape_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
tape_la_LDFLAGS += -ldevinfo
endif
collectd_LDADD += "-dlopen" tape.la
collectd_DEPENDENCIES += tape.la
endif

if BUILD_MODULE_TRAFFIC
pkglib_LTLIBRARIES += traffic.la
traffic_la_SOURCES = traffic.c
traffic_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" traffic.la
collectd_DEPENDENCIES += traffic.la
if BUILD_WITH_LIBKSTAT
traffic_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
traffic_la_LDFLAGS += -ldevinfo
endif
if BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_LIBKVM
traffic_la_LDFLAGS += -lkvm
endif
if BUILD_WITH_LIBDEVSTAT
traffic_la_LDFLAGS += -ldevstat
endif
traffic_la_LDFLAGS += -lstatgrab
endif
endif

if BUILD_MODULE_UNIXSOCK
pkglib_LTLIBRARIES += unixsock.la
unixsock_la_SOURCES = unixsock.c
unixsock_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBPTHREAD
unixsock_la_LDFLAGS += -lpthread
endif
collectd_LDADD += "-dlopen" unixsock.la
collectd_DEPENDENCIES += unixsock.la
endif

if BUILD_MODULE_USERS
pkglib_LTLIBRARIES += users.la
users_la_SOURCES = users.c
users_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" users.la
collectd_DEPENDENCIES += users.la
endif

if BUILD_MODULE_VSERVER
pkglib_LTLIBRARIES += vserver.la
vserver_la_SOURCES = vserver.c
vserver_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" vserver.la
collectd_DEPENDENCIES += vserver.la
endif

if BUILD_MODULE_WIRELESS
pkglib_LTLIBRARIES += wireless.la
wireless_la_SOURCES = wireless.c
wireless_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" wireless.la
collectd_DEPENDENCIES += wireless.la
endif

dist_man_MANS = collectd.1 collectd.conf.5
#collectd_1_SOURCES = collectd.pod

#EXTRA_DIST = $(man_MANS)

.pod.1:
	pod2man --release=$(VERSION) --center=$(PACKAGE) $< >$@

.pod.5:
	pod2man --section=5 --release=$(VERSION) --center=$(PACKAGE) $< >$@

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)
	if test -e $(DESTDIR)$(sysconfdir)/collectd.conf; \
	then \
		$(INSTALL) -m 0640 collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf.pkg-orig; \
	else \
		$(INSTALL) -m 0640 collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf; \
	fi
