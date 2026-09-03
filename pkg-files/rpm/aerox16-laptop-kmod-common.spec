Name:       aerox16-laptop-kmod-common

Version:        0.2.0
Release:        1%{?dist}
Summary:        Common file for aerox16-laptop drivers
License:        GPL
URL:            https://github.com/tangalbert919/gigabyte-laptop-wmi

Source0:        aerox16-laptop.conf

Requires:       aerox16-laptop-kmod = %{?epoch:%{epoch}:}%{version}
Provides:       aerox16-laptop-kmod-common = %{?epoch:%{epoch}:}%{version}

%description
Common package for aerox16-laptop drivers. Mostly empty.

%install
install -p -m 0644 -D %{SOURCE0} %{buildroot}%{_sysconfdir}/modules-load.d/aerox16-laptop.conf

%files
%{_sysconfdir}/modules-load.d/aerox16-laptop.conf

%changelog
