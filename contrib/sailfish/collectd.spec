#
# This is a specfile for building RPM packages of current collectd
# release for Sailfish OS. Its based on specfile for RHEL/CentOS
# versions 5, 6 and 7.  In this version, the plugins are picked and
# all inserted into a single RPM with collectd daemon.
#


Summary:	Statistics collection and monitoring daemon
Name:		collectd
Version:	5.5.0.git.2018.01.20
Release:	1%{?dist}
URL:		http://collectd.org
Source:		http://collectd.org/files/%{name}-%{version}.tar.bz2
License:	GPLv2
Group:		System Environment/Daemons
BuildRoot:	%{_tmppath}/%{name}-%{version}-root
BuildRequires:	libgcrypt-devel, kernel-headers, libtool-ltdl-devel, libcap-devel, libxml2-devel, python-devel, perl-devel, perl-ExtUtils-MakeMaker
BuildRequires:	rrdtool-devel, autoconf, automake, libtool, bison, flex
BuildRequires:  libkeepalive-glib, libkeepalive-glib-devel, dbus-glib-devel

Requires:	systemd, libxml2, rrdtool, libkeepalive-glib, dbus-glib, rsync

%description
collectd is a small daemon which collects system information periodically and
provides mechanisms to monitor and store the values in a variety of ways. It
is written in C for performance. Since the daemon doesn't need to start up
every time it wants to update the values it's very fast and easy on the
system.


%package perl
Summary:	Perl plugin for collectd
Group:		System/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	perl(:MODULE_COMPAT_%(eval "`%{__perl} -V:version`"; echo $version))
BuildRequires:	perl-ExtUtils-Embed
%description perl
The Perl plugin embeds a Perl interpreter into collectd and exposes the
application programming interface (API) to Perl-scripts.

# %package python
# Summary:	Python plugin for collectd
# Group:		System/Daemons
# Requires:	%{name}%{?_isa} = %{version}-%{release}
# BuildRequires: python-devel
# %description python
# The Python plugin embeds a Python interpreter into collectd and exposes the
# application programming interface (API) to Python-scripts.

%package -n libcollectdclient
Summary:	Collectd client library
Group:		System/Daemons
%description -n libcollectdclient
Collectd client library

%package -n libcollectdclient-devel
Summary:	Development files for libcollectdclient
Group:		System/Daemons
Requires:	pkgconfig
Requires:	libcollectdclient%{?_isa} = %{version}-%{release}
%description -n libcollectdclient-devel
Development files for libcollectdclient

%package -n collectd-utils
Summary:	Collectd utilities
Group:		System/Daemons
Requires:	libcollectdclient%{?_isa} = %{version}-%{release}
Requires:	collectd%{?_isa} = %{version}-%{release}
%description -n collectd-utils
Collectd utilities

%prep
%setup -q -n %{name}-%{version}

%build

./build.sh

# %configure CFLAGS="%{optflags} -DLT_LAZY_OR_NOW=\"RTLD_LAZY|RTLD_GLOBAL\"" \
# 	--disable-static \
# 	--without-included-ltdl \
# 	--enable-all-plugins=yes \
# 	--enable-match_empty_counter \
# 	--enable-match_hashed \
# 	--enable-match_regex \
# 	--enable-match_timediff \
# 	--enable-match_value \
# 	--enable-target_notification \
# 	--enable-target_replace \
# 	--enable-target_scale \
# 	--enable-target_set \
# 	--enable-target_v5upgrade \
# 	%{?_with_aggregation} \
# 	%{?_with_amqp} \
# 	%{?_with_apache} \
# 	%{?_with_apcups} \
# 	%{?_with_apple_sensors} \
# 	%{?_with_aquaero} \
# 	%{?_with_ascent} \
# 	%{?_with_barometer} \
# 	%{?_with_battery} \
# 	%{?_with_bind} \
# 	%{?_with_ceph} \
# 	%{?_with_cgroups} \
# 	%{?_with_conntrack} \
# 	%{?_with_contextswitch} \
# 	%{?_with_cpu} \
# 	%{?_with_cpufreq} \
# 	%{?_with_csv} \
# 	%{?_with_curl} \
# 	%{?_with_curl_json} \
# 	%{?_with_curl_xml} \
# 	%{?_with_dbi} \
# 	%{?_with_df} \
# 	%{?_with_disk} \
# 	%{?_with_dns} \
# 	%{?_with_drbd} \
# 	%{?_with_email} \
# 	%{?_with_entropy} \
# 	%{?_with_ethstat} \
# 	%{?_with_exec} \
# 	%{?_with_fhcount} \
# 	%{?_with_filecount} \
# 	%{?_with_fscache} \
# 	%{?_with_gmond} \
# 	%{?_with_hddtemp} \
# 	%{?_with_interface} \
# 	%{?_with_ipc} \
# 	%{?_with_ipmi} \
# 	%{?_with_iptables} \
# 	%{?_with_ipvs} \
# 	%{?_with_java} \
# 	%{?_with_virt} \
# 	%{?_with_log_logstash} \
# 	%{?_with_lpar} \
# 	%{?_with_lvm} \
# 	%{?_with_memcachec} \
# 	%{?_with_mic} \
# 	%{?_with_modbus} \
# 	%{?_with_multimeter} \
# 	%{?_with_mysql} \
# 	%{?_with_netapp} \
# 	%{?_with_netlink} \
# 	%{?_with_nginx} \
# 	%{?_with_notify_desktop} \
# 	%{?_with_notify_email} \
# 	%{?_with_nut} \
# 	%{?_with_onewire} \
# 	%{?_with_openldap} \
# 	%{?_with_oracle} \
# 	%{?_with_perl} \
# 	%{?_with_pf} \
# 	%{?_with_pinba} \
# 	%{?_with_ping} \
# 	%{?_with_postgresql} \
# 	%{?_with_python} \
# 	%{?_with_redis} \
# 	%{?_with_routeros} \
# 	%{?_with_rrdcached} \
# 	%{?_with_rrdtool} \
# 	%{?_with_sensors} \
# 	%{?_with_sigrok} \
# 	%{?_with_smart} \
# 	%{?_with_snmp} \
# 	%{?_with_tape} \
# 	%{?_with_tokyotyrant} \
# 	%{?_with_varnish} \
# 	%{?_with_write_http} \
# 	%{?_with_write_kafka} \
# 	%{?_with_write_mongodb} \
# 	%{?_with_write_redis} \
# 	%{?_with_xmms} \
# 	%{?_with_zfs_arc} \
# 	%{?_with_zookeeper} \
# 	%{?_with_irq} \
# 	%{?_with_load} \
# 	%{?_with_logfile} \
# 	%{?_with_madwifi} \
# 	%{?_with_mbmon} \
# 	%{?_with_md} \
# 	%{?_with_memcached} \
# 	%{?_with_memory} \
# 	%{?_with_network} \
# 	%{?_with_nfs} \
# 	%{?_with_ntpd} \
# 	%{?_with_numa} \
# 	%{?_with_olsrd} \
# 	%{?_with_openvpn} \
# 	%{?_with_powerdns} \
# 	%{?_with_processes} \
# 	%{?_with_protocols} \
# 	%{?_with_serial} \
# 	%{?_with_statsd} \
# 	%{?_with_swap} \
# 	%{?_with_syslog} \
# 	%{?_with_table} \
# 	%{?_with_tail} \
# 	%{?_with_tail_csv} \
# 	%{?_with_tcpconns} \
# 	%{?_with_teamspeak2} \
# 	%{?_with_ted} \
# 	%{?_with_thermal} \
# 	%{?_with_threshold} \
# 	%{?_with_turbostat} \
# 	%{?_with_unixsock} \
# 	%{?_with_uptime} \
# 	%{?_with_users} \
# 	%{?_with_uuid} \
# 	%{?_with_vmem} \
# 	%{?_with_vserver} \
# 	%{?_with_wireless}\
# 	%{?_with_write_graphite} \
# 	%{?_with_write_http} \
# 	%{?_with_write_log} \
# 	%{?_with_write_riemann} \
# 	%{?_with_write_sensu} \
# 	%{?_with_write_tsdb}

%configure --enable-keepalive \
	   --disable-all-plugins \
	   --with-perl-bindings="INSTALLDIRS=vendor" \
	   --enable-aggregation \
	   --enable-battery \
	   --enable-contextswitch \
	   --enable-cpu \
	   --enable-cpufreq \
	   --enable-cpuidle \
	   --enable-cpusleep \
	   --enable-cpufreq \
	   --enable-csv \
	   --enable-df \
	   --enable-disk \
	   --enable-entropy \
	   --enable-exec \
	   --enable-fhcount \
	   --enable-filecount \
	   --enable-interface \
	   --enable-ipc \
	   --enable-irq \
	   --enable-load \
	   --enable-logfile \
	   --enable-match_empty_counter \
	   --enable-match_hashed \
	   --enable-match_regex \
	   --enable-match_timediff \
	   --enable-match_value \
	   --enable-memory \
	   --enable-network \
	   --enable-processes \
	   --enable-protocols \
	   --enable-radio \
	   --enable-rrdtool \
	   --enable-statefs_bluetooth \
	   --enable-statefs_cellular \
	   --enable-statefs_internet \
	   --enable-suspend \
	   --enable-swap \
	   --enable-table \
	   --enable-tail \
	   --enable-tail_csv \
	   --enable-target_notification \
	   --enable-target_replace \
	   --enable-target_scale \
	   --enable-target_set \
	   --enable-tcpconns \
	   --enable-unixsock \
	   --enable-uptime \
	   --enable-uuid \
	   --enable-vmem \
	   --enable-wireless \
	   --enable-write_graphite \
	   --enable-write_log \
	   --enable-write_sensu \
	   --enable-write_tsdb

%{__make} %{?_smp_mflags}


%install
rm -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot}

%{__install} -Dp -m0644 contrib/sailfish/collectd.service %{buildroot}%{_userunitdir}/collectd.service
#%{__install} -Dp -m0644 contrib/sailfish/collectd2tmpfs.service %{buildroot}%{_userunitdir}/collectd2tmpfs.service
#%{__install} -Dp -m0644 contrib/sailfish/collectd2tmpfs.timer %{buildroot}%{_userunitdir}/collectd2tmpfs.timer
%{__install} -Dp -m0644 contrib/sailfish/collectd.conf %{buildroot}%{_sysconfdir}/collectd.conf
%{__install} -Dp -m0755 contrib/sailfish/collectd2tmpfs.sh %{buildroot}%{_bindir}/collectd2tmpfs

#%{__install} -d %{buildroot}%{_sharedstatedir}/collectd/
#%{__install} -d %{buildroot}%{_sysconfdir}/collectd.d/


### Clean up docs
find contrib/ -type f -exec %{__chmod} a-x {} \;
# *.la files shouldn't be distributed.
rm -f %{buildroot}/%{_libdir}/{collectd/,}*.la
rm -f %{buildroot}/%{_libdir}/{collectd/,}*.a

# SFOS: remove man pages
rm -f %{buildroot}/%{_mandir}/man1/collectd*.1*
rm -f %{buildroot}/%{_mandir}/man5/collectd*.5*
rm -f %{buildroot}/%{_mandir}/man5/types.db.5*


# Remove Perl hidden .packlist files.
find %{buildroot} -type f -name .packlist -delete
# Remove Perl temporary file perllocal.pod
find %{buildroot} -type f -name perllocal.pod -delete

rm -f %{buildroot}%{_mandir}/man5/collectd-perl.5*
rm -f %{buildroot}%{_mandir}/man3/Collectd::Unixsock.3pm*
rm -fr %{buildroot}/usr/lib/perl5/

rm -f %{buildroot}%{_datadir}/collectd/postgresql_default.conf

rm -f %{buildroot}%{_mandir}/man5/collectd-python.5*


%clean
rm -rf %{buildroot}

%pre
su nemo -c "systemctl --user stop %{name}.service"
#su nemo -c "systemctl --user stop collectd2tmpfs.timer"
exit 0

%preun
su nemo -c "systemctl --user disable %{name}.service"
su nemo -c "systemctl --user stop %{name}.service"
#su nemo -c "systemctl --user disable collectd2tmpfs.timer"
#su nemo -c "systemctl --user stop collectd2tmpfs.timer"

%post
su nemo -c "systemctl --user daemon-reload"
# su nemo -c "systemctl --user enable %{name}.service"
# su nemo -c "systemctl --user start %{name}.service"
# su nemo -c "systemctl --user enable collectd2tmpfs.timer"
# su nemo -c "systemctl --user start collectd2tmpfs.timer"

%postun
su nemo -c "systemctl --user daemon-reload"


%post -n libcollectdclient -p /sbin/ldconfig
%postun -n libcollectdclient -p /sbin/ldconfig


%files
%doc AUTHORS COPYING ChangeLog README
%config(noreplace) %{_sysconfdir}/collectd.conf
%{_userunitdir}/collectd.service
#%{_userunitdir}/collectd2tmpfs.service
#%{_userunitdir}/collectd2tmpfs.timer
%{_sbindir}/collectd
%{_sbindir}/collectdmon
%{_datadir}/collectd/types.db
# all plugins bundled with the main collectd package
%{_libdir}/%{name}/*.so
%{_bindir}/collectd2tmpfs

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

%files perl
%{perl_vendorlib}/Collectd.pm
%{perl_vendorlib}/Collectd/

# %files python
# %{_libdir}/%{name}/python.so

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
