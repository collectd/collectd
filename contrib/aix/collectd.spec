
%define name    collectd
%define version 4.10.1
%define release 1

Name:           %{name}
Summary:        Statistics collection daemon for filling RRD files.
Version:        %{version}
Release:        %{release}
#Source:         http://collectd.org/files/%{name}-%{version}.tar.gz
Source0:        %{name}-%{version}.tar.gz
Group:          System Environment/Daemons
BuildRoot:      %{_tmppath}/%{name}-%{version}-buildroot
License:        GPL
BuildPrereq:    rrdtool-devel,net-snmp-devel
Requires:       rrdtool,net-snmp
Packager:       Aurelien Reynaud <collectd@wattapower.net>
Vendor:         collectd development team <collectd@verplant.org>

%description
collectd is a small daemon which collects system information periodically and
provides mechanisms to monitor and store the values in a variety of ways. It
is written in C for performance. Since the daemon doesn't need to startup
every time it wants to update the values it's very fast and easy on the
system. Also, the statistics are very fine grained since the files are updated
every 10 seconds.

%prep
%setup

%build
# The RM variable in the RPM environment conflicts with that of the build environment,
# at least when building on AIX 6.1. This is definitely a bug in one of the tools but
# for now we work around it by unsetting the variable below.
[ -n "$RM" ] && unset RM
./configure LDFLAGS="-Wl,-brtl" --prefix=/opt/freeware --mandir=/opt/freeware/man --disable-dns --with-libnetsnmp=/opt/freeware/bin/net-snmp-config
make

%install
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{_localstatedir}/lib/%{name}
mkdir -p $RPM_BUILD_ROOT/%{_localstatedir}/run
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
cp contrib/aix/init.d-collectd $RPM_BUILD_ROOT/etc/rc.d/init.d/collectd

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf "$RPM_BUILD_ROOT"

%files
%defattr(-,root,system)
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
%config(noreplace) %attr(0644,root,system) %{_sysconfdir}/collectd.conf
%attr(0755,root,system) /etc/rc.d/init.d/collectd
%attr(0755,root,system) %{_sbindir}/collectd
%attr(0755,root,system) %{_bindir}/collectd-nagios
%attr(0755,root,system) %{_sbindir}/collectdmon
%attr(0644,root,system) %{_mandir}/man1/*
%attr(0644,root,system) %{_mandir}/man5/*

# client
%attr(0644,root,system) %{_includedir}/%{name}/client.h
%attr(0644,root,system) %{_includedir}/%{name}/lcc_features.h

%attr(0644,root,system) %{_libdir}/libcollectdclient.*
%attr(0644,root,system) %{_libdir}/pkgconfig/libcollectdclient.pc

%attr(0444,root,system) %{_libdir}/%{name}/*.so
%attr(0444,root,system) %{_libdir}/%{name}/*.a
%attr(0444,root,system) %{_libdir}/%{name}/*.la

%attr(0644,root,system) %{_datadir}/%{name}/types.db

%dir %{_localstatedir}/lib/%{name}
%dir %{_localstatedir}/run

