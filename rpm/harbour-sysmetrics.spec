# Neutral packaging metadata — no personal identifiers.
%define _buildhost reproducible-builder

# Self-built Ultimate variant (adds the online CVE search):
#   mb2 build -- --with ultimate
# Store/default builds stay without it.
%bcond_with ultimate
Name:       harbour-sysmetrics
Summary:    System diagnostics for Sailfish OS
Version:    0.3.6
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
* Fri Sep 04 2026 harbour-sysmetrics contributors 0.3.4-1
- The three cameras of a MediaTek phone are recognised. The sensor names were
  only ever read from the vendor modules Qualcomm ships, so on the other stack
  the list stayed empty and the section still called itself CAMSS. The driver
  names them in procfs instead -- with the frame it grabs, which is not the
  sensor's pixel count and says so. The calibration EEPROMs and the processing
  engines are counted from their device nodes, where a V4L2-only count reported
  zero of each.
- Qt and QML warnings are written to a log file (~/.cache, truncated at each
  start, capped). On this platform a broken binding leaves an empty row and
  says nothing anywhere -- the file is the only place it becomes visible. A
  healthy run writes one line: that the log was opened.
- Internally, every figure now goes through one list of candidate sources --
  path, unit and the window the result has to fall into -- and the first that
  answers is remembered. "Nothing answered" is a state of its own now, so a
  missing register is no longer a zero: a gauge that counts no cycles no longer
  produces a verdict built on zero of them.
- The battery capacities had two readers, the sampler and the detail page, each
  finding them on its own. Two paths to one number is how a page ends up
  contradicting itself; there is one reader now.
- The charge target voltage is found on Qualcomm too (voltage_max), and the
  input voltage on the older MediaTek phone, which keeps it on the battery node.
* Fri Sep 04 2026 harbour-sysmetrics contributors 0.3.3-1
- Every figure on the battery, charger, network, storage and wake pages was
  read back against three phones -- two MediaTek, one Qualcomm -- and compared
  with the node it comes from. What follows is what disagreed.
- A state of health is no longer reported where full and design capacity are
  the same number. One phone ships both from the same device-tree entry, so
  their ratio is 100 % by construction; the page showed a green "as new" built
  on a division of a number by itself. It now says so, and prints the gauge's
  own full-charge figure beside it where that disagrees.
- The charger's input limit came from current_max, which on one charger is the
  battery-side ceiling rather than the input's. input_current_limit decides
  where it exists. Input voltages are refused unless they can be a voltage:
  vendor nodes report millivolt where the class says microvolt, and a switched
  off wireless input answered 1070 volt.
- WLAN band and channel were wrong wherever iw prints frequencies with a
  decimal point (since iw 6.x): the integer parse answered zero, and zero reads
  as a valid 2.4 GHz channel. A 5 GHz link was labelled 2.4 GHz.
- The WLAN chip vendor was read from the driver name, and a driver plainly
  called "wlan" was attributed to Qualcomm on a MediaTek phone. The device
  tree's compatible string decides now.
- The regulatory-domain check read the global block of iw reg get instead of
  the radio's own, and reported a country where the phy stood at 99/DFS-UNSET.
- "What keeps the device awake" ranked sources by how long they were held,
  under a heading that promised the time suspend was actually prevented. The
  kernel counts both; the list now reads the second, takes it from
  /sys/class/wakeup (world-readable, unlike debugfs) and drops the kernel's
  aggregate row for deleted sources.
- Storage counted device-mapper layers as separate devices, so the same writes
  appeared up to three times. A filesystem mounted at several places is listed
  once. Read-only system images are no longer painted red for being full.
- Type-C roles printed the kernel's selection list verbatim, so "source [sink]"
  read as source while the phone was the sink. The bracketed entry is the
  active one, and it is the one shown.
- USB-PD is now reported on chipsets without a pd_active node, from the port's
  own contract state, and PPS is named where the driver distinguishes it.
- Corrected in the glossary and the notes: the current's sign (it is negative
  while discharging, not while charging), the CPU busy sum (nice was missing),
  deep sleep (not every phone suspends to RAM), the charger ADC current (it is
  the battery's, sign included), and an entry describing a per-process power
  figure that 0.3.2 removed. New entries for design capacity and for QMAX.
- Smaller: a missing cycle counter no longer counts as zero cycles and no
  longer produces a verdict; UFS WriteBooster is read from the register that
  carries it; the wear figure names its band instead of an approximate percent;
  caches without a size are not listed; the hottest zone is among the zones the
  overview shows; unconnected UDP client sockets are not counted as listening;
  a Bluetooth node is not picked by the first two letters of its name.
- The battery page now names three capacities and sources each one separately:
  the design capacity from the driver where nothing contradicts it and from the
  maker's figure where the driver's is demonstrably not this cell, the full
  capacity from the driver or from the gauge's own register, and the current
  capacity as the charge level applied to whichever full capacity holds. The
  figure a driver is shown to have wrong is not displayed at all any more --
  a note says what it reports and why it was dropped. On one phone that means
  5450 and 5584 mAh in place of 3760 twice.
- Rows that could only say "—" are gone: no design voltage, no capacity, no
  identity line where the kernel publishes none. Where a phone exports no
  capacity register at all, a note says so instead of leaving four blanks.
- The words the kernel uses for a state are translated. "Good", "Charging",
  "Fast" and eighteen more are defined by the power-supply class, so they are
  ours to translate; what a vendor invented is left as it is.
- The charge target voltage is read where it exists (constant_charge_voltage,
  or the battery's voltage_max) and labelled as the charger's register rather
  than the cell's: measured on one phone, 4.52 V programmed against 4.447 V at
  the cell while 1.2 A flowed.
- The eight per-band charge counters are gone. They were a Qualcomm register
  printed as a row of numbers with nothing to do with them.
- New in the glossary: design capacity and where it comes from, QMAX and why it
  moves, the charge target, and an entry on what to expect from a phone this
  app was never measured against.
* Thu Sep 03 2026 harbour-sysmetrics contributors 0.3.2-1
- The per-process milliamp figures are gone, and with them the section that
  carried them. Nothing in a phone meters a process. What 0.3.1 showed was a
  share of CPU time weighted by cluster and scaled against the gauge -- a model
  dressed as a measurement, and it read like one. It suggested where it should
  have informed, which is not what this app is for. The processor page still
  ranks processes by CPU time, which is counted rather than modelled.
- The same estimate stood a second time on the process detail page as
  "estimated power share": the device's whole draw multiplied by the process's
  share of busy CPU, display and radios included. Also gone.
- Current and power are no longer printed where the driver publishes no
  current. The Gemini PDA answers its legacy BatteryAverageCurrent with a hard
  zero while discharging, and a printed 0 mA reads as a measurement rather than
  an absent sensor. The rows are omitted instead; voltage and temperature,
  which that gauge does measure, stay.
- The discharge graph plotted MediaTek devices with the wrong sign: the status
  there reads "Cmd discharging", and the check compared the whole string.
- A pass over the thermal zones costs 134 ms on the Jolla Phone (2026), because
  reading a zone is a transaction to the part it measures -- the charge pump
  alone takes 11 ms. It ran every tick whether or not anything showed a
  temperature; it now runs while the overview is in front, and a zone's type is
  read once instead of 56 times a tick.
- The Bluetooth refresh blocked the interface thread on a system-bus round trip
  every five seconds, on every page and in the background. It follows its page
  now.
- The settings file is created 0600, and an existing one is tightened on start.
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
