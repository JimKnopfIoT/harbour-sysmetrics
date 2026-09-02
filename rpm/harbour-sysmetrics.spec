# Neutral packaging metadata — no personal identifiers.
%define _buildhost reproducible-builder

# Self-built Ultimate variant (adds the online CVE search):
#   mb2 build -- --with ultimate
# Store/default builds stay without it.
%bcond_with ultimate
Name:       harbour-sysmetrics
Summary:    System diagnostics for Sailfish OS
Version:    0.3.1
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
%qmake5 "DEFINES+=SYSMETRICS_VERSION=%{version}-%{release}" %{?with_ultimate:CONFIG+=ultimate}
%make_build

%install
%qmake5_install
# Ship a stripped binary: smaller package, and no symbol table to read.
strip %{buildroot}%{_bindir}/%{name}

%post
# Apply a changed [X-Sailjail] section without reboot.
systemctl restart sailjaild >/dev/null 2>&1 || :
systemctl daemon-reload >/dev/null 2>&1 || :
# Upgrades: disable a manually enabled pre-0.1.1 helper.
if [ "$1" -gt 1 ]; then
    systemctl disable --now harbour-sysmetrics-helper.service >/dev/null 2>&1 || :
fi

%preun
if [ "$1" = 0 ]; then
    systemctl disable --now harbour-sysmetrics-helper.service >/dev/null 2>&1 || :
fi

%postun
systemctl daemon-reload >/dev/null 2>&1 || :

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
/usr/lib/systemd/system/harbour-sysmetrics-helper.service
%{_datadir}/polkit-1/rules.d/50-harbour-sysmetrics.rules

%changelog
* Wed Sep 02 2026 harbour-sysmetrics contributors 0.3.1-1
- The battery page ranked processes by CPU percent, which is a figure for the
  processor page. It now shows where the current goes: what the gauge measures,
  what of that the CPU accounts for, and the remainder that no process caused.
- Per-process milliamps come from the kernel's energy model, which states the
  power a core draws at each frequency, divided by the cell voltage. Weighting
  by cluster is not optional -- on the Jolla Phone (2026) a big core costs 8.2
  times a little one by the model and 7.2 by measurement. The figures are a
  floor: the model counts core power only, leaving out leakage, L3, memory
  controller and regulator loss, together about half the true cost.
- Where the gauge yields a scale of its own precisely enough, that is used
  instead. It is rejected above 35 % spread, because on the same device with
  the display off current_now returned 67 mA and 644 mA seconds apart under
  identical full load.
- New section: what keeps the device awake, from the kernel's own wakelock
  tally. It answers for the processes CPU time cannot see. The path joins the
  root helper's read allowlist -- the file is world-readable but debugfs is not.
- Collecting runs only off the charger, decided by the current's sign rather
  than the status string, because older MediaTek gauges report "Not charging"
  while discharging.
- tools/power-probe.sh: measures whether a device's gauge reacts to load, how
  fast and by how much.

* Sun Aug 30 2026 harbour-sysmetrics contributors 0.3.0-1
- Detail pages reorganised: the figures needed for an overview stay open at the
  top; catalogues, firmware, module list, device tree and raw nodes are folded
  away at the end and open on tap. From four sections of the same kind on --
  power-supply nodes, network interfaces, USB devices, raw nodes -- they share
  a single header.
- Diagnosis is placed by the page instead of appended. On System & CPU it sits
  directly under the kernel hardening switches, which is what it reads against.
- Nothing behind a closed header is built, and a section whose rows cost real
  time builds them on first open. The HAL listing is one of those: it runs an
  external tool that never returns on some adaptations, and it cost three
  seconds on every visit to System & CPU.
- Android HAL services: all three binder domains are asked, each naming the
  service manager that answers for it. An Android 13+ base runs no
  hwservicemanager and registers its HALs as AIDL on /dev/binder; HIDL on
  /dev/hwbinder is the older way, and asking only that one answered nothing on
  a current device.
- Raw nodes carry a unit wherever the kernel's own interface fixes one -- module
  section sizes, thermal zone temperature, interface counters and MTU, link
  speed -- with the figure the kernel wrote kept beside it. Everything else
  stays the driver's own number.
- Byte sizes are labelled KiB, MiB and GiB, which is what they have always been.
- Thermal: the limiter units were built with QLatin1String from a UTF-8 literal
  and printed as "Â°C"; the unit now sits on the last number, so a list ending
  in a sentinel no longer reads "no limit K", and "no limit" / "no sensor" go
  through the translations.
- Thermal zones that are not temperatures are dropped: Qualcomm's BCL watchdogs
  (*-vbat-lvl, *-ibat-lvl, *-vph-lvl, *-bcl-lvl, soc) report mA, mV and percent
  through the thermal framework, and the battery card shows all three in their
  own units. camera-therm-usr on the Xperia 10 III is corrected for a 400k
  pull-up read through a 100k lookup table (1/T_true = 1/T_read + ln(4)/4250),
  verified radiometrically; recomputed figures carry an asterisk.
- Value columns wrap on word boundaries: a figure is never split across lines.
- The overview cards no longer swallow taps meant for their "show all" toggle.
- Network: the radio's firmware blobs are a folded list with the directory of
  each, instead of one long line.

* Sun Aug 23 2026 harbour-sysmetrics contributors 0.2.4-1
- "Since boot" keeps only what belongs to no single part: times, sleep mode,
  wake sources. CPU time moved to the processor page, memory pressure to RAM,
  bytes moved to storage; network volume was already on the network page.
- Figures that are our reading say so: battery quality names its basis, the
  storage assessment is labelled as ours, the colour scale and every threshold
  are written out in the glossary. Wear step 0x0B is named "exceeded" instead
  of being turned into 100 %.
- Battery: state of health is full ÷ design, with the soh register beside it
  rather than instead of it, and a note when the two disagree or when the gauge
  has never learned a capacity. Adds cycle bands, ageing profile, ESR, stored
  and ID resistance, each with its documented unit.
- Network: IP address per interface from getifaddrs, rates follow the default
  route instead of summing every interface, download and upload sit under their
  own graphs.
- Storage detail page gains bytes moved; glossary gains Backup.

* Sun Aug 23 2026 harbour-sysmetrics contributors 0.2.3-1
- New "Since boot" section: screen-on time from the MCE wakelock, awake time
  against deep sleep, suspend attempts with the device that blocked them,
  wake sources, data moved, CPU time budget, memory pressure and the counters
  that predate this boot. Card in the main page footer; glossary group and a
  Backup entry.
- Charging: the PD contract row says what was negotiated instead of naming
  the specification's "contract"; glossary keeps the original term.

* Sat Aug 22 2026 harbour-sysmetrics contributors 0.2.2-1
- USB-PD source capabilities from the raw PDOs; root helper narrowed to a
  literal read list, validated signal/renice, uid peer check; root mode is
  per session and asks before it is switched on.

* Thu Aug 20 2026 harbour-sysmetrics contributors 0.1.1-1
- The root helper is a settings switch (off by default) instead of a manual
  devel-su start: StartUnit/StopUnit over the system bus, polkit rule scoped
  to the helper unit. The helper exits by itself when no client is connected.

* Thu Aug 20 2026 harbour-sysmetrics contributors 0.1.0-1
- Initial release.
