#
# q: What is this ?
# a: A specfile for building RPM packages of current collectd releases, for
#    RHEL/CentOS versions 5, 6 and 7. By default all the plugins which are
#    buildable based on the libraries available in the distribution + the
#    EPEL repository, will be built. Plugins depending on external libs will
#    be packaged in separate RPMs.
#
# q: And how can I do that ?
# a: By following these instructions, using mock:
#
# - install and configure mock (https://fedoraproject.org/wiki/Projects/Mock)
#
# - enable the EPEL repository (http://dl.fedoraproject.org/pub/epel/) in the
#   configuration files for your target systems (/etc/mock/*.cfg).
#
# - fetch the desired collectd release file from https://collectd.org/files/
#   and save it in your ~/rpmbuild/SOURCES/ directory (or build your own out of
#   the git repository: ./build.sh && ./configure && make-dist-bz2)
#
# - copy this file in your ~/rpmbuild/SPECS/ directory. Make sure the
#   "Version:" tag matches the version from the tarball.
#
# - build the SRPM first:
#   mock -r centos-6-x86_64 --buildsrpm --spec ~/rpmbuild/SPECS/collectd.spec \
#     --sources ~/rpmbuild/SOURCES/
#
# - then build the RPMs:
#   mock -r centos-6-x86_64 --no-clean --rebuild \
#     /var/lib/mock/centos-6-x86_64/result/collectd-X.Y.Z-NN.src.rpm
#
# - you can also optionally enable/disable plugins which are disabled/enabled
#   by default:
#   mock -r centos-6-x86_64 --no-clean --without=java --with=oracle --rebuild \
#     /var/lib/mock/centos-6-x86_64/result/collectd-X.Y.Z-NN.src.rpm
#

%global _hardened_build 1
%{?perl_default_filter}

# plugins only buildable on RHEL6
# (NB: %{elN} macro is not available on RHEL < 6)
%{?el6:%global _has_libyajl 1}
%{?el6:%global _has_recent_libpcap 1}
%{?el6:%global _has_recent_sockios_h 1}
%{?el6:%global _has_recent_libganglia 1}
%{?el6:%global _has_working_libiptc 1}
%{?el6:%global _has_ip_vs_h 1}
%{?el6:%global _has_lvm2app_h 1}
%{?el6:%global _has_libmodbus 1}
%{?el6:%global _has_libudev 1}
%{?el6:%global _has_iproute 1}
%{?el6:%global _has_atasmart 1}
%{?el6:%global _has_hiredis 1}
%{?el6:%global _has_asm_msr_index 1}

%{?el7:%global _has_libyajl 1}
%{?el7:%global _has_recent_libpcap 1}
%{?el7:%global _has_recent_sockios_h 1}
%{?el7:%global _has_working_libiptc 1}
%{?el7:%global _has_ip_vs_h 1}
%{?el7:%global _has_lvm2app_h 1}
%{?el7:%global _has_libudev 1}
%{?el7:%global _has_recent_librrd 1}
%{?el7:%global _has_varnish4 1}
%{?el7:%global _has_broken_libmemcached 1}
%{?el7:%global _has_iproute 1}
%{?el7:%global _has_atasmart 1}
%{?el7:%global _has_hiredis 1}
%{?el7:%global _has_asm_msr_index 1}

# plugins enabled by default
%define with_aggregation 0%{!?_without_aggregation:1}
%define with_amqp 0%{!?_without_amqp:1}
%define with_apache 0%{!?_without_apache:1}
%define with_apcups 0%{!?_without_apcups:1}
%define with_ascent 0%{!?_without_ascent:1}
%define with_battery 0%{!?_without_battery:1}
%define with_bind 0%{!?_without_bind:1}
%define with_ceph 0%{!?_without_ceph:0%{?_has_libyajl}}
%define with_cgroups 0%{!?_without_cgroups:1}
%define with_conntrack 0%{!?_without_conntrack:1}
%define with_contextswitch 0%{!?_without_contextswitch:1}
%define with_cpu 0%{!?_without_cpu:1}
%define with_cpufreq 0%{!?_without_cpufreq:1}
%define with_csv 0%{!?_without_csv:1}
%define with_curl 0%{!?_without_curl:1}
%define with_curl_json 0%{!?_without_curl_json:0%{?_has_libyajl}}
%define with_curl_xml 0%{!?_without_curl_xml:1}
%define with_dbi 0%{!?_without_dbi:1}
%define with_df 0%{!?_without_df:1}
%define with_disk 0%{!?_without_disk:1}
%define with_dns 0%{!?_without_dns:0%{?_has_recent_libpcap}}
%define with_drbd 0%{!?_without_drbd:1}
%define with_email 0%{!?_without_email:1}
%define with_entropy 0%{!?_without_entropy:1}
%define with_ethstat 0%{!?_without_ethstat:0%{?_has_recent_sockios_h}}
%define with_exec 0%{!?_without_exec:1}
%define with_fhcount 0%{!?_without_fhcount:1}
%define with_filecount 0%{!?_without_filecount:1}
%define with_fscache 0%{!?_without_fscache:1}
%define with_gmond 0%{!?_without_gmond:0%{?_has_recent_libganglia}}
%define with_hddtemp 0%{!?_without_hddtemp:1}
%define with_interface 0%{!?_without_interface:1}
%define with_ipc 0%{!?_without_ipc:1}
%define with_ipmi 0%{!?_without_ipmi:1}
%define with_iptables 0%{!?_without_iptables:0%{?_has_working_libiptc}}
%define with_ipvs 0%{!?_without_ipvs:0%{?_has_ip_vs_h}}
%define with_irq 0%{!?_without_irq:1}
%define with_java 0%{!?_without_java:1}
%define with_virt 0%{!?_without_virt:1}
%define with_load 0%{!?_without_load:1}
%define with_logfile 0%{!?_without_logfile:1}
%define with_log_logstash 0%{!?_without_log_logstash:0%{?_has_libyajl}}
%define with_lvm 0%{!?_without_lvm:0%{?_has_lvm2app_h}}
%define with_madwifi 0%{!?_without_madwifi:1}
%define with_mbmon 0%{!?_without_mbmon:1}
%define with_md 0%{!?_without_md:1}
%define with_memcachec 0%{!?_without_memcachec:0%{!?_has_broken_libmemcached:1}}
%define with_memcached 0%{!?_without_memcached:1}
%define with_memory 0%{!?_without_memory:1}
%define with_multimeter 0%{!?_without_multimeter:1}
%define with_modbus 0%{!?_without_modbus:0%{?_has_libmodbus}}
%define with_mysql 0%{!?_without_mysql:1}
%define with_netlink 0%{!?_without_netlink:0%{?_has_iproute}}
%define with_network 0%{!?_without_network:1}
%define with_nfs 0%{!?_without_nfs:1}
%define with_nginx 0%{!?_without_nginx:1}
%define with_notify_desktop 0%{!?_without_notify_desktop:1}
%define with_notify_email 0%{!?_without_notify_email:1}
%define with_ntpd 0%{!?_without_ntpd:1}
%define with_numa 0%{!?_without_numa:1}
%define with_nut 0%{!?_without_nut:1}
%define with_olsrd 0%{!?_without_olsrd:1}
%define with_openldap 0%{!?_without_openldap:1}
%define with_openvpn 0%{!?_without_openvpn:1}
%define with_perl 0%{!?_without_perl:1}
%define with_pinba 0%{!?_without_pinba:1}
%define with_ping 0%{!?_without_ping:1}
%define with_postgresql 0%{!?_without_postgresql:1}
%define with_powerdns 0%{!?_without_powerdns:1}
%define with_processes 0%{!?_without_processes:1}
%define with_protocols 0%{!?_without_protocols:1}
%define with_python 0%{!?_without_python:1}
%define with_redis 0%{!?_without_redis:0%{?_has_hiredis}}
%define with_rrdcached 0%{!?_without_rrdcached:0%{?_has_recent_librrd}}
%define with_rrdtool 0%{!?_without_rrdtool:1}
%define with_sensors 0%{!?_without_sensors:1}
%define with_serial 0%{!?_without_serial:1}
%define with_smart 0%{!?_without_smart:0%{?_has_atasmart}}
%define with_snmp 0%{!?_without_snmp:1}
%define with_statsd 0%{!?_without_statsd:1}
%define with_swap 0%{!?_without_swap:1}
%define with_syslog 0%{!?_without_syslog:1}
%define with_table 0%{!?_without_table:1}
%define with_tail 0%{!?_without_tail:1}
%define with_tail_csv 0%{!?_without_tail_csv:1}
%define with_tcpconns 0%{!?_without_tcpconns:1}
%define with_teamspeak2 0%{!?_without_teamspeak2:1}
%define with_ted 0%{!?_without_ted:1}
%define with_thermal 0%{!?_without_thermal:1}
%define with_threshold 0%{!?_without_threshold:1}
%define with_turbostat 0%{!?_without_turbostat:0%{?_has_asm_msr_index}}
%define with_unixsock 0%{!?_without_unixsock:1}
%define with_uptime 0%{!?_without_uptime:1}
%define with_users 0%{!?_without_users:1}
%define with_uuid 0%{!?_without_uuid:1}
%define with_varnish 0%{!?_without_varnish:0%{!?_has_varnish4:1}}
%define with_vmem 0%{!?_without_vmem:1}
%define with_vserver 0%{!?_without_vserver:1}
%define with_wireless 0%{!?_without_wireless:1}
%define with_write_graphite 0%{!?_without_write_graphite:1}
%define with_write_http 0%{!?_without_write_http:1}
%define with_write_log 0%{!?_without_write_log:1}
%define with_write_redis 0%{!?_without_write_redis:0%{?_has_hiredis}}
%define with_write_riemann 0%{!?_without_write_riemann:1}
%define with_write_sensu 0%{!?_without_write_sensu:1}
%define with_write_tsdb 0%{!?_without_write_tsdb:1}
%define with_zfs_arc 0%{!?_without_zfs_arc:1}
%define with_zookeeper 0%{!?_without_zookeeper:1}

# Plugins not built by default because of dependencies on libraries not
# available in RHEL or EPEL:

# plugin apple_sensors disabled, requires a Mac
%define with_apple_sensors 0%{!?_without_apple_sensors:0}
# plugin aquaero disabled, requires a libaquaero5
%define with_aquaero 0%{!?_without_aquaero:0}
# plugin barometer disabled, requires a libi2c
%define with_barometer 0%{!?_without_barometer:0}
# plugin lpar disabled, requires AIX
%define with_lpar 0%{!?_without_lpar:0}
# plugin mic disabled, requires Mic
%define with_mic 0%{!?_without_mic:0}
# plugin netapp disabled, requires libnetapp
%define with_netapp 0%{!?_without_netapp:0}
# plugin onewire disabled, requires libowfs
%define with_onewire 0%{!?_without_onewire:0}
# plugin oracle disabled, requires Oracle
%define with_oracle 0%{!?_without_oracle:0}
# plugin oracle disabled, requires BSD
%define with_pf 0%{!?_without_pf:0}
# plugin routeros disabled, requires librouteros
%define with_routeros 0%{!?_without_routeros:0}
# plugin sigrok disabled, requires libsigrok
%define with_sigrok 0%{!?_without_sigrok:0}
# plugin tape disabled, requires libkstat
%define with_tape 0%{!?_without_tape:0}
# plugin tokyotyrant disabled, requires tcrdb.h
%define with_tokyotyrant 0%{!?_without_tokyotyrant:0}
# plugin write_kafka disabled, requires librdkafka
%define with_write_kafka 0%{!?_without_write_kafka:0}
# plugin write_mongodb disabled, requires libmongoc
%define with_write_mongodb 0%{!?_without_write_mongodb:0}
# plugin xmms disabled, requires xmms
%define with_xmms 0%{!?_without_xmms:0}

Summary:	statistics collection and monitoring daemon
Name:		collectd
Version:	5.5.0
Release:	1%{?dist}
URL:		http://collectd.org
Source:		http://collectd.org/files/%{name}-%{version}.tar.bz2
License:	GPLv2
Group:		System Environment/Daemons
BuildRoot:	%{_tmppath}/%{name}-%{version}-root
BuildRequires:	libgcrypt-devel, kernel-headers, libtool-ltdl-devel, libcap-devel
Vendor:		collectd development team <collectd@verplant.org>

%if 0%{?el7:1}
Requires(pre):		initscripts
Requires(post):		systemd
Requires(preun):	systemd
Requires(postun):	systemd
%else
Requires(post):		chkconfig
Requires(preun):	chkconfig, initscripts
Requires(postun):	initscripts
%endif

%description
collectd is a small daemon which collects system information periodically and
provides mechanisms to monitor and store the values in a variety of ways. It
is written in C for performance. Since the daemon doesn't need to start up
every time it wants to update the values it's very fast and easy on the
system. Also, the statistics are very fine grained since the files are updated
every 10 seconds by default.

%if %{with_amqp}
%package amqp
Summary:	AMQP plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	librabbitmq-devel
%description amqp
The AMQP plugin transmits or receives values collected by collectd via the
Advanced Message Queuing Protocol (AMQP).
%endif

%if %{with_apache}
%package apache
Summary:	Apache plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	curl-devel
%description apache
This plugin collects data provided by Apache's `mod_status'.
%endif

%if %{with_aquaero}
%package aquaero
Summary:	aquaero plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description aquaero
Various sensors in the Aquaero 5 watercooling board made by Aquacomputer.
%endif

%if %{with_ascent}
%package ascent
Summary:	Ascent plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libxml2-devel, curl-devel
%description ascent
The Ascent plugin reads and parses the statistics page of Ascent, a free and
open-source server software for the game World of Warcraft by Blizzard
Entertainment.
%endif

%if %{with_barometer}
%package barometer
Summary:       barometer plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
%description barometer
Collects pressure and temperature from digital barometers.
%endif

%if %{with_bind}
%package bind
Summary:	Bind plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libxml2-devel, curl-devel
%description bind
The BIND plugin retrieves this information that's encoded in XML and provided
via HTTP and submits the values to collectd.
%endif

%if %{with_ceph}
%package ceph
Summary:       Ceph plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
BuildRequires: yajl-devel
%description ceph
Ceph plugin for collectd
%endif

%if %{with_curl}
%package curl
Summary:	Curl plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	curl-devel
%description curl
The cURL plugin uses libcurl to read files and then parses them according to
the configuration.
%endif

%if %{with_curl_json}
%package curl_json
Summary:	Curl_json plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	curl-devel, yajl-devel
%description curl_json
The cURL-JSON plugin queries JavaScript Object Notation (JSON) data using the
cURL library and parses it according to the user's configuration.
%endif

%if %{with_curl_xml}
%package curl_xml
Summary:	Curl_xml plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	curl-devel, libxml2-devel
%description curl_xml
The cURL-XML plugin reads files using libcurl and parses it as Extensible
Markup Language (XML).
%endif

%if %{with_dbi}
%package dbi
Summary:	DBI plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libdbi-devel
%description dbi
The DBI plugin uses libdbi, a database abstraction library, to execute SQL
statements on a database and read back the result.
%endif

%if %{with_disk}
%package disk
Summary:	disk plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%{?_has_libudev:BuildRequires:  libudev-devel}
%description disk
The "disk" plugin collects information about the usage of physical disks and
logical disks (partitions).
%endif

%if %{with_dns}
%package dns
Summary:	DNS plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}, libpcap >= 1.0
BuildRequires:	libpcap-devel >= 1.0
%description dns
The DNS plugin has a similar functionality to dnstop: It uses libpcap to get a
copy of all traffic from/to port UDP/53 (that's the DNS port), interprets the
packets and collects statistics of your DNS traffic.
%endif

%if %{with_email}
%package email
Summary:	Email plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}, spamassassin
%description email
This plugin collects data provided by spamassassin.
%endif

%if %{with_gmond}
%package gmond
Summary:	Gmond plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	ganglia-devel
%description gmond
The gmond plugin subscribes to a Multicast group to receive data from gmond,
the client daemon of the Ganglia project.
%endif

%if %{with_hddtemp}
%package hddtemp
Summary:	Hddtemp plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}, hddtemp
%description hddtemp
The HDDTemp plugin collects the temperature of hard disks. The temperatures are
provided via SMART and queried by the external hddtemp daemon.
%endif

%if %{with_ipmi}
%package ipmi
Summary:	IPMI plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	OpenIPMI-devel
%description ipmi
The IPMI plugin uses the OpenIPMI library to read hardware sensors from servers
using the Intelligent Platform Management Interface (IPMI).
%endif

%if %{with_iptables}
%package iptables
Summary:	IPtables plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	iptables-devel
%description iptables
The IPtables plugin can gather statistics from your ip_tables based packet
filter (aka. firewall) for both the IPv4 and the IPv6 protocol. It can collect
the byte- and packet-counters of selected rules and submit them to collectd.
%endif

%if %{with_java}
%package java
Summary:	Java plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	java-devel, jpackage-utils
Requires:	java, jpackage-utils
%description java
This plugin for collectd allows plugins to be written in Java and executed
in an embedded JVM.
%endif

%if %{with_log_logstash}
%package log_logstash
Summary:       log_logstash plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
BuildRequires: yajl-devel
%description log_logstash
This plugin logs in logstash JSON format
%endif

%if %{with_lvm}
%package lvm
Summary:	LVM plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	lvm2-devel
%description lvm
This plugin collects size of “Logical Volumes” (LV) and “Volume Groups” (VG)
of Linux' “Logical Volume Manager” (LVM).
%endif

%if %{with_memcachec}
%package memcachec
Summary:	Memcachec plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libmemcached-devel
%description memcachec
The Memcachec plugin uses libmemcached to read statistics from a Memcached
instance. Note that another plugin, named `memcached', exists and does a
similar job, without requiring the installation of libmemcached.
%endif

%if %{with_mic}
%package mic
Summary:	mic plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description mic
The mic plugin collects CPU usage, memory usage, temperatures and power
consumption from Intel Many Integrated Core (MIC) CPUs.
%endif

%if %{with_modbus}
%package modbus
Summary:       modbus plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libmodbus-devel
%description modbus
The modbus plugin collects values from Modbus/TCP enabled devices
%endif

%if %{with_mysql}
%package mysql
Summary:	MySQL plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	mysql-devel
%description mysql
MySQL querying plugin. This plugin provides data of issued commands, called
handlers and database traffic.
%endif

%if %{with_netlink}
%package netlink
Summary:	netlink plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libmnl-devel, iproute-devel
%description netlink
The netlink plugin collects detailed network interface and routing statistics.
%endif

%if %{with_nginx}
%package nginx
Summary:	Nginx plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	curl-devel
%description nginx
This plugin gets data provided by nginx.
%endif

%if %{with_notify_desktop}
%package notify_desktop
Summary:	Notify_desktop plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libnotify-devel, gtk2-devel
%description notify_desktop
The Notify Desktop plugin uses libnotify to display notifications to the user
via the desktop notification specification, i. e. on an X display.
%endif

%if %{with_notify_email}
%package notify_email
Summary:	Notify_email plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libesmtp-devel
%description notify_email
The Notify Email plugin uses libESMTP to send notifications to a configured
email address.
%endif

%if %{with_nut}
%package nut
Summary:	Nut plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	nut-devel
%description nut
This plugin for collectd provides Network UPS Tools support.
%endif

%if %{with_openldap}
%package openldap
Summary:       Openldap plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
BuildRequires: openldap-devel
%description openldap
This plugin reads monitoring information from OpenLDAP's cn=Monitor subtree.
%endif

%if %{with_perl}
%package perl
Summary:	Perl plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	perl(:MODULE_COMPAT_%(eval "`%{__perl} -V:version`"; echo $version))
	%if 0%{?rhel} >= 6
BuildRequires:	perl-ExtUtils-Embed
	%else
BuildRequires:	perl
	%endif
%description perl
The Perl plugin embeds a Perl interpreter into collectd and exposes the
application programming interface (API) to Perl-scripts.
%endif

%if %{with_pinba}
%package pinba
Summary:	Pinba plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	protobuf-c-devel
%description pinba
The Pinba plugin receives and dispatches timing values from Pinba, a profiling
extension for PHP.
%endif

%if %{with_ping}
%package ping
Summary:	Ping plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	liboping-devel
%description ping
The Ping plugin measures network latency using ICMP “echo requests”, usually
known as “ping”.
%endif

%if %{with_postgresql}
%package postgresql
Summary:	PostgreSQL plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	postgresql-devel
%description postgresql
The PostgreSQL plugin connects to and executes SQL statements on a PostgreSQL
database.
%endif

%if %{with_python}
%package python
Summary:	Python plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
	%if 0%{?rhel} >= 6
BuildRequires: python-devel
	%else
BuildRequires: python26-devel
	%endif
%description python
The Python plugin embeds a Python interpreter into collectd and exposes the
application programming interface (API) to Python-scripts.
%endif

%if %{with_redis}
%package redis
Summary:	Redis plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	hiredis-devel
%description redis
The Redis plugin connects to one or more instances of Redis, a key-value store,
and collects usage information using the hiredis library.
%endif

%if %{with_rrdcached}
%package rrdcached
Summary:        RRDCached plugin for collectd
Group:          System Environment/Daemons
Requires:       %{name}%{?_isa} = %{version}-%{release}, rrdtool >= 1.4
BuildRequires:  rrdtool-devel
%description rrdcached
The RRDCacheD plugin connects to the “RRD caching daemon”, rrdcached and
submits updates for RRD files to that daemon.
%endif

%if %{with_rrdtool}
%package rrdtool
Summary:	RRDtool plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	rrdtool-devel
%description rrdtool
The RRDtool plugin writes values to RRD-files using librrd.
%endif

%if %{with_sensors}
%package sensors
Summary:	Sensors plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	lm_sensors-devel
%description sensors
This plugin for collectd provides querying of sensors supported by lm_sensors.
%endif

%if %{with_sigrok}
%package sigrok
Summary:	sigrok plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description sigrok
Uses libsigrok as a backend, allowing any sigrok-supported device to have its
measurements fed to collectd. This includes multimeters, sound level meters,
thermometers, and much more.
%endif

%if %{with_smart}
%package smart
Summary:       SMART plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
BuildRequires: libatasmart-devel
%description smart
Collect SMART statistics, notably load cycle count, temperature and bad
sectors.
%endif

%if %{with_snmp}
%package snmp
Summary:	SNMP plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	net-snmp-devel
%description snmp
This plugin for collectd allows querying of network equipment using SNMP.
%endif

%if %{with_varnish}
%package varnish
Summary:	Varnish plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	varnish-libs-devel
%description varnish
The Varnish plugin collects information about Varnish, an HTTP accelerator.
%endif

%if %{with_virt}
%package virt
Summary:	Virt plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libvirt-devel
%description virt
This plugin collects information from virtualized guests.
%endif

%if %{with_write_http}
%package write_http
Summary:	Write-HTTP plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	curl-devel
%description write_http
The Write-HTTP plugin sends the values collected by collectd to a web-server
using HTTP POST requests.
%endif

%if %{with_write_kafka}
%package write_kafka
Summary:       Write-kafka plugin for collectd
Group:         System Environment/Daemons
Requires:      %{name}%{?_isa} = %{version}-%{release}
BuildRequires: rdkafka-devel
%description write_kafka
The write_kafka plugin sends values to kafka, a distributed messaging system.
%endif

%if %{with_write_redis}
%package write_redis
Summary:	Write-Redis plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	hiredis-devel
%description write_redis
The Write Redis plugin stores values in Redis, a “data structures server”.
%endif

%if %{with_write_riemann}
%package write_riemann
Summary:	riemann plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	protobuf-c-devel
%description write_riemann
The riemann plugin submits values to Riemann, an event stream processor.
%endif

%package collection3
Summary:	Web-based viewer for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires: httpd
%description collection3
collection3 is a graphing front-end for the RRD files created by and filled
with collectd. It is written in Perl and should be run as an CGI-script.
Graphs are generated on-the-fly, so no cron job or similar is necessary.

%package php-collection
Summary:	collect php webfrontent
Group:		System Environment/Daemons
Requires:	collectd = %{version}-%{release}
Requires:	httpd
Requires:	php
Requires:	php-rrdtool
%description php-collection
PHP graphing frontend for RRD files created by and filled with collectd.

%package contrib
Summary:	Contrib files for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description contrib
All the files found under contrib/ in the source tree are bundled in this
package.

%package -n libcollectdclient
Summary:	Collectd client library
Group:		System Environment/Daemons
%description -n libcollectdclient
Collectd client library

%package -n libcollectdclient-devel
Summary:	Development files for libcollectdclient
Group:		System Environment/Daemons
Requires:	pkgconfig
Requires:	libcollectdclient%{?_isa} = %{version}-%{release}
%description -n libcollectdclient-devel
Development files for libcollectdclient

%package -n collectd-utils
Summary:	Collectd utilities
Group:		System Environment/Daemons
Requires:	libcollectdclient%{?_isa} = %{version}-%{release}
Requires:	collectd%{?_isa} = %{version}-%{release}
%description -n collectd-utils
Collectd utilities

%prep
%setup -q

%build
%if %{with_aggregation}
%define _with_aggregation --enable-aggregation
%else
%define _with_aggregation --disable-aggregation
%endif

%if %{with_amqp}
%define _with_amqp --enable-amqp
%else
%define _with_amqp --disable-amqp
%endif

%if %{with_apache}
%define _with_apache --enable-apache
%else
%define _with_apache --disable-apache
%endif

%if %{with_apcups}
%define _with_apcups --enable-apcups
%else
%define _with_apcups --disable-apcups
%endif

%if %{with_apple_sensors}
%define _with_apple_sensors --enable-apple_sensors
%else
%define _with_apple_sensors --disable-apple_sensors
%endif

%if %{with_aquaero}
%define _with_aquaero --enable-aquaero
%else
%define _with_aquaero --disable-aquaero
%endif

%if %{with_ascent}
%define _with_ascent --enable-ascent
%else
%define _with_ascent --disable-ascent
%endif

%if %{with_barometer}
%define _with_barometer --enable-barometer
%else
%define _with_barometer --disable-barometer
%endif

%if %{with_battery}
%define _with_battery --enable-battery
%else
%define _with_battery --disable-battery
%endif

%if %{with_bind}
%define _with_bind --enable-bind
%else
%define _with_bind --disable-bind
%endif

%if %{with_cgroups}
%define _with_cgroups --enable-cgroups
%else
%define _with_cgroups --disable-cgroups
%endif

%if %{with_conntrack}
%define _with_conntrack --enable-conntrack
%else
%define _with_conntrack --disable-conntrack
%endif

%if %{with_contextswitch}
%define _with_contextswitch --enable-contextswitch
%else
%define _with_contextswitch --disable-contextswitch
%endif

%if %{with_cpu}
%define _with_cpu --enable-cpu
%else
%define _with_cpu --disable-cpu
%endif

%if %{with_cpufreq}
%define _with_cpufreq --enable-cpufreq
%else
%define _with_cpufreq --disable-cpufreq
%endif

%if %{with_csv}
%define _with_csv --enable-csv
%else
%define _with_csv --disable-csv
%endif

%if %{with_ceph}
%define _with_ceph --enable-ceph
%else
%define _with_ceph --disable-ceph
%endif

%if %{with_curl}
%define _with_curl --enable-curl
%else
%define _with_curl --disable-curl
%endif

%if %{with_curl_json}
%define _with_curl_json --enable-curl_json
%else
%define _with_curl_json --disable-curl_json
%endif

%if %{with_curl_xml}
%define _with_curl_xml --enable-curl_xml
%else
%define _with_curl_xml --disable-curl_xml
%endif

%if %{with_dbi}
%define _with_dbi --enable-dbi
%else
%define _with_dbi --disable-dbi
%endif

%if %{with_df}
%define _with_df --enable-df
%else
%define _with_df --disable-df
%endif

%if %{with_disk}
%define _with_disk --enable-disk
%else
%define _with_disk --disable-disk
%endif

%if %{with_dns}
%define _with_dns --enable-dns
%else
%define _with_dns --disable-dns
%endif

%if %{with_drbd}
%define _with_drbd --enable-drbd
%else
%define _with_drbd --disable-drbd
%endif

%if %{with_email}
%define _with_email --enable-email
%else
%define _with_email --disable-email
%endif

%if %{with_entropy}
%define _with_entropy --enable-entropy
%else
%define _with_entropy --disable-entropy
%endif

%if %{with_ethstat}
%define _with_ethstat --enable-ethstat
%else
%define _with_ethstat --disable-ethstat
%endif

%if %{with_exec}
%define _with_exec --enable-exec
%else
%define _with_exec --disable-exec
%endif

%if %{with_fhcount}
%define _with_fhcount --enable-fhcount
%else
%define _with_fhcount --disable-fhcount
%endif

%if %{with_filecount}
%define _with_filecount --enable-filecount
%else
%define _with_filecount --disable-filecount
%endif

%if %{with_fscache}
%define _with_fscache --enable-fscache
%else
%define _with_fscache --disable-fscache
%endif

%if %{with_gmond}
%define _with_gmond --enable-gmond
%else
%define _with_gmond --disable-gmond
%endif

%if %{with_hddtemp}
%define _with_hddtemp --enable-hddtemp
%else
%define _with_hddtemp --disable-hddtemp
%endif

%if %{with_interface}
%define _with_interface --enable-interface
%else
%define _with_interface --disable-interface
%endif

%if %{with_ipc}
%define _with_ipc --enable-ipc
%else
%define _with_ipc --disable-ipc
%endif

%if %{with_ipmi}
%define _with_ipmi --enable-ipmi
%else
%define _with_ipmi --disable-ipmi
%endif

%if %{with_iptables}
%define _with_iptables --enable-iptables
%else
%define _with_iptables --disable-iptables
%endif

%if %{with_ipvs}
%define _with_ipvs --enable-ipvs
%else
%define _with_ipvs --disable-ipvs
%endif

%if %{with_irq}
%define _with_irq --enable-irq
%else
%define _with_irq --disable-irq
%endif

%if %{with_java}
%define _with_java --enable-java --with-java=%{java_home}/
%else
%define _with_java --disable-java
%endif

%if %{with_virt}
%define _with_virt --enable-virt
%else
%define _with_virt --disable-virt
%endif

%if %{with_load}
%define _with_load --enable-load
%else
%define _with_load --disable-load
%endif

%if %{with_logfile}
%define _with_logfile --enable-logfile
%else
%define _with_logfile --disable-logfile
%endif

%if %{with_log_logstash}
%define _with_log_logstash --enable-log_logstash
%else
%define _with_log_logstash --disable-log_logstash
%endif

%if %{with_lpar}
%define _with_lpar --enable-lpar
%else
%define _with_lpar --disable-lpar
%endif

%if %{with_lvm}
%define _with_lvm --enable-lvm
%else
%define _with_lvm --disable-lvm
%endif

%if %{with_madwifi}
%define _with_madwifi --enable-madwifi
%else
%define _with_madwifi --disable-madwifi
%endif

%if %{with_mbmon}
%define _with_mbmon --enable-mbmon
%else
%define _with_mbmon --disable-mbmon
%endif

%if %{with_md}
%define _with_md --enable-md
%else
%define _with_md --disable-md
%endif

%if %{with_memcachec}
%define _with_memcachec --enable-memcachec
%else
%define _with_memcachec --disable-memcachec
%endif

%if %{with_memcached}
%define _with_memcached --enable-memcached
%else
%define _with_memcached --disable-memcached
%endif

%if %{with_memory}
%define _with_memory --enable-memory
%else
%define _with_memory --disable-memory
%endif

%if %{with_mic}
%define _with_mic --enable-mic
%else
%define _with_mic --disable-mic
%endif

%if %{with_modbus}
%define _with_modbus --enable-modbus
%else
%define _with_modbus --disable-modbus
%endif

%if %{with_multimeter}
%define _with_multimeter --enable-multimeter
%else
%define _with_multimeter --disable-multimeter
%endif

%if %{with_mysql}
%define _with_mysql --enable-mysql
%else
%define _with_mysql --disable-mysql
%endif

%if %{with_netapp}
%define _with_netapp --enable-netapp
%else
%define _with_netapp --disable-netapp
%endif

%if %{with_netlink}
%define _with_netlink --enable-netlink
%else
%define _with_netlink --disable-netlink
%endif

%if %{with_network}
%define _with_network --enable-network
%else
%define _with_network --disable-network
%endif

%if %{with_nfs}
%define _with_nfs --enable-nfs
%else
%define _with_nfs --disable-nfs
%endif

%if %{with_nginx}
%define _with_nginx --enable-nginx
%else
%define _with_nginx --disable-nginx
%endif

%if %{with_notify_desktop}
%define _with_notify_desktop --enable-notify_desktop
%else
%define _with_notify_desktop --disable-notify_desktop
%endif

%if %{with_notify_email}
%define _with_notify_email --enable-notify_email
%else
%define _with_notify_email --disable-notify_email
%endif

%if %{with_ntpd}
%define _with_ntpd --enable-ntpd
%else
%define _with_ntpd --disable-ntpd
%endif

%if %{with_numa}
%define _with_numa --enable-numa
%else
%define _with_numa --disable-numa
%endif

%if %{with_nut}
%define _with_nut --enable-nut
%else
%define _with_nut --disable-nut
%endif

%if %{with_olsrd}
%define _with_olsrd --enable-olsrd
%else
%define _with_olsrd --disable-olsrd
%endif

%if %{with_onewire}
%define _with_onewire --enable-onewire
%else
%define _with_onewire --disable-onewire
%endif

%if %{with_openldap}
%define _with_openldap --enable-openldap
%else
%define _with_openldap --disable-openldap
%endif

%if %{with_openvpn}
%define _with_openvpn --enable-openvpn
%else
%define _with_openvpn --disable-openvpn
%endif

%if %{with_oracle}
%define _with_oracle --enable-oracle
%else
%define _with_oracle --disable-oracle
%endif

%if %{with_perl}
%define _with_perl --enable-perl --with-perl-bindings="INSTALLDIRS=vendor"
%else
%define _with_perl --disable-perl
%endif

%if %{with_pf}
%define _with_pf --enable-pf
%else
%define _with_pf --disable-pf
%endif

%if %{with_pinba}
%define _with_pinba --enable-pinba
%else
%define _with_pinba --disable-pinba
%endif

%if %{with_ping}
%define _with_ping --enable-ping
%else
%define _with_ping --disable-ping
%endif

%if %{with_postgresql}
%define _with_postgresql --enable-postgresql
%else
%define _with_postgresql --disable-postgresql
%endif

%if %{with_powerdns}
%define _with_powerdns --enable-powerdns
%else
%define _with_powerdns --disable-powerdns
%endif

%if %{with_processes}
%define _with_processes --enable-processes
%else
%define _with_processes --disable-processes
%endif

%if %{with_protocols}
%define _with_protocols --enable-protocols
%else
%define _with_protocols --disable-protocols
%endif

%if %{with_python}
	%if 0%{?rhel} >= 6
%define _with_python --enable-python
	%else
%define _with_python --enable-python --with-python=%{_bindir}/python2.6
	%endif
%else
%define _with_python --disable-python
%endif

%if %{with_redis}
%define _with_redis --enable-redis
%else
%define _with_redis --disable-redis
%endif

%if %{with_routeros}
%define _with_routeros --enable-routeros
%else
%define _with_routeros --disable-routeros
%endif

%if %{with_rrdcached}
%define _with_rrdcached --enable-rrdcached
%else
%define _with_rrdcached --disable-rrdcached
%endif

%if %{with_rrdtool}
%define _with_rrdtool --enable-rrdtool
%else
%define _with_rrdtool --disable-rrdtool
%endif

%if %{with_sensors}
%define _with_sensors --enable-sensors
%else
%define _with_sensors --disable-sensors
%endif

%if %{with_serial}
%define _with_serial --enable-serial
%else
%define _with_serial --disable-serial
%endif

%if %{with_sigrok}
%define _with_sigrok --enable-sigrok
%else
%define _with_sigrok --disable-sigrok
%endif

%if %{with_smart}
%define _with_smart --enable-smart
%else
%define _with_smart --disable-smart
%endif

%if %{with_snmp}
%define _with_snmp --enable-snmp
%else
%define _with_snmp --disable-snmp
%endif

%if %{with_statsd}
%define _with_statsd --enable-statsd
%else
%define _with_statsd --disable-statsd
%endif

%if %{with_swap}
%define _with_swap --enable-swap
%else
%define _with_swap --disable-swap
%endif

%if %{with_syslog}
%define _with_syslog --enable-syslog
%else
%define _with_syslog --disable-syslog
%endif

%if %{with_table}
%define _with_table --enable-table
%else
%define _with_table --disable-table
%endif

%if %{with_tail}
%define _with_tail --enable-tail
%else
%define _with_tail --disable-tail
%endif

%if %{with_tail_csv}
%define _with_tail_csv --enable-tail_csv
%else
%define _with_tail_csv --disable-tail_csv
%endif

%if %{with_tape}
%define _with_tape --enable-tape
%else
%define _with_tape --disable-tape
%endif

%if %{with_tcpconns}
%define _with_tcpconns --enable-tcpconns
%else
%define _with_tcpconns --disable-tcpconns
%endif

%if %{with_teamspeak2}
%define _with_teamspeak2 --enable-teamspeak2
%else
%define _with_teamspeak2 --disable-teamspeak2
%endif

%if %{with_ted}
%define _with_ted --enable-ted
%else
%define _with_ted --disable-ted
%endif

%if %{with_thermal}
%define _with_thermal --enable-thermal
%else
%define _with_thermal --disable-thermal
%endif

%if %{with_threshold}
%define _with_threshold --enable-threshold
%else
%define _with_threshold --disable-threshold
%endif

%if %{with_tokyotyrant}
%define _with_tokyotyrant --enable-tokyotyrant
%else
%define _with_tokyotyrant --disable-tokyotyrant
%endif

%if %{with_turbostat}
%define _with_turbostat --enable-turbostat
%else
%define _with_turbostat --disable-turbostat
%endif

%if %{with_unixsock}
%define _with_unixsock --enable-unixsock
%else
%define _with_unixsock --disable-unixsock
%endif

%if %{with_uptime}
%define _with_uptime --enable-uptime
%else
%define _with_uptime --disable-uptime
%endif

%if %{with_users}
%define _with_users --enable-users
%else
%define _with_users --disable-users
%endif

%if %{with_uuid}
%define _with_uuid --enable-uuid
%else
%define _with_uuid --disable-uuid
%endif

%if %{with_varnish}
%define _with_varnish --enable-varnish
%else
%define _with_varnish --disable-varnish
%endif

%if %{with_vmem}
%define _with_vmem --enable-vmem
%else
%define _with_vmem --disable-vmem
%endif

%if %{with_vserver}
%define _with_vserver --enable-vserver
%else
%define _with_vserver --disable-vserver
%endif

%if %{with_wireless}
%define _with_wireless --enable-wireless
%else
%define _with_wireless --disable-wireless
%endif

%if %{with_write_graphite}
%define _with_write_graphite --enable-write_graphite
%else
%define _with_write_graphite --disable-write_graphite
%endif

%if %{with_write_http}
%define _with_write_http --enable-write_http
%else
%define _with_write_http --disable-write_http
%endif

%if %{with_write_kafka}
%define _with_write_kafka --enable-write_kafka
%else
%define _with_write_kafka --disable-write_kafka
%endif

%if %{with_write_log}
%define _with_write_log --enable-write_log
%else
%define _with_write_log --disable-write_log
%endif

%if %{with_write_mongodb}
%define _with_write_mongodb --enable-write_mongodb
%else
%define _with_write_mongodb --disable-write_mongodb
%endif

%if %{with_write_redis}
%define _with_write_redis --enable-write_redis
%else
%define _with_write_redis --disable-write_redis
%endif

%if %{with_write_riemann}
%define _with_write_riemann --enable-write_riemann
%else
%define _with_write_riemann --disable-write_riemann
%endif

%if %{with_write_sensu}
%define _with_write_sensu --enable-write_sensu
%else
%define _with_write_sensu --disable-write_sensu
%endif

%if %{with_write_tsdb}
%define _with_write_tsdb --enable-write_tsdb
%else
%define _with_write_tsdb --disable-write_tsdb
%endif

%if %{with_xmms}
%define _with_xmms --enable-xmms
%else
%define _with_xmms --disable-xmms
%endif

%if %{with_zfs_arc}
%define _with_zfs_arc --enable-zfs_arc
%else
%define _with_zfs_arc --disable-zfs_arc
%endif

%if %{with_zookeeper}
%define _with_zookeeper --enable-zookeeper
%else
%define _with_zookeeper --disable-zookeeper
%endif

%configure CFLAGS="%{optflags} -DLT_LAZY_OR_NOW=\"RTLD_LAZY|RTLD_GLOBAL\"" \
	--disable-static \
	--without-included-ltdl \
	--enable-all-plugins=yes \
	--enable-match_empty_counter \
	--enable-match_hashed \
	--enable-match_regex \
	--enable-match_timediff \
	--enable-match_value \
	--enable-target_notification \
	--enable-target_replace \
	--enable-target_scale \
	--enable-target_set \
	--enable-target_v5upgrade \
	%{?_with_aggregation} \
	%{?_with_amqp} \
	%{?_with_apache} \
	%{?_with_apcups} \
	%{?_with_apple_sensors} \
	%{?_with_aquaero} \
	%{?_with_ascent} \
	%{?_with_barometer} \
	%{?_with_battery} \
	%{?_with_bind} \
	%{?_with_ceph} \
	%{?_with_cgroups} \
	%{?_with_conntrack} \
	%{?_with_contextswitch} \
	%{?_with_cpu} \
	%{?_with_cpufreq} \
	%{?_with_csv} \
	%{?_with_curl} \
	%{?_with_curl_json} \
	%{?_with_curl_xml} \
	%{?_with_dbi} \
	%{?_with_df} \
	%{?_with_disk} \
	%{?_with_dns} \
	%{?_with_drbd} \
	%{?_with_email} \
	%{?_with_entropy} \
	%{?_with_ethstat} \
	%{?_with_exec} \
	%{?_with_fhcount} \
	%{?_with_filecount} \
	%{?_with_fscache} \
	%{?_with_gmond} \
	%{?_with_hddtemp} \
	%{?_with_interface} \
	%{?_with_ipc} \
	%{?_with_ipmi} \
	%{?_with_iptables} \
	%{?_with_ipvs} \
	%{?_with_java} \
	%{?_with_virt} \
	%{?_with_log_logstash} \
	%{?_with_lpar} \
	%{?_with_lvm} \
	%{?_with_memcachec} \
	%{?_with_mic} \
	%{?_with_modbus} \
	%{?_with_multimeter} \
	%{?_with_mysql} \
	%{?_with_netapp} \
	%{?_with_netlink} \
	%{?_with_nginx} \
	%{?_with_notify_desktop} \
	%{?_with_notify_email} \
	%{?_with_nut} \
	%{?_with_onewire} \
	%{?_with_openldap} \
	%{?_with_oracle} \
	%{?_with_perl} \
	%{?_with_pf} \
	%{?_with_pinba} \
	%{?_with_ping} \
	%{?_with_postgresql} \
	%{?_with_python} \
	%{?_with_redis} \
	%{?_with_routeros} \
	%{?_with_rrdcached} \
	%{?_with_rrdtool} \
	%{?_with_sensors} \
	%{?_with_sigrok} \
	%{?_with_smart} \
	%{?_with_snmp} \
	%{?_with_tape} \
	%{?_with_tokyotyrant} \
	%{?_with_varnish} \
	%{?_with_write_http} \
	%{?_with_write_kafka} \
	%{?_with_write_mongodb} \
	%{?_with_write_redis} \
	%{?_with_xmms} \
	%{?_with_zfs_arc} \
	%{?_with_zookeeper} \
	%{?_with_irq} \
	%{?_with_load} \
	%{?_with_logfile} \
	%{?_with_madwifi} \
	%{?_with_mbmon} \
	%{?_with_md} \
	%{?_with_memcached} \
	%{?_with_memory} \
	%{?_with_network} \
	%{?_with_nfs} \
	%{?_with_ntpd} \
	%{?_with_numa} \
	%{?_with_olsrd} \
	%{?_with_openvpn} \
	%{?_with_powerdns} \
	%{?_with_processes} \
	%{?_with_protocols} \
	%{?_with_serial} \
	%{?_with_statsd} \
	%{?_with_swap} \
	%{?_with_syslog} \
	%{?_with_table} \
	%{?_with_tail} \
	%{?_with_tail_csv} \
	%{?_with_tcpconns} \
	%{?_with_teamspeak2} \
	%{?_with_ted} \
	%{?_with_thermal} \
	%{?_with_threshold} \
	%{?_with_turbostat} \
	%{?_with_unixsock} \
	%{?_with_uptime} \
	%{?_with_users} \
	%{?_with_uuid} \
	%{?_with_vmem} \
	%{?_with_vserver} \
	%{?_with_wireless}\
	%{?_with_write_graphite} \
	%{?_with_write_http} \
	%{?_with_write_log} \
	%{?_with_write_riemann} \
	%{?_with_write_sensu} \
	%{?_with_write_tsdb}


%{__make} %{?_smp_mflags}


%install
rm -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot}
%if 0%{?el7:1}
%{__install} -Dp -m0644 contrib/systemd.collectd.service %{buildroot}%{_unitdir}/collectd.service
%else
%{__install} -Dp -m0755 contrib/redhat/init.d-collectd %{buildroot}%{_initrddir}/collectd
%endif
%{__install} -Dp -m0644 src/collectd.conf %{buildroot}%{_sysconfdir}/collectd.conf
%{__install} -d %{buildroot}%{_sharedstatedir}/collectd/
%{__install} -d %{buildroot}%{_sysconfdir}/collectd.d/

%{__mkdir} -p %{buildroot}%{_localstatedir}/www
%{__mkdir} -p %{buildroot}/%{_sysconfdir}/httpd/conf.d

%{__mv} contrib/collection3 %{buildroot}%{_localstatedir}/www
%{__mv} contrib/redhat/collection3.conf %{buildroot}/%{_sysconfdir}/httpd/conf.d/

%{__mv} contrib/php-collection %{buildroot}%{_localstatedir}/www
%{__mv} contrib/redhat/php-collection.conf %{buildroot}/%{_sysconfdir}/httpd/conf.d/

### Clean up docs
find contrib/ -type f -exec %{__chmod} a-x {} \;
# *.la files shouldn't be distributed.
rm -f %{buildroot}/%{_libdir}/{collectd/,}*.la

# Remove Perl hidden .packlist files.
find %{buildroot} -type f -name .packlist -delete
# Remove Perl temporary file perllocal.pod
find %{buildroot} -type f -name perllocal.pod -delete

%if ! %{with_java}
rm -f %{buildroot}%{_datadir}/collectd/java/collectd-api.jar
rm -f %{buildroot}%{_datadir}/collectd/java/generic-jmx.jar
rm -f %{buildroot}%{_mandir}/man5/collectd-java.5*
%endif

%if ! %{with_perl}
rm -f %{buildroot}%{_mandir}/man5/collectd-perl.5*
rm -f %{buildroot}%{_mandir}/man3/Collectd::Unixsock.3pm*
rm -fr %{buildroot}/usr/lib/perl5/
%endif

%if ! %{with_postgresql}
rm -f %{buildroot}%{_datadir}/collectd/postgresql_default.conf
%endif

%if ! %{with_python}
rm -f %{buildroot}%{_mandir}/man5/collectd-python.5*
%endif

%if ! %{with_snmp}
rm -f %{buildroot}%{_mandir}/man5/collectd-snmp.5*
%endif


%clean
rm -rf %{buildroot}

%pre
%if 0%{?el7:1}
# stop sysv-based instance before upgrading to systemd
if [ $1 -eq 2 ] && [ -f /var/lock/subsys/collectd ]; then
	SYSTEMCTL_SKIP_REDIRECT=1 %{_initddir}/collectd stop >/dev/null 2>&1 || :
fi
%endif

%post
%if 0%{?el7:1}
if [ $1 -eq 2 ]; then
	/usr/bin/systemctl daemon-reload >/dev/null 2>&1 || :
fi
%systemd_post collectd.service
%else
/sbin/chkconfig --add collectd || :
%endif

%preun
%if 0%{?el7:1}
%systemd_preun collectd.service
%else
# stop collectd only when uninstalling
if [ $1 -eq 0 ]; then
	/sbin/service collectd stop >/dev/null 2>&1 || :
	/sbin/chkconfig --del collectd || :
fi
%endif

%postun
%if 0%{?el7:1}
%systemd_postun_with_restart collectd.service
%else
# restart collectd only when upgrading
if [ $1 -eq 1 ]; then
	/sbin/service collectd condrestart >/dev/null 2>&1 || :
fi
%endif

%post -n libcollectdclient -p /sbin/ldconfig
%postun -n libcollectdclient -p /sbin/ldconfig


%files
%doc AUTHORS COPYING ChangeLog README
%config(noreplace) %{_sysconfdir}/collectd.conf
%if 0%{?el7:1}
%{_unitdir}/collectd.service
%else
%{_initrddir}/collectd
%endif
%{_sbindir}/collectd
%{_sbindir}/collectdmon
%{_datadir}/collectd/types.db
%{_sharedstatedir}/collectd
%{_mandir}/man1/collectd.1*
%{_mandir}/man1/collectdmon.1*
%{_mandir}/man5/collectd-email.5*
%{_mandir}/man5/collectd-exec.5*
%{_mandir}/man5/collectd-threshold.5*
%{_mandir}/man5/collectd-unixsock.5*
%{_mandir}/man5/collectd.conf.5*
%{_mandir}/man5/types.db.5*

# all plugins bundled with the main collectd package
%{_libdir}/%{name}/match_empty_counter.so
%{_libdir}/%{name}/match_hashed.so
%{_libdir}/%{name}/match_regex.so
%{_libdir}/%{name}/match_timediff.so
%{_libdir}/%{name}/match_value.so
%{_libdir}/%{name}/target_notification.so
%{_libdir}/%{name}/target_replace.so
%{_libdir}/%{name}/target_scale.so
%{_libdir}/%{name}/target_set.so
%{_libdir}/%{name}/target_v5upgrade.so

%if %{with_aggregation}
%{_libdir}/%{name}/aggregation.so
%endif
%if %{with_apcups}
%{_libdir}/%{name}/apcups.so
%endif
%if %{with_battery}
%{_libdir}/%{name}/battery.so
%endif
%if %{with_cgroups}
%{_libdir}/%{name}/cgroups.so
%endif
%if %{with_conntrack}
%{_libdir}/%{name}/conntrack.so
%endif
%if %{with_contextswitch}
%{_libdir}/%{name}/contextswitch.so
%endif
%if %{with_cpu}
%{_libdir}/%{name}/cpu.so
%endif
%if %{with_cpufreq}
%{_libdir}/%{name}/cpufreq.so
%endif
%if %{with_csv}
%{_libdir}/%{name}/csv.so
%endif
%if %{with_df}
%{_libdir}/%{name}/df.so
%endif
%if %{with_drbd}
%{_libdir}/%{name}/drbd.so
%endif
%if %{with_ethstat}
%{_libdir}/%{name}/ethstat.so
%endif
%if %{with_entropy}
%{_libdir}/%{name}/entropy.so
%endif
%if %{with_exec}
%{_libdir}/%{name}/exec.so
%endif
%if %{with_fhcount}
%{_libdir}/%{name}/fhcount.so
%endif
%if %{with_filecount}
%{_libdir}/%{name}/filecount.so
%endif
%if %{with_fscache}
%{_libdir}/%{name}/fscache.so
%endif
%if %{with_interface}
%{_libdir}/%{name}/interface.so
%endif
%if %{with_ipc}
%{_libdir}/%{name}/ipc.so
%endif
%if %{with_ipvs}
%{_libdir}/%{name}/ipvs.so
%endif
%if %{with_irq}
%{_libdir}/%{name}/irq.so
%endif
%if %{with_load}
%{_libdir}/%{name}/load.so
%endif
%if %{with_logfile}
%{_libdir}/%{name}/logfile.so
%endif
%if %{with_madwifi}
%{_libdir}/%{name}/madwifi.so
%endif
%if %{with_mbmon}
%{_libdir}/%{name}/mbmon.so
%endif
%if %{with_md}
%{_libdir}/%{name}/md.so
%endif
%if %{with_memcached}
%{_libdir}/%{name}/memcached.so
%endif
%if %{with_memory}
%{_libdir}/%{name}/memory.so
%endif
%if %{with_multimeter}
%{_libdir}/%{name}/multimeter.so
%endif
%if %{with_network}
%{_libdir}/%{name}/network.so
%endif
%if %{with_nfs}
%{_libdir}/%{name}/nfs.so
%endif
%if %{with_ntpd}
%{_libdir}/%{name}/ntpd.so
%endif
%if %{with_numa}
%{_libdir}/%{name}/numa.so
%endif
%if %{with_openvpn}
%{_libdir}/%{name}/openvpn.so
%endif
%if %{with_olsrd}
%{_libdir}/%{name}/olsrd.so
%endif
%if %{with_powerdns}
%{_libdir}/%{name}/powerdns.so
%endif
%if %{with_processes}
%{_libdir}/%{name}/processes.so
%endif
%if %{with_protocols}
%{_libdir}/%{name}/protocols.so
%endif
%if %{with_serial}
%{_libdir}/%{name}/serial.so
%endif
%if %{with_statsd}
%{_libdir}/%{name}/statsd.so
%endif
%if %{with_swap}
%{_libdir}/%{name}/swap.so
%endif
%if %{with_syslog}
%{_libdir}/%{name}/syslog.so
%endif
%if %{with_table}
%{_libdir}/%{name}/table.so
%endif
%if %{with_tail}
%{_libdir}/%{name}/tail.so
%endif
%if %{with_tail_csv}
%{_libdir}/%{name}/tail_csv.so
%endif
%if %{with_tcpconns}
%{_libdir}/%{name}/tcpconns.so
%endif
%if %{with_teamspeak2}
%{_libdir}/%{name}/teamspeak2.so
%endif
%if %{with_ted}
%{_libdir}/%{name}/ted.so
%endif
%if %{with_thermal}
%{_libdir}/%{name}/thermal.so
%endif
%if %{with_threshold}
%{_libdir}/%{name}/threshold.so
%endif
%if %{with_turbostat}
%{_libdir}/%{name}/turbostat.so
%endif
%if %{with_unixsock}
%{_libdir}/%{name}/unixsock.so
%endif
%if %{with_uptime}
%{_libdir}/%{name}/uptime.so
%endif
%if %{with_users}
%{_libdir}/%{name}/users.so
%endif
%if %{with_uuid}
%{_libdir}/%{name}/uuid.so
%endif
%if %{with_vmem}
%{_libdir}/%{name}/vmem.so
%endif
%if %{with_vserver}
%{_libdir}/%{name}/vserver.so
%endif
%if %{with_wireless}
%{_libdir}/%{name}/wireless.so
%endif
%if %{with_write_graphite}
%{_libdir}/%{name}/write_graphite.so
%endif
%if %{with_write_log}
%{_libdir}/%{name}/write_log.so
%endif
%if %{with_write_sensu}
%{_libdir}/%{name}/write_sensu.so
%endif
%if %{with_write_tsdb}
%{_libdir}/%{name}/write_tsdb.so
%endif
%if %{with_zfs_arc}
%{_libdir}/%{name}/zfs_arc.so
%endif
%if %{with_zookeeper}
%{_libdir}/%{name}/zookeeper.so
%endif

%files -n libcollectdclient-devel
%{_includedir}/collectd/client.h
%{_includedir}/collectd/network.h
%{_includedir}/collectd/network_buffer.h
%{_includedir}/collectd/lcc_features.h
%{_libdir}/pkgconfig/libcollectdclient.pc

%files -n libcollectdclient
%{_libdir}/libcollectdclient.so
%{_libdir}/libcollectdclient.so.*

%files -n collectd-utils
%{_bindir}/collectd-nagios
%{_bindir}/collectd-tg
%{_bindir}/collectdctl
%{_mandir}/man1/collectdctl.1*
%{_mandir}/man1/collectd-nagios.1*
%{_mandir}/man1/collectd-tg.1*

%if %{with_amqp}
%files amqp
%{_libdir}/%{name}/amqp.so
%endif

%if %{with_apache}
%files apache
%{_libdir}/%{name}/apache.so
%endif

%if %{with_aquaero}
%files aquaero
%{_libdir}/%{name}/aquaero.so
%endif

%if %{with_ascent}
%files ascent
%{_libdir}/%{name}/ascent.so
%endif

%if %{with_barometer}
%files barometer
%{_libdir}/%{name}/barometer.so
%endif

%if %{with_bind}
%files bind
%{_libdir}/%{name}/bind.so
%endif

%if %{with_ceph}
%files ceph
%{_libdir}/%{name}/ceph.so
%endif

%if %{with_curl}
%files curl
%{_libdir}/%{name}/curl.so
%endif

%if %{with_curl_json}
%files curl_json
%{_libdir}/%{name}/curl_json.so
%endif

%if %{with_curl_xml}
%files curl_xml
%{_libdir}/%{name}/curl_xml.so
%endif

%if %{with_disk}
%files disk
%{_libdir}/%{name}/disk.so
%endif

%if %{with_dns}
%files dns
%{_libdir}/%{name}/dns.so
%endif

%if %{with_dbi}
%files dbi
%{_libdir}/%{name}/dbi.so
%endif

%if %{with_email}
%files email
%{_libdir}/%{name}/email.so
%endif

%if %{with_gmond}
%files gmond
%{_libdir}/%{name}/gmond.so
%endif

%if %{with_hddtemp}
%files hddtemp
%{_libdir}/%{name}/hddtemp.so
%endif

%if %{with_ipmi}
%files ipmi
%{_libdir}/%{name}/ipmi.so
%endif

%if %{with_iptables}
%files iptables
%{_libdir}/%{name}/iptables.so
%endif

%if %{with_java}
%files java
%{_datadir}/collectd/java/collectd-api.jar
%{_datadir}/collectd/java/generic-jmx.jar
%{_libdir}/%{name}/java.so
%{_mandir}/man5/collectd-java.5*
%endif

%if %{with_virt}
%files virt
%{_libdir}/%{name}/virt.so
%endif

%if %{with_log_logstash}
%files log_logstash
%{_libdir}/%{name}/log_logstash.so
%endif

%if %{with_lvm}
%files lvm
%{_libdir}/%{name}/lvm.so
%endif

%if %{with_memcachec}
%files memcachec
%{_libdir}/%{name}/memcachec.so
%endif

%if %{with_mic}
%files mic
%{_libdir}/%{name}/mic.so
%endif

%if %{with_modbus}
%files modbus
%{_libdir}/%{name}/modbus.so
%endif

%if %{with_mysql}
%files mysql
%{_libdir}/%{name}/mysql.so
%endif

%if %{with_netlink}
%files netlink
%{_libdir}/%{name}/netlink.so
%endif

%if %{with_nginx}
%files nginx
%{_libdir}/%{name}/nginx.so
%endif

%if %{with_notify_desktop}
%files notify_desktop
%{_libdir}/%{name}/notify_desktop.so
%endif

%if %{with_notify_email}
%files notify_email
%{_libdir}/%{name}/notify_email.so
%endif

%if %{with_nut}
%files nut
%{_libdir}/%{name}/nut.so
%endif

%if %{with_openldap}
%files openldap
%{_libdir}/%{name}/openldap.so
%endif

%if %{with_perl}
%files perl
%{perl_vendorlib}/Collectd.pm
%{perl_vendorlib}/Collectd/
%{_mandir}/man3/Collectd::Unixsock.3pm*
%{_mandir}/man5/collectd-perl.5*
%{_libdir}/%{name}/perl.so
%endif

%if %{with_pinba}
%files pinba
%{_libdir}/%{name}/pinba.so
%endif

%if %{with_ping}
%files ping
%{_libdir}/%{name}/ping.so
%endif

%if %{with_postgresql}
%files postgresql
%{_datadir}/collectd/postgresql_default.conf
%{_libdir}/%{name}/postgresql.so
%endif

%if %{with_python}
%files python
%{_mandir}/man5/collectd-python*
%{_libdir}/%{name}/python.so
%endif

%if %{with_redis}
%files redis
%{_libdir}/%{name}/redis.so
%endif

%if %{with_rrdcached}
%files rrdcached
%{_libdir}/%{name}/rrdcached.so
%endif

%if %{with_rrdtool}
%files rrdtool
%{_libdir}/%{name}/rrdtool.so
%endif

%if %{with_sensors}
%files sensors
%{_libdir}/%{name}/sensors.so
%endif

%if %{with_sigrok}
%files sigrok
%{_libdir}/%{name}/sigrok.so
%endif

%if %{with_smart}
%files smart
%{_libdir}/%{name}/smart.so
%endif

%if %{with_snmp}
%files snmp
%{_mandir}/man5/collectd-snmp.5*
%{_libdir}/%{name}/snmp.so
%endif

%if %{with_varnish}
%files varnish
%{_libdir}/%{name}/varnish.so
%endif

%if %{with_write_http}
%files write_http
%{_libdir}/%{name}/write_http.so
%endif

%if %{with_write_kafka}
%files write_kafka
%{_libdir}/%{name}/write_kafka.so
%endif

%if %{with_write_redis}
%files write_redis
%{_libdir}/%{name}/write_redis.so
%endif

%if %{with_write_riemann}
%files write_riemann
%{_libdir}/%{name}/write_riemann.so
%endif

%files collection3
%{_localstatedir}/www/collection3
%{_sysconfdir}/httpd/conf.d/collection3.conf

%files php-collection
%{_localstatedir}/www/php-collection
%{_sysconfdir}/httpd/conf.d/php-collection.conf

%files contrib
%doc contrib/

%changelog
* Wed May 27 2015 Marc Fournier <marc.fournier@camptocamp.com> 5.5.0-1
- New upstream version
- New plugins enabled by default: ceph, drbd, log_logstash, write_tsdb, smart,
  openldap, redis, write_redis, zookeeper, write_log, write_sensu, ipc,
  turbostat, fhcount
- New plugins disabled by default: barometer, write_kafka
- Enable zfs_arc, now supported on Linux
- Install disk plugin in a dedicated package, as it depends on libudev
- use systemd on EL7, sysvinit on EL6 & EL5
- Install collectdctl, collectd-tg and collectd-nagios in collectd-utils.rpm
- Add build-dependency on libcap-devel

* Mon Aug 19 2013 Marc Fournier <marc.fournier@camptocamp.com> 5.4.0-1
- New upstream version
- Build netlink plugin by default
- Enable cgroups, lvm and statsd plugins
- Enable (but don't build by default) mic, aquaero and sigrok plugins

* Tue Aug 06 2013 Marc Fournier <marc.fournier@camptocamp.com> 5.3.1-1
- New upstream version
- Added RHEL5 support:
  * conditionally disable plugins not building on this platform
  * add/specify some build dependencies and options
  * replace some RPM macros not available on this platform
- Removed duplicate --enable-aggregation
- Added some comments & usage examples
- Replaced a couple of "Buildrequires" by "BuildRequires"
- Enabled modbus plugin on RHEL6
- Enabled netlink plugin on RHEL6 and RHEL7
- Allow perl plugin to build on RHEL5
- Add support for RHEL7
- Misc perl-related improvements:
  * prevent rpmbuild from extracting dependencies from files in /usr/share/doc
  * don't package collection3 and php-collection twice
  * keep perl scripts from contrib/ in collectd-contrib

* Wed Apr 10 2013 Marc Fournier <marc.fournier@camptocamp.com> 5.3.0-1
- New upstream version
- Enabled write_riemann plugin
- Enabled tail_csv plugin
- Installed collectd-tc manpage

* Fri Jan 11 2013 Marc Fournier <marc.fournier@camptocamp.com> 5.2.0-3
- remove dependency on libstatgrab, which isn't required on linux

* Thu Jan 03 2013 Marc Fournier <marc.fournier@camptocamp.com> 5.2.0-2
- collection3 and php-collection viewers are now in separate packages

* Fri Dec 21 2012 Marc Fournier <marc.fournier@camptocamp.com> 5.2.0-1
- New upstream version
- Enabled aggregation plugin
- Installed collectd-tc
- Added network.h and network_buffer.h to libcollectdclient-devel
- Moved libxml2-devel and libcurl-devel BRs to relevant plugins sections
- Moved libcollectdclient.so from libcollectdclient-devel to libcollectdclient
- Added rrdcached and redis plugin descriptions
- Mentioned new pf plugin in disabled plugins list

* Sun Nov 18 2012 Ruben Kerkhof <ruben@tilaa.nl> 5.1.0-3
- Follow Fedora Packaging Guidelines in java subpackage

* Sat Nov 17 2012 Ruben Kerkhof <ruben@tilaa.nl> 5.1.0-2
- Move perl stuff to perl_vendorlib
- Replace hardcoded paths with macros
- Remove unnecessary Requires
- Removed .a and .la files
- Some other small cleanups

* Fri Nov 16 2012 Marc Fournier <marc.fournier@camptocamp.com> 5.1.0-1
- New upstream version
- Changes to support 5.1.0
- Enabled all buildable plugins based on libraries available on EL6 + EPEL
- All plugins requiring external libraries are now shipped in separate
  packages.
- No longer treat Java plugin as an exception, correctly set $JAVA_HOME during
  the build process + ensure build deps are installed.
- Dropped per-plugin configuration files, as they tend to diverge from upstream
  defaults.
- Moved perl stuff to /usr/share/perl5/
- Don't alter Interval and ReadThreads by default, let the user change this
  himself.
- Initscript improvements:
  * checks configuration before (re)starting, based on debian's initscript
  * use /etc/sysconfig instdead of /etc/default
  * include optional $ARGS in arguments passed to collectd.
- Drop collection.cgi from main package, as it's been obsoleted by collection3
- Moved contrib/ to its own package, to avoid cluttering the main package with
  non-essential stuff.
- Replaced BuildPrereq by BuildRequires

* Mon Jan 03 2011 Monetate <jason.stelzer@monetate.com> 5.0.1
- New upstream version
- Changes to support 5.0.1

* Mon Jan 04 2010 Rackspace <stu.hood@rackspace.com> 4.9.0
- New upstream version
- Changes to support 4.9.0
- Added support for Java/GenericJMX plugin

* Mon Mar 17 2008 RightScale <support@rightscale.com> 4.3.1
- New upstream version
- Changes to support 4.3.1
- Added More Prereqs to support more plugins
- Added support for perl plugin

* Mon Aug 06 2007 Kjell Randa <Kjell.Randa@broadpark.no> 4.0.6
- New upstream version

* Wed Jul 25 2007 Kjell Randa <Kjell.Randa@broadpark.no> 4.0.5
- New major releas
- Changes to support 4.0.5

* Thu Jan 11 2007 Iain Lea <iain@bricbrac.de> 3.11.0-0
- fixed spec file to build correctly on fedora core
- added improved init.d script to work with chkconfig
- added %%post and %%postun to call chkconfig automatically

* Sun Jul 09 2006 Florian octo Forster <octo@verplant.org> 3.10.0-1
- New upstream version

* Sun Jun 25 2006 Florian octo Forster <octo@verplant.org> 3.9.4-1
- New upstream version

* Thu Jun 01 2006 Florian octo Forster <octo@verplant.org> 3.9.3-1
- New upstream version

* Tue May 09 2006 Florian octo Forster <octo@verplant.org> 3.9.2-1
- New upstream version

* Tue May 09 2006 Florian octo Forster <octo@verplant.org> 3.8.5-1
- New upstream version

* Fri Apr 21 2006 Florian octo Forster <octo@verplant.org> 3.9.1-1
- New upstream version

* Fri Apr 14 2006 Florian octo Forster <octo@verplant.org> 3.9.0-1
- New upstream version
- Added the `apache' package.

* Tue Mar 14 2006 Florian octo Forster <octo@verplant.org> 3.8.2-1
- New upstream version

* Mon Mar 13 2006 Florian octo Forster <octo@verplant.org> 3.8.1-1
- New upstream version

* Thu Mar 09 2006 Florian octo Forster <octo@verplant.org> 3.8.0-1
- New upstream version

* Sat Feb 18 2006 Florian octo Forster <octo@verplant.org> 3.7.2-1
- Include `tape.so' so the build doesn't terminate because of missing files..
- New upstream version

* Sat Feb 04 2006 Florian octo Forster <octo@verplant.org> 3.7.1-1
- New upstream version

* Mon Jan 30 2006 Florian octo Forster <octo@verplant.org> 3.7.0-1
- New upstream version
- Removed the extra `hddtemp' package

* Tue Jan 24 2006 Florian octo Forster <octo@verplant.org> 3.6.2-1
- New upstream version

* Fri Jan 20 2006 Florian octo Forster <octo@verplant.org> 3.6.1-1
- New upstream version

* Fri Jan 20 2006 Florian octo Forster <octo@verplant.org> 3.6.0-1
- New upstream version
- Added config file, `collectd.conf(5)', `df.so'
- Added package `collectd-mysql', dependency on `mysqlclient10 | mysql'

* Wed Dec 07 2005 Florian octo Forster <octo@verplant.org> 3.5.0-1
- New upstream version

* Sat Nov 26 2005 Florian octo Forster <octo@verplant.org> 3.4.0-1
- New upstream version

* Sat Nov 05 2005 Florian octo Forster <octo@verplant.org> 3.3.0-1
- New upstream version

* Wed Oct 26 2005 Florian octo Forster <octo@verplant.org> 3.2.0-1
- New upstream version
- Added statement to remove the `*.la' files. This fixes a problem when
  `Unpackaged files terminate build' is in effect.
- Added `processes.so*' to the main package

* Fri Oct 14 2005 Florian octo Forster <octo@verplant.org> 3.1.0-1
- New upstream version
- Added package `collectd-hddtemp'

* Fri Sep 30 2005 Florian octo Forster <octo@verplant.org> 3.0.0-1
- New upstream version
- Split the package into `collectd' and `collectd-sensors'

* Fri Sep 16 2005 Florian octo Forster <octo@verplant.org> 2.1.0-1
- New upstream version

* Sat Sep 10 2005 Florian octo Forster <octo@verplant.org> 2.0.0-1
- New upstream version

* Mon Aug 29 2005 Florian octo Forster <octo@verplant.org> 1.8.0-1
- New upstream version

* Thu Aug 25 2005 Florian octo Forster <octo@verplant.org> 1.7.0-1
- New upstream version

* Sun Aug 21 2005 Florian octo Forster <octo@verplant.org> 1.6.0-1
- New upstream version

* Sun Jul 17 2005 Florian octo Forster <octo@verplant.org> 1.5.1-1
- New upstream version

* Sun Jul 17 2005 Florian octo Forster <octo@verplant.org> 1.5-1
- New upstream version

* Mon Jul 11 2005 Florian octo Forster <octo@verplant.org> 1.4.2-1
- New upstream version

* Sat Jul 09 2005 Florian octo Forster <octo@verplant.org> 1.4-1
- Built on RedHat 7.3
