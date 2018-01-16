%define _confdir %{_sysconfdir}/collectd.d
%define _collectdir usr/lib64/collectd/gluster_collectd
%define _unpackaged_files_terminate_build 0

# This is a spec file for gluster_collectd
# The following values are provided by passing the following arguments
# to rpmbuild.  For example:
#         --define "_version 1.0" --define "_release 1"
#

%{!?_version:%global _version __PKG_VERSION__}
%{!?_release:%global _release __PKG_RELEASE__}
 
Summary  : Red Hat Gluster Collectd Plugin
License  : GPLv2
Name     : gluster_collectd
Version  : %{_version}
Release  : %{_release}%{?dist}
Source0  : gluster_collectd-%{_version}-%{_release}.tar.gz
BuildArch: noarch
Group    : Development/Tools
URL      : http://github.com/collectd/collectd
BuildRequires: python
BuildRequires: python-setuptools
Requires : python
Requires : collectd
Requires : collectd-python
 
%description
The gluster plugin for collectd sends metrics to collectd. 
 
%prep
%setup -q -n gluster_collectd-%{_version}
 
%build
%{__python} setup.py build
 
%install
rm -rf %{buildroot}
mkdir -p      %{buildroot}/%{_confdir}/
mkdir -p      %{buildroot}/usr/share/collectd/
mkdir -p      %{buildroot}/%{_collectdir}
cp -r conf/* %{buildroot}/%{_confdir}/
cp -r types/* %{buildroot}/usr/share/collectd/
cp -r ./build/lib/src/*  %{buildroot}/%{_collectdir}/

# Man Pages
install -d -m 755 %{buildroot}%{_mandir}/man8
install -p -m 0644 README.md %{buildroot}%{_mandir}/man8

%files
%defattr(-,root,root)
%{_mandir}/man8/*
/usr/share/collectd/types.db.gluster
%exclude /%{_collectdir}/gluster_plugins/*.pyc
%exclude /%{_collectdir}/gluster_plugins/*.pyo
%exclude /%{_collectdir}/*.pyc
%exclude /%{_collectdir}/*.pyo
/%{_collectdir}/gluster_plugins/*.py
/%{_collectdir}/*.py

%dir %{_confdir}
%dir /%{_collectdir}
%dir /%{_collectdir}/gluster_plugins
%config(noreplace) %{_confdir}/python.conf

%changelog
* Wed Jan 31 2018 Venkata R Edara <redara@redhat.com> 1.0.0-0
- Initial version of gluster collectd plugin 1.0.0-0
