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
                { t: qsTr("Share of busy CPU"), d: qsTr("How much of all the work the CPU did in the interval was this one process.") },
                { t: qsTr("CPU time since boot"), d: qsTr("The kernel tallies every tick of every core into user, kernel, idle and waiting. Summed over all cores, which is why the total exceeds the uptime — eight cores accumulate eight seconds per second.") },
                { t: qsTr("Waiting for storage"), d: qsTr("Time a core sat idle only because a read or write had not come back yet. Constantly high means storage, not the processor, is the limit.") }
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
                { t: qsTr("Cached"), d: qsTr("File contents kept in RAM to speed re-reads; reclaimable on demand.") },
                { t: qsTr("LPDDR"), d: qsTr("Low-Power DDR — the mobile DRAM standard. The type (e.g. LPDDR4X, LPDDR5) fixes the clock range, bus width and command set. Read here from the bootloader's device-tree entry.") },
                { t: qsTr("Mode registers (MR5–MR8)"), d: qsTr("Small on-die registers a DRAM reports at boot: MR5 = manufacturer, MR6 = revision, MR8 = density and I/O width. The bootloader reads them into SMEM, but this platform does not surface them to software — so maker and organisation stay unknown.") },
                { t: qsTr("Ranks / channels / banks"), d: qsTr("How the DRAM is built: a package holds one or more channels (independent buses), each channel one or more ranks (sets of dies selected together), each die a fixed number of banks. This organisation is a JEDEC/datasheet property of the part, not a runtime-readable value here.") },
                { t: qsTr("Physical memory map"), d: qsTr("The address regions the kernel maps, carved around firmware-reserved areas. It reflects how RAM sits in the address space — not the chip's internal rank/channel layout.") },
                { t: qsTr("Swapped in / out"), d: qsTr("The running total of memory pages moved between RAM and storage since the last start. Swapped out is what left, swapped back in is what had to be fetched again — the round trip costs time and write cycles.") },
                { t: qsTr("Killed for memory"), d: qsTr("The out-of-memory killer ends a process when RAM runs out, to keep the system usable. Any number above zero means the phone was at its limit.") }
            ]
        },
        {
            title: qsTr("I/O & energy"), key: "ioenergy",
            items: [
                { t: qsTr("Disk read/write"), d: qsTr("Bytes per second the process moves to and from storage (actual device I/O, not cache hits).") },
                { t: qsTr("Estimated power share"), d: qsTr("Milliwatts attributed to a process: its share of CPU time multiplied by the draw the battery gauge measures for the whole device. An estimate, not a per-app meter — a process that keeps the screen or the radio busy costs far more than its CPU share suggests, and one that only waits costs less. Shown rounded to 10 mW because nothing finer is measured, and marked amber above 300 mW, a line this app draws; the hardware reports no per-process limit.") },
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
                { t: qsTr("Socket owner"), d: qsTr("The process holding the socket, found by matching the socket's inode to a process's open handles. Needs root for other users' sockets.") },
                { t: qsTr("Default route"), d: qsTr("The way out. A phone has several interfaces up at once — Wi-Fi, mobile data, tunnels — and the routing table decides which one a packet takes when no more specific rule applies: the default route. That is the interface actually carrying your traffic, which is why the network card names it. With Wi-Fi and mobile data both connected, each offers a default route and the one with the lower metric wins; on Sailfish that is normally Wi-Fi. It changes by itself when you leave the house.") },
                { t: qsTr("Traffic per interface"), d: qsTr("Bytes an interface has carried since it was created — not since the phone started. The counter belongs to the network device and lives exactly as long as that device does. Switching the radio off is not enough to reset it: on the Xperia 10 III up to SFOS 5.1.0.11, flight mode left both the interface and its total untouched. It starts over only when the device itself is torn down and built anew, as after a driver reload or a firmware crash. Interfaces that exist only while a connection does, like the mobile data ones, count from the moment they appeared. Linux keeps no counter that sums network volume across the whole uptime, which is why none is offered here.") }
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
                { t: qsTr("Not every zone is a sensor"), d: qsTr("Qualcomm registers its battery watchdogs in the same thermal framework, because throttling runs through it — so their reading carries milliamps, millivolts or percent, which the framework then labels °C. Verified against the fuel gauge on the Xperia 10 III: pm7250b-vbat-lvl* is the battery voltage, pm7250b-ibat-lvl* the battery current, soc the charge level. This app leaves them out of the list; they are on the battery card, in their own units.") },
                { t: qsTr("Corrected readings (*)"), d: qsTr("An asterisk marks a figure this app recomputed instead of passing the kernel's through unchanged. There is currently one such case. On the Xperia 10 III the zone camera-therm-usr is the thermistor beside the rear flash LED, and its voltage divider uses a 400 kOhm pull-up while the PMIC's ADC driver applies its 100 kOhm lookup table. The measured resistance therefore comes out four times too low, which the table turns into roughly 35 K too much: the zone reports about 63 °C on a phone lying idle at room temperature. Because the mistake is a pure shift in 1/T on the table's B=4250 curve, it can be undone exactly rather than estimated. Checked with a radiometric camera against the glass above the LED, with the flash as the heat source: 41 and 42 °C measured, 40.2 and 42.7 °C after correction, against 75.9 and 79.0 °C as the kernel exports them. The real fix belongs in the kernel's ADC channel table; until it lands there, this app corrects the figure and says so.") },
                { t: qsTr("Max °C"), d: qsTr("The card headline is the hottest zone right now — the number that actually governs throttling.") },
                { t: qsTr("Throttling"), d: qsTr("When a zone gets too hot the kernel lowers CPU/GPU frequencies to cool down, which shows up as reduced clocks and slower performance.") },
                { t: qsTr("Trip point"), d: qsTr("The temperature at which the kernel is supposed to act on a zone. A passive trip asks for throttling, an active trip starts a fan, a critical trip shuts the device down. Trips around 115 °C are silicon emergency stops, not operating limits — where they are the only ones a zone has, everyday regulation happens somewhere else, or not at all.") },
                { t: qsTr("Cooling device"), d: qsTr("Something the kernel can turn down to shed heat: a CPU frequency cap, the backlight, the charging current. It is shown as the current step out of the highest step, so 0/14 means fifteen levels available and none in use.") },
                { t: qsTr("Bound to a zone"), d: qsTr("A cooling device only does anything when a thermal zone binds it. An unbound one is a lever nothing pulls — the kernel's governor cannot reach it. On the Jolla Phone (2026) not a single cooling device is bound to any zone, and the device still throttles its charging at about 45 °C: the vendor's charger driver does it internally, where the thermal framework never sees it. So a cooling device resting at zero is not evidence that nothing is being limited.") },
                { t: qsTr("A zone is not the part it is named after"), d: qsTr("The distance between a zone and the thing it is named for is not a constant — it grows with the power going through the device. Measured on a Sony Xperia 10 III while charging: the zone beside the charger sat 21.6 K above the cell above 2 A, 7.6 K between 0.5 and 2 A, and 5.7 K below that. At idle, unplugged, it was 1 K. On the Jolla Phone (2026) under load the hottest SoC zone stood 45 K above the radio zone in the same phone. So a zone named after a component tells you what its own sensor reads, and any conclusion about the component itself needs to account for a gap that changes with load.") },
                { t: qsTr("Marked ⚠"), d: qsTr("A zone sitting near freezing while the rest of the board is clearly warm, by a margin wider than any spread this project has measured. A working sensor on a warm board does not do that; a node carrying millivolts, registered in the thermal framework and then labelled °C by it, does — a reading of 4400 among readings of 33000 comes out as a believable 4.4 °C. The mark is a hint and nothing more: the reading stays on the page with its raw figure beside it, because a rule of thumb does not get to delete a measurement. Compare the raw number against its neighbours and decide.") },
                { t: qsTr("Zones without a reading"), d: qsTr("A registered zone that answers with nothing, or with zero. Empty usually means a sensor the board never wired up — fourteen of the fifty-six zones on the Jolla Phone (2026) are like that, the camera and virtual-skin ones. Zero usually means a stage that is not powered, not a zone at freezing point. Both are listed on the thermal page with the reason, because the kernel does register them.") }
            ]
        },
        {
            title: qsTr("Battery"), key: "battery",
            items: [
                { t: qsTr("Capacity"), d: qsTr("Current charge level in percent.") },
                { t: qsTr("State of health"), d: qsTr("Usable capacity versus the design capacity when new. Taken from the fuel gauge's own value when it reports one, otherwise computed from charge_full ÷ charge_full_design.") },
                { t: qsTr("Quality — our reading"), d: qsTr("The word beside the battery is not something the battery reports; it is this app's reading of the state of health. The lines are drawn at 90 % (as new), 80 % (good), 65 % (aged) and 50 % (worn), and below that it suggests considering a replacement. Because the basis matters, the line says where the number came from: a state of health the gauge stands behind, or one computed from charge_full ÷ charge_full_design. Where there is no state of health at all, the wording falls back to the cycle count alone — under 300, 600, 1000 — and says so. That last case is the weakest of the three: cells age very differently for the same number of cycles.") },
                { t: qsTr("Band crossings"), d: qsTr("The gauge keeps eight counters, one per state-of-charge band, and raises one whenever that band is crossed. Their sum is the charging actually done; the cycle figure the gauge reports is their mean, which is why it comes out far lower. Charging mostly in the upper bands — plugging in at 60 % rather than running the cell down — fills those counters unevenly and drags the mean further from the real load.") },
                { t: qsTr("Capacity measurements"), d: qsTr("How often the gauge has completed a learning pass and derived a capacity of its own. At zero it has never measured one, and the full capacity it reports comes from the battery profile rather than from this cell — which decides whether a health percentage means ageing or merely compares two catalogue figures.") },
                { t: qsTr("Battery profile"), d: qsTr("The identifier of the parameter set the gauge loaded for the installed cell: nominal capacity, voltage curve, resistance. A replacement cell can bring a different profile, and with it different nominal figures than the original.") },
                { t: qsTr("soh register"), d: qsTr("A value the gauge offers as \"state of health\" — but in the Qualcomm driver it is not measured at all: the register is written from outside, by whatever component claims to know better, and simply returned on read. Where nothing ever writes it, it keeps its initial value, which is why a flat 100 is so common. It is shown as its own line rather than as the health figure, so it can be compared with the capacities instead of standing in for them.") },
                { t: qsTr("Ageing profile"), d: qsTr("Some devices ship several battery profiles, one per stage of ageing, and the gauge switches to a later one as the cell wears — a lower end-of-charge voltage, a different voltage curve. The number is the index of the profile in use, counted from zero; how many exist is fixed in the device tree of that phone, so \"step 0\" means the original profile and not a score out of anything.") },
                { t: qsTr("Internal resistance (ESR)"), d: qsTr("The equivalent series resistance the gauge last measured, in milliohm. It is what makes the voltage sag under load, and it rises as a cell ages — the most direct wear signal the gauge offers. Measured in pulses rather than continuously, so it does not follow the current draw from moment to moment. Useful as a trend on one device; the absolute figure depends on the cell type.") },
                { t: qsTr("Total resistance (stored)"), d: qsTr("A calibration figure held in the gauge's persistent storage, covering the whole path — cell plus contacts and wiring — and written there by the platform rather than measured by the gauge itself. It therefore sits higher than the cell's own ESR and does not move as the cell ages. Comparing the two numbers says nothing: one is what was filed away about the whole path, the other is what was just measured across the cell. Only the ESR is worth watching over time.") },
                { t: qsTr("Battery ID resistor"), d: qsTr("A resistor in the battery pack, in ohm, that tells the phone which cell is fitted so the right profile is loaded. It identifies the battery and says nothing about its condition.") },
                { t: qsTr("Full cycles"), d: qsTr("Equivalent full charge/discharge cycles counted by the gauge — accumulated charge throughput, not the number of times you plugged in, so it is lower than expected.") },
                { t: qsTr("Current / Voltage"), d: qsTr("Momentary current (mA, negative when charging) and pack voltage from the gauge.") },
                { t: qsTr("At the charger, current is not consumption"), d: qsTr("The current the gauge reports is the current through the cell, not what the device is drawing. On the cable both happen at once: the charger feeds the system and fills the cell from the same input, the two branches part at the charging node, and the gauge only ever sees the one into the cell. A phone charging under full load can therefore report the same battery current as one lying idle — the difference goes past the gauge, not through it. Separating them would mean input voltage times input current times the efficiency of the converter, and no vendor publishes that efficiency; at 5 A in, an assumption five points off is already a 250 mA error. So while charging this page shows the rate into the battery and no consumption figure. It becomes readable again after unplugging, and even then only once the gauge has turned its sign around and refilled its internal average, which takes seconds to a minute depending on the chip. There is one case where the input does measure consumption: a cell already full. With nothing going into it, what comes in is what the device is using.") },
                { t: qsTr("Health flag"), d: qsTr("The driver's own verdict (Good, Overheat, Cold, Over voltage, Dead …).") },
                { t: qsTr("Charging protocol"), d: qsTr("How the charger and phone negotiate power. DCP = simple 5 V charger; CDP = charging USB port; Quick Charge / HVDCP = Qualcomm high-voltage; USB Power Delivery (PD) = the modern standard negotiating higher voltages and currents.") },
                { t: qsTr("USB Power Delivery (PD)"), d: qsTr("A protocol where charger and device agree on a voltage/current profile (e.g. 9 V / 3 A), enabling fast charging well beyond standard USB's 2.5 W.") },
                { t: qsTr("Charging power"), d: qsTr("The actual watts flowing into the battery right now — battery charge current times battery voltage. Lower than the charger's rating due to losses and thermal limits.") },
                { t: qsTr("Charge type"), d: qsTr("The charging phase: Fast (constant current, bulk of the charge), Taper (constant voltage, slowing as it fills), Trickle (topping off / protecting a low battery).") },
                { t: qsTr("Where these battery figures come from"), d: qsTr("Each value on the battery page is a register of the kernel's power-supply interface, and what it means is fixed by the driver behind it. On Qualcomm phones that is the QG gauge, drivers/power/supply/qcom/qpnp-qg.c — readable in any copyleft kernel release for the device. There: RESISTANCE_NOW hands out esr_last, the resistance computed in qg_esr_estimate() as ΔV·1000 ÷ ΔI from a measurement pulse and then filtered, in milliohm. RESISTANCE hands out the RBAT value from the gauge's persistent store multiplied by 1000, so microohm, and it covers the whole path including contacts. RESISTANCE_ID hands out batt_id_ohm, the identification resistor, in ohm. SOH hands back whatever was last written into that register — the driver measures nothing for it. ESR_ACTUAL and ESR_NOMINAL answer -22, which is -EINVAL passed through, whenever they were never set. Other chips use other drivers, so this holds for Qualcomm gauges and not for every phone.") },
                { t: qsTr("Where the current goes"), d: qsTr("There is no per-process ammeter in a phone. The gauge measures one number, the sum of everything, once — so a figure beside a process name can only ever be a share of that sum, worked out from how much CPU work the process caused. Two device measurements go into it and no estimate of this app\'s own: the gauge, and the kernel\'s energy model, which lists what a second of CPU costs on each cluster of this chip at each frequency. That second part matters more than it sounds. On the Jolla Phone (2026) the energy model puts a big core at 8.2 times a little one, and a load step measured against the gauge put it at 7.2 — so weighting by cluster is not a refinement, it is the difference between a ranking that means something and one that does not.") },
                { t: qsTr("Why the milliamps are a floor"), d: qsTr("The energy model does not only rank the clusters, it states what a core actually draws at each frequency — so a share of CPU time converts into power, and the cell voltage turns that into milliamps, with nothing calibrated and nothing assumed. What the model leaves out is everything around the cores: leakage, the L3, the memory controller, and the loss in the regulator between cell and rail. Measured against a load step on the Jolla Phone (2026), the true figure came to about twice the model\'s. So the number beside a process is the part that can be accounted for, and the real cost is higher — it is shown as it is rather than multiplied by a constant taken from one device, which would be wrong on the next.") },
                { t: qsTr("When the gauge supplies the scale instead"), d: qsTr("There is a second route to the same factor, and where it works it is better, because it is measured on the device in hand: watch the current move as the CPU load moves, and the ratio between the two is what one unit of the model costs here. Whether that works depends on the gauge. On the Jolla Phone (2026), with the display off, current_now returned 67 mA and 644 mA seconds apart under identical full load, and a factor fitted to that came out twice as large or half as large depending on which minutes were used. Above 35 % spread it is not used at all and the energy model stands alone. Where it is used, the note on the page says so and gives the spread.") },
                { t: qsTr("Everything else"), d: qsTr("The remainder between what the gauge reports and what the CPU accounts for: display, radios, modem, sensors, the idle draw of the board. It is usually the larger half. It is named rather than distributed, because none of the processes caused it — and a process that keeps the modem busy or holds a wakelock costs battery there without spending CPU time, so it will not appear in the list above at all. That is what the wakelock section beside it is for.") }
            ]
        },
        {
            title: qsTr("Kernel surface"), key: "raw",
            items: [
                { t: qsTr("Raw nodes"), d: qsTr("Everything a subsystem exports in sysfs or procfs, listed attribute by attribute exactly as the kernel wrote it. The sections above them are curated — each row there is a value whose meaning was established first. This is the remainder: the vendor additions nobody wrote a label for. Nothing in it is converted or interpreted.") },
                { t: qsTr("Units are not uniform"), d: qsTr("The same attribute name can carry different units on different nodes of the same device. On the Jolla Phone (2026) the battery reports its current in microamps while the charger driver's ADC beside it reports milliamps — a factor of a thousand, with nothing in sysfs to announce it. That is why raw values are shown raw: a converted figure would have to guess, and a wrong unit is worse than none.") },
                { t: "-1", d: qsTr("On vendor charger nodes -1 usually means unset, not minus one. A driver that holds no override writes -1 rather than leaving the file empty.") },
                { t: qsTr("Device tree"), d: qsTr("The table the bootloader hands the kernel to describe the board. Every chip that gets a driver appears in it as a node with a compatible string — the vendor's own name for the part — which makes it the phone's parts list, and the only place some hardware is named at all: amplifiers on I2C, the fingerprint reader on SPI, the regulators inside each PMIC. Nodes marked disabled are silicon the SoC has and this device does not wire up.") },
                { t: qsTr("Module version"), d: qsTr("A kernel module may carry a version string of its own. For the Mali graphics driver it is the only place an ordinary process can read the driver release — the GPU device directory has no version node, and the call that would answer needs the GPU opened. That release name is what the GPU vendor's security advisories are written against.") },
                { t: qsTr("Catalogue figures"), d: qsTr("Specifications published by the chip vendor for the part, carried inside this app because the kernel does not hold them: the device tree names the SoC and stops. They are not measurements of this device, and every row says who published it. Where the vendor published nothing, the row says so instead of borrowing a number from a spec database.") }
            ]
        },
        {
            title: qsTr("Firmware & risk"), key: "firmware",
            items: [
                { t: qsTr("There is no one firmware version"), d: qsTr("A phone is a dozen computers. The kernel has a version, every kernel module may have one, every radio loads a blob of its own, and the storage, the charger, each USB device and the modem each run software inside their own controller with a release nobody else knows about. That is why these figures sit beside the hardware they belong to instead of on one page: the version of the Wi-Fi firmware is a fact about the Wi-Fi.") },
                { t: qsTr("Driver version and build hash"), d: qsTr("A kernel module may state a release name, or only the hash of the source it was built from, or neither. The hash is worth showing: it cannot be read as a version, but it tells two builds apart, which is exactly the question when a vendor ships an update without renaming anything.") },
                { t: qsTr("Out of tree, unsigned"), d: qsTr("What the kernel records about where a module came from. Out-of-tree means it is not part of the Linux source; unsigned means the kernel did not verify a signature on it. On a vendor phone kernel almost every driver is both, because that is how such kernels are built — it is the normal case here, not a fault, and it is shown because the kernel bothers to track it.") },
                { t: qsTr("USB release number"), d: qsTr("Every USB device reports a release number of its own, and it is the closest thing such a device has to a firmware version. It is the manufacturer's own numbering, not a date, so it means something only against another unit of the same product.") },
                { t: qsTr("Controller firmware"), d: qsTr("Storage is a small computer. A UFS or eMMC part runs firmware inside the chip and reports a revision for it, which is separate from the health of the flash memory it manages — a controller update does not make worn cells young.") },
                { t: qsTr("Firmware files on disk"), d: qsTr("The images the kernel would load into a radio, a signal processor or a sensor when it starts them. The names carry the chip family and the dates say when the vendor last touched them. Their presence says what is available to load; it is not proof that any of it was loaded.") },
                { t: qsTr("Security patch level"), d: qsTr("The month of fixes the Android base under this system was built with. It matters because chipset advisories — the baseband and driver fixes a phone receives — land in that base and not in the system on top of it, and nothing on the device reports them one by one. Its age is shown rather than a verdict: a port is frozen at the base its maker built against, and a fix can be backported without moving the date.") },
                { t: qsTr("How exposed the kernel is"), d: qsTr("A handful of switches that each decide whether a whole class of local attack is available at all: whether kernel addresses are hidden, whether the kernel log can be read, whether one process may debug another, whether the address space is randomised. A phone distribution deliberately relaxes several of them so that ordinary tools keep working, so a value in amber means worth knowing, not broken. The safer setting is named beside each one.") },
                { t: qsTr("World-writable control node"), d: qsTr("A file in sysfs that any process on the device may write, where the rest of its directory belongs to root. This app never writes any of them; it reports them because a control over charging or power that anything can reach is worth knowing about, and the permission is a fact that can be read without touching anything.") },
                { t: qsTr("A version is not a verdict"), d: qsTr("Where this app places a driver release inside a published advisory range, it is saying exactly that and nothing more. Vendors fork drivers and backport fixes without renaming the release, so the name places the driver in the range while only the vendor's changelog can say whether the fix is in. The advisory, the range and the release are shown side by side so the reader can draw the conclusion.") },
                { t: qsTr("Reads that are not reads"), d: qsTr("A few vendor files do work when they are read. One starts a memory test, one sleeps inside the kernel, one clears the fault latches of the camera flash controller — that last is how the chip is re-armed after it shuts itself off. An exhaustive dump has to know where to stop, so this app keeps a list of files it will never open, no matter what its permissions say.") }
            ]
        },
        {
            title: qsTr("Diagnosis"), key: "diagnosis",
            items: [
                { t: qsTr("Mitigation vs. Vulnerable"), d: qsTr("The kernel reports each speculative-execution issue per CPU: 'Mitigation: …' names the active countermeasure (fix in place, shown green), 'Vulnerable' means this kernel build carries no fix (red), 'Not affected' means the CPU's microarchitecture cannot express the attack at all — such entries are not listed on the diagnosis card.") },
                { t: "Spectre v1 / v2", d: qsTr("Speculative execution runs code past unresolved branches and leaves traces in the caches. v1 (bounds-check bypass) tricks speculation past an array bounds check; v2 (branch target injection) poisons the branch predictor to steer speculation into attacker-chosen code. Both affect out-of-order ARM cores (A72, A76, A77 …); mitigations are pointer sanitization (v1) and predictor hardening/CSV2+BHB (v2).") },
                { t: qsTr("Spectre-BHB"), d: qsTr("A v2 refinement: the branch *history* buffer is poisoned instead of the target buffer, bypassing the first round of v2 hardware fixes. Affects newer ARM cores (Cortex-A77 and later); mitigated with history-clearing loop sequences in the kernel.") },
                { t: qsTr("Speculative Store Bypass (v4)"), d: qsTr("The CPU speculatively lets a load run before an older store to the same address is resolved, briefly exposing stale data. Mitigated per process (prctl/SSBS) rather than globally, because the global fix is expensive.") },
                { t: "Meltdown", d: qsTr("Rogue data cache load: on affected CPUs, a user-space access to kernel memory is only faulted *after* speculation already fetched the data into the cache. Broadly an Intel issue; among ARM cores essentially only Cortex-A75. In-order cores (A53, A55) and post-A75 designs (A76, A77 …) fault before the fetch — they are structurally not affected.") },
                { t: qsTr("x86-only classes (MDS, L1TF, TAA, SRBDS …)"), d: qsTr("Several listed classes exploit Intel-specific microarchitecture and cannot occur on ARM SoCs: MDS/TAA sample stale data from fill/store buffers shared between hyper-threads (these SoCs have no SMT); L1TF abuses Intel's handling of not-present page-table entries; TAA needs the TSX transactional-memory extension (ARM has none); SRBDS leaks the on-chip RNG through a shared microcode buffer; iTLB multihit and MMIO stale data target Intel TLB and chipset behavior. The kernel prints 'Not affected' for them; the diagnosis card therefore omits them.") }
            ]
        },
        {
            title: qsTr("Monitoring"), key: "monitoring",
            items: [
                { t: qsTr("Sampling interval"), d: qsTr("How often the app re-reads /proc and /sys. Shorter is more responsive but uses more CPU.") },
                { t: qsTr("Record mode"), d: qsTr("Accumulates CPU time per process over a session and ranks the consumers, catching short-lived processes an instant view misses.") },
                { t: qsTr("Root mode"), d: qsTr("An optional root helper that lets the app inspect processes of other users (system daemons) fully, read the kernel charger log and pull journal excerpts for a bug report. Reading is limited to a fixed list of files; signals and renice reach one named process, the same ones the process detail page offers without root.") },
                { t: qsTr("Colour scale"), d: qsTr("Green, amber and red are this app's grading, not a signal from the device. Processor load turns amber at 50 % and red at 80 %. A filesystem turns amber above 75 % and red above 90 %. Battery charge turns red below 20 %. State of health turns amber below 80 % and red below 65 %. Where a colour stands next to a figure, the figure is the evidence and the colour only the opinion about it.") }
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
                { t: qsTr("Capture mode"), d: qsTr("A sensor output configuration (resolution + frame rate + binning). Modes live in the HAL and are enumerable only on a running camera, not via V4L2.") },
                { t: qsTr("Camera provider crash (Xperia 10 III)"), d: qsTr("Defect: stopping a video recording crashes the Android camera service — CamX dlopens libswregistrationalgo.so from /odm/lib64, which the Sailfish port does not ship (sonyxperiadev bug #761, known since 2022). Fix — extract the proprietary library from the device's own Android firmware, then one of two ways: (1) copy it straight into /odm/lib64 (remount rw; simple, but gone after a reflash of odm), or (2) keep it in /data and bind-mount it over /odm/lib64 via a boot unit (survives OS updates). Both are system-wide, every camera app benefits. Step-by-step details in the README (GitHub only):\ngithub.com/JimKnopfIoT/harbour-advanced-camera") }
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
                { t: qsTr("Explicit / implicit contract"), d: qsTr("The specification's word for the agreed supply — shown here as \"Negotiated\", because that is what it says. Implicit: not a single PD message has been exchanged, 5 V applies from the CC resistors alone. Explicit: the source offered its objects, the sink requested one, the source accepted. Both sides are bound from then on — the source to deliver that voltage, the sink not to draw more than it asked for.") },
                { t: qsTr("Quick Charge"), d: qsTr("Qualcomm's proprietary fast-charge scheme. Older versions signal on the D+/D− data lines; QC4+ rides on PD. Negotiated between charger and PMIC.") },
                { t: qsTr("Data role (DFP/UFP/DRP)"), d: qsTr("DFP = host (downstream-facing), UFP = device (upstream-facing), DRP = dual-role that can be either. A phone is usually UFP to a PC and DFP to a stick.") },
                { t: qsTr("VCONN"), d: qsTr("Power (on the unused CC2 pin) that feeds the active chip inside an electronically-marked cable, so it can answer identity queries.") },
                { t: qsTr("e-marker"), d: qsTr("A chip built into higher-rated USB-C cables that declares the cable's current rating, data speed and a coarse length. Read over PD, not from the wires.") },
                { t: qsTr("SOP / SOP′ / SOP″"), d: qsTr("PD packet targets: SOP addresses the device at the far end, SOP′/SOP″ address the cable's plugs (the e-markers). Discover Identity on SOP′ reads the cable.") },
                { t: qsTr("TDR"), d: qsTr("Time-Domain Reflectometry: send a fast edge and time its reflection to compute cable length and locate faults. Needs PHY support; phone USB PHYs expose none, so length is not measurable here.") },
                { t: qsTr("Cable data on this device"), d: qsTr("The charger stack (qpnp-pdphy in the PMIC) exposes no cable node, so an e-marker's rating, length and speed cannot be read — Discover Identity/SOP′ is not surfaced. A mainline-tcpm kernel would show it. For real cable data, a dedicated USB-C PD analyzer / cable tester reads the e-marker independently of the phone.") },
                { t: qsTr("Open-ended cable"), d: qsTr("USB-C detects an attachment from the far end's CC resistors. A cable with nothing plugged into its other end is electrically invisible — the port reports no partner.") },
                { t: qsTr("Readable PD/Type-C state"), d: qsTr("What this device does expose — the CC current advertisement, PD/Type-C revision and VCONN — appears under Battery → Charging when a charger is attached.") }
            ]
        },
        {
            title: qsTr("Storage"), key: "storage",
            items: [
                { t: qsTr("UFS"), d: qsTr("Universal Flash Storage — the current phone storage standard. Full-duplex serial link, command queueing; faster than the older eMMC.") },
                { t: qsTr("SCSI / LUN"), d: qsTr("UFS speaks the SCSI command set. The chip presents several Logical Units (LUNs): one large user area plus small boot and RPMB units. The capacity shown is the user LUN.") },
                { t: qsTr("Raw vs usable capacity"), d: qsTr("Marketing capacity counts raw NAND in powers of ten (64 GB = 64·10⁹). The OS counts usable space in powers of two (GiB) after over-provisioning and metadata, so 64 GB shows as ~59.6 GiB.") },
                { t: qsTr("Over-provisioning"), d: qsTr("Spare NAND the controller keeps hidden for wear-levelling and bad-block replacement — part of why raw and usable differ.") },
                { t: qsTr("Wear / lifetime"), d: qsTr("UFS and eMMC report a health estimate (bDeviceLifeTimeEst) from the program/erase cycles used — not a percentage, but a step: 0x01 is 0–10 % used, 0x0A is 90–100 %, and 0x0B means the estimated lifetime is exceeded, with no upper figure attached. The percentage shown is the lower edge of the reported band; the exceeded step is named instead of converted.") },
                { t: qsTr("Spare blocks (pre-EOL)"), d: qsTr("A second, independent register: the controller keeps reserve blocks to replace worn ones, and reports whether under 80 %, 80 % or 90 % of them are consumed. It is the more telling of the two — if the lifetime estimate claims to be exhausted while the spare blocks still read normal, the chip contradicts itself and the estimate should not be trusted.") },
                { t: qsTr("Assessment — our reading"), d: qsTr("Not a value the chip reports but this app's summary of the two registers above. Urgent when the chip calls its lifetime exceeded or 90 % of the spare blocks consumed; warning at 80 % of spare blocks or from wear step 8 of 11, which is 70 % of the estimated endurance; good below that. The two registers are the better evidence — this line only saves reading them.") },
                { t: qsTr("Block / erase block"), d: qsTr("NAND is read/written in pages but erased in larger blocks. Logical blocks (sectors, usually 4 KiB) are the unit the filesystem addresses.") },
                { t: qsTr("RPMB"), d: qsTr("Replay-Protected Memory Block — a small authenticated LUN for anti-rollback and secure counters, not general storage.") },
                { t: qsTr("Data moved"), d: qsTr("Bytes read from and written to a storage device, counted by the kernel from the moment it registered that device — not from the start of the phone. For built-in storage the two are seconds apart; a memory card counts from when it was inserted, and starts again if it is taken out and put back. Only requests that reached the device are counted: anything served from the cache never appears, so the figure is lower than what programs asked for.") },
                { t: qsTr("Backup"), d: qsTr("Better to have it and not need it than to need it and not have it. A regular backup — a copy of your data on a second medium — protects you from losing what matters when storage fails, the phone goes missing or something is deleted by mistake. Flash gives no warning before it goes; the copy has to exist beforehand.") }
            ]
        },
        {
            title: qsTr("Since boot"), key: "sinceboot",
            items: [
                { t: qsTr("Uptime"), d: qsTr("Time since the last start, deep sleep included — the phone counts it even while suspended.") },
                { t: qsTr("Awake"), d: qsTr("The part of the uptime the system was really running. Two kernel clocks make it visible: one stops during suspend, the other keeps going, and the gap between them is sleep.") },
                { t: qsTr("Deep sleep"), d: qsTr("Suspend-to-RAM: the CPU is off, memory is kept alive, only a wakeup source can bring the phone back. This is where a phone spends most of its day, and where it saves almost all of its energy.") },
                { t: qsTr("Screen-on time"), d: qsTr("How long the display was up since the last start. MCE, the Sailfish power daemon, holds a wakelock for exactly that period and the kernel sums it — so the figure exists without anything recording it. It runs a few seconds over per switch-off, because the lock is released after the display is already dark.") },
                { t: qsTr("Wakelock"), d: qsTr("A request that keeps the system from suspending, held by a driver or a program for as long as it needs the phone awake. The kernel counts how often each one was taken and how long it was held.") },
                { t: qsTr("Wake source"), d: qsTr("Hardware or a driver that can end deep sleep: an incoming packet, the modem, a timer, a key. The count says how often it did, not whether it was justified.") },
                { t: qsTr("Suspend attempt"), d: qsTr("The kernel tries to go down whenever nothing holds it awake. Each try either succeeds or is abandoned — a wakeup arriving mid-attempt is enough to abort it, which is why failed attempts are ordinary and not a defect in themselves.") },
                { t: qsTr("Freezing tasks"), d: qsTr("First step of a suspend: all processes are halted at a safe point. If one refuses or a driver is still busy, the attempt stops right there and the step is recorded.") },
                { t: qsTr("EBUSY (-16)"), d: qsTr("The error a driver returns when it cannot be put to sleep at that moment because it is still working. It names the device that blocked the attempt.") }
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
