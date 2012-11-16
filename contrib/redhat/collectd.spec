
Summary:	Statistics collection daemon for filling RRD files.
Name:		collectd
Version:	5.1.0
Release:	1%{?dist}
Source:		http://collectd.org/files/%{name}-%{version}.tar.gz
License:	GPL
Group:		System Environment/Daemons
BuildRoot:	%{_tmppath}/%{name}-%{version}-root
BuildRequires:	rrdtool-devel, libstatgrab-devel, libxml2-devel, libiptcdata-devel, libgcrypt-devel, kernel-headers
# libcurl deps
BuildRequires:	curl-devel, libidn-devel, openssl-devel
Requires:	libstatgrab, libgcrypt
Packager:	RightScale <support@rightscale.com>
Vendor:		collectd development team <collectd@verplant.org>

%description
collectd is a small daemon which collects system information periodically and
provides mechanisms to monitor and store the values in a variety of ways. It
is written in C for performance. Since the daemon doesn't need to startup
every time it wants to update the values it's very fast and easy on the
system. Also, the statistics are very fine grained since the files are updated
every 10 seconds.

%package amqp
Summary:	amqp-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, librabbitmq
BuildRequires:	librabbitmq-devel
%description amqp
The AMQP plugin transmits or receives values collected by collectd via the
Advanced Message Queuing Protocol (AMQP).

%package apache
Summary:	apache-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl
%description apache
This plugin collects data provided by Apache's `mod_status'.

%package ascent
Summary:	ascent-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl, libxml2
%description ascent
The Ascent plugin reads and parses the statistics page of Ascent, a free and
open-source server software for the game World of Warcraft by Blizzard
Entertainment.

%package bind
Summary:	bind-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl, libxml2
%description bind
The BIND plugin retrieves this information that's encoded in XML and provided
via HTTP and submits the values to collectd.

%package curl
Summary:	curl-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl
%description curl
The cURL plugin uses libcurl to read files and then parses them according to
the configuration.

%package curl_json
Summary:	curl_json-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl, yajl
Buildrequires:	yajl-devel
%description curl_json
The cURL-JSON plugin queries JavaScript Object Notation (JSON) data using the
cURL library and parses it according to the user's configuration.

%package curl_xml
Summary:	curl_xml-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl, libxml2
%description curl_xml
The cURL-XML plugin reads files using libcurl and parses it as Extensible
Markup Language (XML).

%package dns
Summary:	dns-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libpcap
Buildrequires:	libpcap-devel
%description dns
The DNS plugin has a similar functionality to dnstop: It uses libpcap to get a
copy of all traffic from/to port UDP/53 (that's the DNS port), interprets the
packets and collects statistics of your DNS traffic.

%package dbi
Summary:	dbi-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libdbi
Buildrequires:	libdbi-devel
%description dbi
The DBI plugin uses libdbi, a database abstraction library, to execute SQL
statements on a database and read back the result.

%package email
Summary:	email-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, spamassassin
%description email
This plugin collects data provided by spamassassin.

%package gmond
Summary:	gmond-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, ganglia
BuildRequires:	ganglia-devel
%description gmond
The gmond plugin subscribes to a Multicast group to receive data from gmond,
the client daemon of the Ganglia project.

%package hddtemp
Summary:	hddtemp-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, hddtemp
%description hddtemp
The HDDTemp plugin collects the temperature of hard disks. The temperatures are
provided via SMART and queried by the external hddtemp daemon.

%package ipmi
Summary:	ipmi-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, OpenIPMI-libs
BuildRequires:	OpenIPMI-devel
%description ipmi
The IPMI plugin uses the OpenIPMI library to read hardware sensors from servers
using the Intelligent Platform Management Interface (IPMI).

%package iptables
Summary:	iptables-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, iptables-devel
BuildRequires:	iptables-devel
%description iptables
The IP-Tables plugin can gather statistics from your ip_tables based packet
filter (aka. firewall) for both the IPv4 and the IPv6 protocol. It can collect
the byte- and packet-counters of selected rules and submit them to collectd.

%package java
Summary:	java-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libjvm.so
BuildRequires:	java-1.7.0-openjdk-devel
%description java
This plugin for collectd allows plugins to be written in Java and executed
in an embedded JVM.

%package libvirt
Summary:	libvirt-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libvirt, libxml2
BuildRequires:	libvirt-devel
%description libvirt

%package memcachec
Summary:	memcachec-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libmemcached
BuildRequires:	libmemcached-devel
%description memcachec
The Memcachec plugin uses libmemcached to read statistics from a Memcached
instance. Note that another plugin, named `memcached', exists and does a
similar job, without requiring the installation of libmemcached.

%package mysql
Summary:	mysql-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, mysql
BuildRequires:	mysql-devel
%description mysql
MySQL querying plugin. This plugins provides data of issued commands, called
handlers and database traffic.

%package nginx
Summary:	nginx-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl
%description nginx
This plugin gets data provided by nginx.

%package notify_desktop
Summary:	notify_desktop-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libnotify
BuildRequires:	libnotify-devel
%description notify_desktop
The Notify Desktop plugin uses libnotify to display notifications to the user
via the desktop notification specification, i. e. on an X display.

%package notify_email
Summary:	notify_email-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libesmtp
BuildRequires:	libesmtp-devel
%description notify_email
The Notify Email plugin uses libESMTP to send notifications to a configured
email address(es).

%package nut
Summary:	nut-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, nut-client
BuildRequires:	nut-devel
%description nut

%package perl
Summary:	perl-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, perl
BuildRequires:	perl-ExtUtils-Embed
%description perl
The Perl plugin embeds a Perl interpreter into collectd and exposes the
application programming interface (API) to Perl-scripts.

%package pinba
Summary:	pinba-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, protobuf-c
BuildRequires:	protobuf-c-devel
%description pinba
The Pinba plugin receives and dispatches timing values from Pinba, a profiling
extension for PHP.

%package ping
Summary:	ping-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, liboping
BuildRequires:	liboping-devel
%description ping
The Ping plugin measures network latency using ICMP “echo requests”, usually
known as “ping”.

%package postgresql
Summary:	postgresql-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, postgresql-libs
BuildRequires:	postgresql-devel
%description postgresql
The PostgreSQL plugin connects to and executes SQL statements on a PostgreSQL
database.

%package python
Summary:	python-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, python
BuildRequires:	python-devel
%description python
The Python plugin embeds a Python interpreter into collectd and exposes the
application programming interface (API) to Python-scripts.

%package rrdtool
Summary:	librrdtool-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, rrdtool
%description rrdtool
The RRDtool plugin writes values to RRD-files using librrd.

%package sensors
Summary:	sensors-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, lm_sensors
BuildRequires:	lm_sensors-devel
%description sensors
This plugin for collectd provides querying of sensors supported by lm_sensors.

%package snmp
Summary:	snmp-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, net-snmp
BuildRequires:	net-snmp-devel
%description snmp
This plugin for collectd allows querying of network equipment using SNMP.

%package varnish
Summary:	varnish-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, varnish-libs
BuildRequires:	varnish-libs-devel
%description varnish
The Varnish plugin collects information about Varnish, an HTTP accelerator.

%package write_http
Summary:	write_http-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl
%description write_http
The Write HTTP plugin sends the values collected by collectd to a web-server
using HTTP POST requests.

%package contrib
Summary:	contrib files for collectd.
Group:		System Environment/Daemons
%description contrib
All the files found under contrib/ in the source tree are bundled in this
package.

%prep
rm -rf $RPM_BUILD_ROOT
%setup

%build
export JAVA_HOME="/usr"
./configure CFLAGS=-"DLT_LAZY_OR_NOW='RTLD_LAZY|RTLD_GLOBAL'" --prefix=%{_prefix} --sbindir=%{_sbindir} --mandir=%{_mandir} --libdir=%{_libdir} --sysconfdir=%{_sysconfdir} --localstatedir=%{_localstatedir}
make

%install
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
cp contrib/redhat/init.d-collectd $RPM_BUILD_ROOT/etc/rc.d/init.d/collectd
mkdir -p $RPM_BUILD_ROOT/var/lib/collectd
### Clean up docs
find contrib/ -type f -exec %{__chmod} a-x {} \;

###Modify Config for Redhat Based Distros
sed -i 's:#BaseDir     "/usr/var/lib/collectd":BaseDir     "/var/lib/collectd":' $RPM_BUILD_ROOT/etc/collectd.conf
sed -i 's:#PIDFile     "/usr/var/run/collectd.pid":PIDFile     "/var/run/collectd.pid":' $RPM_BUILD_ROOT/etc/collectd.conf
sed -i 's:#PluginDir   "/usr/lib/collectd":PluginDir   "%{_libdir}/collectd":' $RPM_BUILD_ROOT/etc/collectd.conf
sed -i 's:#TypesDB     "/usr/share/collectd/types.db":TypesDB     "/usr/share/collectd/types.db":' $RPM_BUILD_ROOT/etc/collectd.conf

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/chkconfig --add collectd
/sbin/chkconfig collectd on

%preun
if [ "$1" = 0 ]; then
   /sbin/chkconfig collectd off
   /etc/init.d/collectd stop
   /sbin/chkconfig --del collectd
fi
exit 0

%postun
if [ "$1" -ge 1 ]; then
    /etc/init.d/collectd restart
fi
exit 0

%files
%defattr(-,root,root)
%exclude %{_mandir}/man5/collectd-java*
%exclude %{_mandir}/man5/collectd-perl*
%exclude %{_mandir}/man5/collectd-python*
%exclude %{_mandir}/man5/collectd-snmp*
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
%config %attr(0644,root,root) /etc/collectd.conf
%attr(0755,root,root) /etc/rc.d/init.d/collectd
%attr(0755,root,root) %{_sbindir}/collectd
%attr(0755,root,root) %{_bindir}/collectd-nagios
%attr(0755,root,root) %{_bindir}/collectdctl
%attr(0755,root,root) %{_sbindir}/collectdmon
%attr(0644,root,root) %{_mandir}/man1/*
%attr(0644,root,root) %{_mandir}/man5/*

# client
%attr(0644,root,root) /usr/include/collectd/client.h
%attr(0644,root,root) /usr/include/collectd/lcc_features.h

%attr(0644,root,root) %{_libdir}/libcollectdclient.*
%attr(0644,root,root) %{_libdir}/pkgconfig/libcollectdclient.pc

# macro to grab binaries for a plugin, given a name
%define plugin_macro() \
%attr(0644,root,root) %{_libdir}/%{name}/%1.a \
%attr(0644,root,root) %{_libdir}/%{name}/%1.so* \
%attr(0644,root,root) %{_libdir}/%{name}/%1.la

# all plugins bundled with the main collectd package
%plugin_macro apcups
%plugin_macro battery
%plugin_macro conntrack
%plugin_macro contextswitch
%plugin_macro cpufreq
%plugin_macro cpu
%plugin_macro csv
%plugin_macro df
%plugin_macro disk
%plugin_macro ethstat
%plugin_macro entropy
%plugin_macro email
%plugin_macro exec
%plugin_macro filecount
%plugin_macro fscache
%plugin_macro interface
%plugin_macro iptables
%plugin_macro ipvs
%plugin_macro irq
%plugin_macro load
%plugin_macro logfile
%plugin_macro madwifi
%plugin_macro match_empty_counter
%plugin_macro match_hashed
%plugin_macro match_regex
%plugin_macro match_timediff
%plugin_macro match_value
%plugin_macro mbmon
%plugin_macro md
%plugin_macro memcached
%plugin_macro memory
%plugin_macro multimeter
%plugin_macro network
%plugin_macro nfs
%plugin_macro ntpd
%plugin_macro numa
%plugin_macro openvpn
%plugin_macro olsrd
%plugin_macro powerdns
%plugin_macro processes
%plugin_macro protocols
%plugin_macro serial
%plugin_macro swap
%plugin_macro syslog
%plugin_macro table
%plugin_macro tail
%plugin_macro target_notification
%plugin_macro target_replace
%plugin_macro target_scale
%plugin_macro target_set
%plugin_macro target_v5upgrade
%plugin_macro tcpconns
%plugin_macro teamspeak2
%plugin_macro ted
%plugin_macro thermal
%plugin_macro threshold
%plugin_macro unixsock
%plugin_macro uptime
%plugin_macro users
%plugin_macro uuid
%plugin_macro vmem
%plugin_macro vserver
%plugin_macro wireless
%plugin_macro write_graphite

# All plugins not built because of dependencies on libraries not available in
# RHEL or EPEL:
# plugin modbus disabled, requires libmodbus
# plugin netlink disabled, requires libnetlink.h
# plugin numa disabled, requires libnetapp
# plugin onewire disabled, requires libowfs
# plugin oracle disabled, requires Oracle
# plugin redis disabled, requires credis
# plugin routeros disabled, requires librouteros
# plugin rrdcached disabled, requires rrdtool >= 1.4
# plugin tokyotyrant disabled, requires tcrdb.h
# plugin write_mongodb disabled, requires libmongoc
# plugin write_redis disabled, requires credis
# plugin xmms disabled, requires xmms

%attr(0644,root,root) %{_datadir}/%{name}/types.db

%dir /var/lib/collectd

%files amqp
%plugin_macro amqp

%files apache
%plugin_macro apache

%files ascent
%plugin_macro ascent

%files bind
%plugin_macro bind

%files curl
%plugin_macro curl

%files curl_json
%plugin_macro curl_json

%files curl_xml
%plugin_macro curl_xml

%files dns
%plugin_macro dns

%files dbi
%plugin_macro dbi

%files email
%plugin_macro email

%files gmond
%plugin_macro gmond

%files hddtemp
%plugin_macro hddtemp

%files ipmi
%plugin_macro ipmi

%files iptables
%plugin_macro iptables

%files java
%attr(0644,root,root) /usr/share/man/man5/collectd-java.5.gz
%attr(0644,root,root) /usr/share/collectd/java/collectd-api.jar
%attr(0644,root,root) /usr/share/collectd/java/generic-jmx.jar
%plugin_macro java

%files libvirt
%plugin_macro libvirt

%files memcachec
%plugin_macro memcachec

%files mysql
%plugin_macro mysql

%files nginx
%plugin_macro nginx

%files notify_desktop
%plugin_macro notify_desktop

%files notify_email
%plugin_macro notify_email

%files nut
%plugin_macro nut

%files perl
%exclude %{_libdir}/perl5/perllocal.pod
%attr(0644,root,root) %{_libdir}/perl5/auto/Collectd/.packlist
%attr(0644,root,root) /usr/share/perl5/Collectd.pm
%attr(0644,root,root) /usr/share/perl5/Collectd/Unixsock.pm
%attr(0644,root,root) /usr/share/perl5/Collectd/Plugins/OpenVZ.pm
%attr(0644,root,root) /usr/share/perl5/Collectd/Plugins/Monitorus.pm
%attr(0644,root,root) /usr/share/man/man3/Collectd::Unixsock.3pm.gz
%attr(0644,root,root) /usr/share/man/man5/collectd-perl.5.gz
%plugin_macro perl

%files pinba
%plugin_macro pinba

%files ping
%plugin_macro ping

%files postgresql
/usr/share/collectd/postgresql_default.conf
%plugin_macro postgresql

%files python
%attr(0644,root,root) /usr/share/man/man5/collectd-python.5.gz
%plugin_macro python

%files rrdtool
%plugin_macro rrdtool

%files sensors
%plugin_macro sensors

%files snmp
%attr(0644,root,root) /usr/share/man/man5/collectd-snmp.5.gz
%plugin_macro snmp

%files varnish
%plugin_macro varnish

%files write_http
%plugin_macro write_http

%files contrib
%doc contrib/

%changelog
* Fri Nov 16 2012 Marc Fournier <marc.fournier@camptocamp.com> 5.1.0
- New upstream version
- Changes to support 5.1.0
- Enabled all buildable plugins based on libraries available on EL6 + EPEL
- All plugins requiring external libraries are now shipped in seperate
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

%changelog
* Tue Jan 03 2011 Monetate <jason.stelzer@monetate.com> 5.0.1
- New upstream version
- Changes to support 5.0.1

* Tue Jan 04 2010 Rackspace <stu.hood@rackspace.com> 4.9.0
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

* Wed Jan 11 2007 Iain Lea <iain@bricbrac.de> 3.11.0-0
- fixed spec file to build correctly on fedora core
- added improved init.d script to work with chkconfig
- added %post and %postun to call chkconfig automatically

* Sun Jul 09 2006 Florian octo Forster <octo@verplant.org> 3.10.0-1
- New upstream version

* Tue Jun 25 2006 Florian octo Forster <octo@verplant.org> 3.9.4-1
- New upstream version

* Tue Jun 01 2006 Florian octo Forster <octo@verplant.org> 3.9.3-1
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

* Thu Mar 14 2006 Florian octo Forster <octo@verplant.org> 3.8.2-1
- New upstream version

* Thu Mar 13 2006 Florian octo Forster <octo@verplant.org> 3.8.1-1
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

* Tue Oct 26 2005 Florian octo Forster <octo@verplant.org> 3.2.0-1
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

* Mon Sep 10 2005 Florian octo Forster <octo@verplant.org> 2.0.0-1
- New upstream version

* Mon Aug 29 2005 Florian octo Forster <octo@verplant.org> 1.8.0-1
- New upstream version

* Sun Aug 25 2005 Florian octo Forster <octo@verplant.org> 1.7.0-1
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
