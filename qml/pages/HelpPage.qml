import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Glossary attached to the right of the main pages (swipe left to reach it).
// Term tokens stay literal; explanations go through qsTr for translation.
Page {
    id: page
    allowedOrientations: Orientation.All

    property var topics: []
    function shownGroups() {
        if (!topics || topics.length === 0) return groups
        var out = []
        for (var i = 0; i < groups.length; ++i)
            if (topics.indexOf(groups[i].key) >= 0) out.push(groups[i])
        return out
    }
    property var groups: [
        {
            title: qsTr("Processor"), key: "cpu",
            items: [
                { t: "CPU %", d: qsTr("Share of one CPU core a process uses. 100 % = one core fully busy; on 8 cores the ceiling is 800 %.") },
                { t: qsTr("System CPU %"), d: qsTr("Total busy time across all cores, 0–100 %, from user + system + irq + softirq + steal jiffies.") },
                { t: qsTr("Cores used"), d: qsTr("How many distinct cores a process actually ran on during the last interval — shows real parallelism.") },
                { t: qsTr("Load average"), d: qsTr("Average number of runnable + waiting tasks over 1/5/15 minutes. Above the core count means the queue is backing up.") },
                { t: qsTr("Frequency"), d: qsTr("Current clock of each core in MHz. Cores scale down when idle to save power.") },
                { t: qsTr("Nice"), d: qsTr("Scheduling politeness, -20 (greedy) to 19 (yielding). Higher nice = less CPU. Lowering it needs privilege.") },
                { t: qsTr("Priority"), d: qsTr("Kernel scheduling priority derived from nice; lower runs sooner.") },
                { t: qsTr("CPU affinity"), d: qsTr("The set of cores a process is allowed to run on.") },
                { t: qsTr("Share of busy CPU"), d: qsTr("How much of all the work the CPU did in the interval was this one process.") }
            ]
        },
        {
            title: qsTr("Process states"), key: "procstate",
            items: [
                { t: "R — " + qsTr("running"), d: qsTr("On a CPU or ready to run right now.") },
                { t: "S — " + qsTr("sleeping"), d: qsTr("Idle, waiting for an event (the normal resting state).") },
                { t: "D — " + qsTr("uninterruptible"), d: qsTr("Blocked in the kernel, usually on I/O, and cannot be interrupted. Many D processes point to storage stalls.") },
                { t: "Z — " + qsTr("zombie"), d: qsTr("Finished but not yet reaped by its parent; holds only a slot in the table.") },
                { t: "T — " + qsTr("stopped"), d: qsTr("Suspended by a signal (SIGSTOP); resumes on SIGCONT.") },
                { t: "I — " + qsTr("idle"), d: qsTr("An idle kernel thread; does not count toward load.") }
            ]
        },
        {
            title: qsTr("Scheduling & faults"), key: "sched",
            items: [
                { t: qsTr("Context switches"), d: qsTr("Times the process was swapped on/off a core. Voluntary = it waited for something; involuntary = the scheduler preempted it.") },
                { t: qsTr("Wakeups/s"), d: qsTr("How often the process leaves sleep per second. A high rate keeps the CPU from sleeping and drains the battery even at low CPU %.") },
                { t: qsTr("Page faults"), d: qsTr("Minor = a memory page was mapped without disk. Major = it had to be read from storage; frequent majors mean memory pressure or heavy mmap I/O.") }
            ]
        },
        {
            title: qsTr("Memory"), key: "mem",
            items: [
                { t: "RSS", d: qsTr("Resident Set Size — physical RAM the process holds, shared libraries counted in full for every user.") },
                { t: "PSS", d: qsTr("Proportional Set Size — like RSS but shared pages are split across the processes sharing them. The fairest single number for real footprint.") },
                { t: "USS", d: qsTr("Unique Set Size — memory private to this process, freed entirely when it exits.") },
                { t: qsTr("Virtual (VmSize)"), d: qsTr("Total address space reserved, most of it never backed by RAM. Almost always much larger than RSS.") },
                { t: "Swap", d: qsTr("Process memory pushed out to swap/zram under pressure.") },
                { t: qsTr("Available"), d: qsTr("RAM that can be handed out without swapping (MemAvailable) — the honest 'free' figure, unlike raw MemFree.") },
                { t: qsTr("Cached"), d: qsTr("File contents kept in RAM to speed re-reads; reclaimable on demand.") }
            ]
        },
        {
            title: qsTr("I/O & energy"), key: "ioenergy",
            items: [
                { t: qsTr("Disk read/write"), d: qsTr("Bytes per second the process moves to and from storage (actual device I/O, not cache hits).") },
                { t: qsTr("Estimated power share"), d: qsTr("Rough milliwatts attributed to the process: its CPU share times the battery's measured power draw. An estimate, not a per-app meter.") },
                { t: qsTr("Power draw"), d: qsTr("Whole-device power right now, current times voltage from the fuel gauge.") }
            ]
        },
        {
            title: qsTr("Process identity"), key: "procid",
            items: [
                { t: "PID / PPID", d: qsTr("Process ID and the ID of its parent (who started it).") },
                { t: "cgroup", d: qsTr("The control group the process belongs to — how the system groups and limits it (a service, an app, a session).") },
                { t: qsTr("OOM score"), d: qsTr("How attractive the process is to the out-of-memory killer under pressure; higher is killed sooner.") },
                { t: qsTr("File descriptors (fds)"), d: qsTr("Open handles the process holds: files, sockets, pipes, devices, timers.") },
                { t: "timerfd", d: qsTr("A timer held as a file descriptor. Many of them means many periodic wake-ups.") },
                { t: qsTr("Executable / cwd"), d: qsTr("The binary on disk that is running, and the process's current working directory.") },
                { t: qsTr("Threads"), d: qsTr("Independent lines of execution inside one process, sharing its memory.") }
            ]
        },
        {
            title: qsTr("Devices"), key: "devices",
            items: [
                { t: qsTr("Node"), d: qsTr("The /dev entry the process opened (a camera, sensor, GPU, serial port, block device …).") },
                { t: qsTr("Subsystem"), d: qsTr("The kernel class the device belongs to (input, sound, block, tty, usb …).") },
                { t: qsTr("Driver"), d: qsTr("The kernel module driving the device.") },
                { t: qsTr("Vendor / Product / Serial"), d: qsTr("Identity read from sysfs by walking up the device tree, when the hardware exposes it.") }
            ]
        },
        {
            title: qsTr("Connections"), key: "conn",
            items: [
                { t: "TCP / UDP", d: qsTr("Connection-oriented vs. datagram transport. v6 marks IPv6.") },
                { t: qsTr("Local / remote endpoint"), d: qsTr("Address and port on this device, and at the other end of the connection.") },
                { t: qsTr("Direction — inbound"), d: qsTr("The other side opened the connection to a listening port on this device (this device is the server).") },
                { t: qsTr("Direction — outbound"), d: qsTr("This device opened the connection to a remote service (this device is the client).") },
                { t: qsTr("Direction — listening"), d: qsTr("An open port waiting for connections; nobody is connected yet.") },
                { t: "ESTABLISHED", d: qsTr("An open, active connection.") },
                { t: "LISTEN", d: qsTr("A server socket accepting new connections.") },
                { t: "TIME_WAIT / CLOSE_WAIT", d: qsTr("A connection being torn down; briefly lingers before the socket is freed.") },
                { t: qsTr("Activity dot"), d: qsTr("Lit when data currently sits in the socket's send/receive queue — the connection is exchanging data now.") },
                { t: qsTr("Socket owner"), d: qsTr("The process holding the socket, found by matching the socket's inode to a process's open handles. Needs root for other users' sockets.") }
            ]
        },
        {
            title: qsTr("Threat assessment"), key: "threat",
            items: [
                { t: qsTr("Public vs. private"), d: qsTr("Remote addresses are classified: private (10.x, 192.168.x, …), loopback, link-local, or public (routable on the internet).") },
                { t: qsTr("Critical (red)"), d: qsTr("SSH (port 22), unencrypted services (telnet/ftp), or inbound connections from public addresses.") },
                { t: qsTr("Elevated (amber)"), d: qsTr("Outbound connections to public addresses.") },
                { t: qsTr("Watch (violet)"), d: qsTr("Ports listening on all interfaces, reachable from the network.") },
                { t: qsTr("SSH is always red"), d: qsTr("A remote shell is the highest-value target, so any SSH connection is flagged critical regardless of who opened it.") }
            ]
        },
        {
            title: qsTr("Access monitor"), key: "access",
            items: [
                { t: qsTr("Traced by"), d: qsTr("Another process is attached as a debugger (ptrace) and can read this process's memory.") },
                { t: qsTr("Watchers"), d: qsTr("Other processes that hold open handles into this process's /proc entry — i.e. are inspecting it.") }
            ]
        },
        {
            title: qsTr("Thermal"), key: "thermal",
            items: [
                { t: qsTr("Thermal zone"), d: qsTr("A temperature sensor the kernel exposes. Phones have many — one per SoC block (CPU clusters, GPU, modem), plus battery, PMIC and skin sensors.") },
                { t: qsTr("Zone names"), d: qsTr("The labels (e.g. cpu-0-0-usr, mtktscpu, gpu, battery, pmic) come straight from the device's kernel and are SoC-specific — not every one maps to something you'd recognise.") },
                { t: qsTr("Max °C"), d: qsTr("The card headline is the hottest zone right now — the number that actually governs throttling.") },
                { t: qsTr("Throttling"), d: qsTr("When a zone gets too hot the kernel lowers CPU/GPU frequencies to cool down, which shows up as reduced clocks and slower performance.") }
            ]
        },
        {
            title: qsTr("Battery"), key: "battery",
            items: [
                { t: qsTr("Capacity"), d: qsTr("Current charge level in percent.") },
                { t: qsTr("State of health"), d: qsTr("Usable capacity versus the design capacity when new. Taken from the fuel gauge's own value when it reports one, otherwise computed from charge_full ÷ charge_full_design.") },
                { t: qsTr("Full cycles"), d: qsTr("Equivalent full charge/discharge cycles counted by the gauge — accumulated charge throughput, not the number of times you plugged in, so it is lower than expected.") },
                { t: qsTr("Current / Voltage"), d: qsTr("Momentary current (mA, negative when charging) and pack voltage from the gauge.") },
                { t: qsTr("Health flag"), d: qsTr("The driver's own verdict (Good, Overheat, Cold, Over voltage, Dead …).") },
                { t: qsTr("Charging protocol"), d: qsTr("How the charger and phone negotiate power. DCP = simple 5 V charger; CDP = charging USB port; Quick Charge / HVDCP = Qualcomm high-voltage; USB Power Delivery (PD) = the modern standard negotiating higher voltages and currents.") },
                { t: qsTr("USB Power Delivery (PD)"), d: qsTr("A protocol where charger and device agree on a voltage/current profile (e.g. 9 V / 3 A), enabling fast charging well beyond standard USB's 2.5 W.") },
                { t: qsTr("Charging power"), d: qsTr("The actual watts flowing into the battery right now — battery charge current times battery voltage. Lower than the charger's rating due to losses and thermal limits.") },
                { t: qsTr("Charge type"), d: qsTr("The charging phase: Fast (constant current, bulk of the charge), Taper (constant voltage, slowing as it fills), Trickle (topping off / protecting a low battery).") },
                { t: qsTr("Top consumers (drain proxy)"), d: qsTr("CPU time is the single biggest battery drain on a phone, so ranking processes by CPU use approximates who is draining the battery. It is an estimate — a process can also drain via wakeups, radio or screen without much CPU.") }
            ]
        },
        {
            title: qsTr("Monitoring"), key: "monitoring",
            items: [
                { t: qsTr("Sampling interval"), d: qsTr("How often the app re-reads /proc and /sys. Shorter is more responsive but uses more CPU.") },
                { t: qsTr("Record mode"), d: qsTr("Accumulates CPU time per process over a session and ranks the consumers, catching short-lived processes an instant view misses.") },
                { t: qsTr("Root mode"), d: qsTr("An optional root helper that lets the app inspect processes of other users (system daemons) fully. Read-only.") }
            ]
        },
        {
            title: qsTr("Sensors"), key: "sensors",
            items: [
                { t: qsTr("Accelerometer"), d: qsTr("Measures linear acceleration on three axes (m/s²), including gravity — how the device is tilted and moved.") },
                { t: qsTr("Gyroscope"), d: qsTr("Measures angular velocity (°/s) — how fast the device is rotating around each axis.") },
                { t: qsTr("Magnetometer / Compass"), d: qsTr("Measures the magnetic field (µT); combined with the accelerometer it yields the compass heading (azimuth).") },
                { t: qsTr("Proximity"), d: qsTr("A short-range sensor near the earpiece; reports near/far, used to blank the screen during calls.") },
                { t: qsTr("Ambient light"), d: qsTr("Measures surrounding brightness; drives automatic display brightness.") },
                { t: qsTr("GPS fix / TTFF"), d: qsTr("A fix is a computed position from enough satellites. TTFF (time to first fix) is how long the receiver needed from cold start — seconds with a clear sky, longer indoors.") },
                { t: qsTr("Accuracy"), d: qsTr("The estimated horizontal error radius of the position, in metres — smaller is better.") }
            ]
        },
        {
            title: qsTr("Camera"), key: "camera",
            items: [
                { t: qsTr("Image sensor"), d: qsTr("The photodiode array that converts light to charge. Identified here by part number (e.g. Sony IMX486) read from the vendor camera modules.") },
                { t: qsTr("Bayer CFA (RGGB)"), d: qsTr("Colour-filter array over the pixels: a repeating 2×2 of red, two greens, one blue. Each pixel captures one primary; the ISP interpolates the rest (demosaicing). Three primaries → full RGB.") },
                { t: qsTr("Bit depth"), d: qsTr("Bits per pixel in the raw readout, typically 10-bit (1024 levels per channel) on mobile sensors, before tone-mapping to 8-bit output.") },
                { t: qsTr("Optical format"), d: qsTr("The sensor's diagonal size as a fraction of an inch (e.g. 1/2.9\"). With the pixel count it gives the pixel pitch. A datasheet spec of the part — not queryable from the device.") },
                { t: qsTr("ISP"), d: qsTr("Image Signal Processor — the SoC block that demosaics, denoises, white-balances and encodes the sensor stream.") },
                { t: qsTr("CAMSS / cam-req-mgr"), d: qsTr("Qualcomm's camera subsystem in the kernel. It exposes control nodes (cam-req-mgr, cam_sync), not per-camera capture devices — capture runs through the userspace HAL (camx).") },
                { t: qsTr("EEPROM (calibration)"), d: qsTr("A small memory beside each module holding per-unit factory calibration: lens shading, autofocus range, colour.") },
                { t: qsTr("Capture mode"), d: qsTr("A sensor output configuration (resolution + frame rate + binning). Modes live in the HAL and are enumerable only on a running camera, not via V4L2.") }
            ]
        },
        {
            title: qsTr("Modem / SIM"), key: "modem",
            items: [
                { t: qsTr("IMEI"), d: qsTr("International Mobile Equipment Identity — the modem's unique 15-digit hardware serial. Identifies the device on the network, independent of the SIM.") },
                { t: qsTr("IMSI"), d: qsTr("International Mobile Subscriber Identity — the subscriber ID stored on the SIM. Begins with the MCC+MNC of the home network.") },
                { t: qsTr("ICCID"), d: qsTr("The SIM card's own serial number, printed on the card. Identifies the physical SIM, not the subscriber.") },
                { t: qsTr("MCC / MNC"), d: qsTr("Mobile Country Code + Mobile Network Code — together they name the operator (e.g. 262/01 = Germany, Telekom). Present both on the SIM (home) and from the network (serving).") },
                { t: qsTr("APN"), d: qsTr("Access Point Name — the gateway name the modem uses to open a mobile-data (packet) connection to the operator.") },
                { t: qsTr("Radio technology"), d: qsTr("The active air interface: GSM (2G), UMTS (3G), LTE (4G), NR (5G). Determines throughput and latency.") },
                { t: qsTr("Cell ID / LAC / TAC"), d: qsTr("The identifier of the serving base station cell, and the Location/Tracking Area it belongs to. Used for paging and, roughly, for locating the device.") },
                { t: qsTr("Signal strength"), d: qsTr("The received signal quality as a percentage from ofono. Underlying metric is RSRP/RSSI in dBm depending on technology.") },
                { t: qsTr("PIN / PUK"), d: qsTr("PIN locks the SIM at power-on; after three wrong PINs the SIM blocks and needs the longer PUK to unlock.") },
                { t: qsTr("ofono"), d: qsTr("The telephony daemon on Sailfish OS. It talks to the modem over RIL and exposes modem, SIM, network and data state on D-Bus — the source of everything on this page.") }
            ]
        },
        {
            title: qsTr("USB & charging"), key: "usb",
            items: [
                { t: qsTr("USB-C / CC"), d: qsTr("The Configuration Channel pins on a USB-C plug. They detect attach, cable orientation and the advertised current, and carry the Power Delivery messages.") },
                { t: qsTr("Type-C current advertisement"), d: qsTr("Before any negotiation, a resistor (Rp) on CC signals how much the port offers: 500 mA (default USB), 1.5 A or 3.0 A. Purely analog — no protocol.") },
                { t: qsTr("USB Power Delivery (PD)"), d: qsTr("A negotiation protocol over CC: source and sink agree on a voltage/current contract (5–48 V, up to 240 W in PD 3.1). Governs fast charging on modern devices.") },
                { t: qsTr("Quick Charge"), d: qsTr("Qualcomm's proprietary fast-charge scheme. Older versions signal on the D+/D− data lines; QC4+ rides on PD. Negotiated between charger and PMIC.") },
                { t: qsTr("Data role (DFP/UFP/DRP)"), d: qsTr("DFP = host (downstream-facing), UFP = device (upstream-facing), DRP = dual-role that can be either. A phone is usually UFP to a PC and DFP to a stick.") },
                { t: qsTr("VCONN"), d: qsTr("Power (on the unused CC2 pin) that feeds the active chip inside an electronically-marked cable, so it can answer identity queries.") },
                { t: qsTr("e-marker"), d: qsTr("A chip built into higher-rated USB-C cables that declares the cable's current rating, data speed and a coarse length. Read over PD, not from the wires.") },
                { t: qsTr("SOP / SOP′ / SOP″"), d: qsTr("PD packet targets: SOP addresses the device at the far end, SOP′/SOP″ address the cable's plugs (the e-markers). Discover Identity on SOP′ reads the cable.") },
                { t: qsTr("TDR"), d: qsTr("Time-Domain Reflectometry: send a fast edge and time its reflection to compute cable length and locate faults. Needs PHY support; phone USB PHYs expose none, so length is not measurable here.") }
            ]
        },
        {
            title: qsTr("Storage"), key: "storage",
            items: [
                { t: qsTr("UFS"), d: qsTr("Universal Flash Storage — the current phone storage standard. Full-duplex serial link, command queueing; faster than the older eMMC.") },
                { t: qsTr("SCSI / LUN"), d: qsTr("UFS speaks the SCSI command set. The chip presents several Logical Units (LUNs): one large user area plus small boot and RPMB units. The capacity shown is the user LUN.") },
                { t: qsTr("Raw vs usable capacity"), d: qsTr("Marketing capacity counts raw NAND in powers of ten (64 GB = 64·10⁹). The OS counts usable space in powers of two (GiB) after over-provisioning and metadata, so 64 GB shows as ~59.6 GiB.") },
                { t: qsTr("Over-provisioning"), d: qsTr("Spare NAND the controller keeps hidden for wear-levelling and bad-block replacement — part of why raw and usable differ.") },
                { t: qsTr("Wear / lifetime"), d: qsTr("UFS reports a health estimate (bDeviceLifeTimeEst) in 10% steps from the count of program/erase cycles used. \"Good\" means most of the endurance budget is unused.") },
                { t: qsTr("Block / erase block"), d: qsTr("NAND is read/written in pages but erased in larger blocks. Logical blocks (sectors, usually 4 KiB) are the unit the filesystem addresses.") },
                { t: qsTr("RPMB"), d: qsTr("Replay-Protected Memory Block — a small authenticated LUN for anti-rollback and secure counters, not general storage.") }
            ]
        }
    ]

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Glossary") }

            Label {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Every figure the app shows, explained. Tap a term to reveal its details.")
            }

            // Each term is its own collapsed row; tapping expands the explanation.
            Repeater {
                model: page.shownGroups()
                Column {
                    width: page.width
                    SectionHeader { text: modelData.title }
                    ExpandingSectionGroup {
                        width: page.width
                        Repeater {
                            model: modelData.items
                            ExpandingSection {
                                title: modelData.t
                                content.sourceComponent: Component {
                                    Column {
                                        width: page.width
                                        Label {
                                            x: Theme.horizontalPageMargin
                                            width: page.width - 2 * Theme.horizontalPageMargin
                                            text: modelData.d
                                            font.pixelSize: Theme.fontSizeExtraSmall
                                            color: Theme.primaryColor
                                            wrapMode: Text.Wrap
                                        }
                                        Item { width: 1; height: Theme.paddingMedium }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
