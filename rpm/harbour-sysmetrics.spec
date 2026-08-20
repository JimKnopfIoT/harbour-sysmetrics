# Neutral packaging metadata — no personal identifiers.
%define _buildhost reproducible-builder
Name:       harbour-sysmetrics
Summary:    System diagnostics for Sailfish OS
Version:    0.1.0
Release:    1
License:    GPL-3.0-or-later
URL:        https://github.com/JimKnopfIoT/harbour-sysmetrics
Source0:    %{name}-%{version}.tar.bz2
Vendor:     harbour-sysmetrics contributors
Packager:   harbour-sysmetrics contributors

Requires:   sailfishsilica-qt5
Requires:   nemo-qml-plugin-configuration-qt5
Requires:   qt5-qtdeclarative-import-sensors
Requires:   qt5-qtdeclarative-import-positioning
Requires:   iw
BuildRequires: pkgconfig(sailfishapp)
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: desktop-file-utils

%description
Process and system monitor: per-process CPU, memory, I/O, open files,
devices, network sockets; system graphs; recording mode for load analysis.
On-device only, collects nothing, transmits nothing.

%prep
%setup -q

%build
%qmake5 "DEFINES+=SYSMETRICS_VERSION=%{version}-%{release}"
%make_build

%install
%qmake5_install

%post
# Apply a changed [X-Sailjail] section without reboot.
systemctl restart sailjaild >/dev/null 2>&1 || :

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
/usr/lib/systemd/system/harbour-sysmetrics-helper.service

%changelog
* Thu Aug 20 2026 harbour-sysmetrics contributors 0.1.0-1
- Initial release.
