// Builders that turn the sysmon/bt getters into InfoDetailPage sections.
// Non-library JS: shares the importing component's scope (sysmon, bt, qsTr).

function row(k, v, opt) {
    var r = { k: k, v: (v === undefined || v === null || v === "") ? "—" : ("" + v) }
    if (opt) {
        if (opt.mono) r.mono = true
        if (opt.active === false) r.active = false
        if (opt.color) r.color = opt.color
        // Second value, pushed to the right edge: lets a list line up in two
        // columns instead of running the figures together in one string.
        if (opt.right !== undefined && opt.right !== "") r.vRight = "" + opt.right
    }
    return r
}

// Fold a group of sections away: they keep their place at the end of the page
// and cost one header line each until the reader opens them. Everything that
// is a full dump rather than a figure one needs for an overview goes through
// here — the page has to answer "what is this device" before it answers
// "what does the kernel export".
function folded(list) {
    for (var i = 0; i < list.length; ++i)
        list[i].collapsed = true
    return list
}

// A run of sections of the same kind — one directory per power-supply stage,
// one per sound card, one per raw node — is a list, not a chapter each. From
// four of them on they go under a single header and open together; three or
// fewer are quicker to read than to unpack.
function group(list, id, title, note) {
    if (list.length <= 3) return list
    var out = [{ groupHeader: id, title: title, count: list.length, note: note, rows: [] }]
    for (var i = 0; i < list.length; ++i) {
        list[i].collapsed = true
        list[i].group = id
        out.push(list[i])
    }
    return out
}

// The page's findings, placed where the builder wants them rather than after
// everything else.
function diagnosisHere() {
    return { diagnosis: true }
}

// Raw node dumps: whatever a subsystem exports, one section per directory.
// Deliberately the last thing on a page — the named facts first, the unlabelled
// remainder after them, for the reader who would rather see the file than our
// reading of it.
function rawSections(topic) {
    var groups = []
    try { groups = sysmon.rawNodes(topic) } catch (e) { return [] }
    var out = []
    for (var i = 0; i < groups.length; ++i) {
        var g = groups[i]
        var attrs = g.attrs || []
        var rows = []
        for (var j = 0; j < attrs.length; ++j) {
            var at = attrs[j]
            // Where the kernel's ABI fixes the unit, the reading takes the
            // value column and the figure the kernel wrote moves to the right.
            // Everything else stands exactly as it was written.
            rows.push(at.reading
                      ? row(at.name, at.reading, {mono:true, right: at.value})
                      : row(at.name, at.value, {mono:true}))
        }
        if (!rows.length) continue
        out.push({ title: g.title, rows: rows })
    }
    if (!out.length) return []
    var note = qsTr("Everything this subsystem exports, exactly as the kernel wrote it — the attributes this app has a name for and the ones it does not. Almost nothing below is converted: each value stands in whatever unit its driver chose, and that is not always the same unit from one node to the next. The exception is the handful of attributes whose unit the kernel's own interface fixes for every device — a module's section sizes are bytes, a thermal zone's temperature is millidegrees — and those are read out in words, with the figure the kernel wrote kept on the right. All of it is world-readable; none of it needs the root helper.")
    folded(out)
    if (out.length <= 3) {
        out[0].note = note
        return out
    }
    return group(out, "raw-" + topic, qsTr("Raw kernel nodes"), note)
}

// The device tree as a parts list. filter is a space-separated set of
// substrings matched against node name and compatible string; empty lists
// every node that names a chip at all.
function dtSections(filter, title) {
    var parts = []
    try { parts = sysmon.deviceTreeParts(filter || "") } catch (e) { return [] }
    if (!parts.length) return []
    var rows = []
    for (var i = 0; i < parts.length; ++i) {
        var p = parts[i]
        rows.push(row(p.node, p.compatible,
                      { mono: true, active: p.status === "disabled" ? false : undefined }))
    }
    return folded([{ title: title || qsTr("Device tree"),
        note: qsTr("Every device-tree node that names a chip, with the compatible string the kernel matched a driver against — the vendor's own name for the part. This is the board's parts list, and it names hardware no device class enumerates: the amplifiers on I2C, the fingerprint reader on SPI, the regulators inside each PMIC. Grayed entries are switched off in the board file: the silicon is in the SoC, this device does not wire it up."),
        rows: rows }])
}

// ---- firmware, kept on the subsystem's own page ---------------------------
// There is no single firmware version on a phone. There are dozens: the
// kernel, a module per silicon block, a blob per radio, and a controller with
// a release of its own in the storage, the charger, every USB device and the
// modem. Collecting them on one page would put the Wi-Fi firmware somewhere
// other than the Wi-Fi, so each lands beside the hardware it belongs to.

// A device release number as the USB specification writes it: two BCD bytes,
// which sysfs prints as four hex digits.
function bcd(v) {
    if (!v) return ""
    var t = ("" + v).trim()
    if (t.length === 4 && t.indexOf(".") < 0) return t.slice(0, 2) + "." + t.slice(2)
    return t
}

function fwModules(patterns, title) {
    var mods = []
    try { mods = sysmon.firmwareDetail("modules").modules || [] } catch (e) { return [] }
    var rows = []
    for (var i = 0; i < mods.length; ++i) {
        var mo = mods[i]
        var hit = false
        for (var p = 0; p < patterns.length; ++p)
            if (mo.name.toLowerCase().indexOf(patterns[p]) >= 0) { hit = true; break }
        if (!hit) continue
        var v = mo.version ? mo.version
              : mo.srcversion ? qsTr("build %1").arg(mo.srcversion)
              : qsTr("no version string")
        rows.push(row(mo.name, v, { mono: true, right: mo.taintWords,
                                    active: (mo.version || mo.srcversion) ? undefined : false }))
    }
    if (!rows.length) return []
    return folded([{ title: title || qsTr("Driver and firmware"),
        note: qsTr("The modules behind this hardware, with whatever version each one carries. A driver that names a release states it; one that does not is shown by the hash of the source it was built from, which still tells two builds apart. The note on the right is what the kernel knows about where the code came from — out-of-tree and unsigned are the normal case on a vendor kernel, not a fault."),
        rows: rows }])
}

function fwUsb() {
    var devs = []
    try { devs = sysmon.firmwareDetail("usb").usb || [] } catch (e) { return [] }
    if (!devs.length) return []
    var rows = []
    for (var i = 0; i < devs.length; ++i) {
        var d = devs[i]
        var name = ((d.manufacturer || "") + " " + (d.product || "")).trim()
        if (!name.length) name = d.vendorId + ":" + d.productId
        var bits = []
        if (d.release) bits.push(qsTr("firmware %1").arg(bcd(d.release)))
        if (d.usbVersion) bits.push("USB " + ("" + d.usbVersion).trim())
        if (d.speed) bits.push(d.speed + " Mbit/s")
        bits.push(d.vendorId + ":" + d.productId)
        rows.push(row(name, bits.join("  ·  "), { right: d.port }))
    }
    return folded([{ title: qsTr("Attached devices — firmware"),
        note: qsTr("Every USB device reports a release number of its own, and it is the closest thing such a device has to a firmware version. It is the manufacturer's number, not a date, so it only compares against another unit of the same product. The identifier at the end is the vendor and product pair the kernel matched a driver against."),
        rows: rows }])
}

function fwNet() {
    var ifs = []
    try { ifs = sysmon.firmwareDetail("net").interfaces || [] } catch (e) { return [] }
    if (!ifs.length) return []
    var rows = []
    for (var i = 0; i < ifs.length; ++i) {
        var n = ifs[i]
        rows.push(row(n.name, (n.driver || "?")
                  + (n.driverVersion ? "  " + n.driverVersion : "")
                  + (n.firmware ? "  ·  " + qsTr("firmware") + " " + n.firmware : "")
                  + (n.rom ? "  ·  ROM " + n.rom : ""),
                  { mono: true, right: n.bus }))
    }
    return folded([{ title: qsTr("Interface firmware"),
        note: qsTr("Asked of the driver directly, because no file carries it: the firmware string comes back from the device rather than from the kernel. An interface that answers with a driver name and nothing else has a driver that never implemented the question — the radio still runs firmware, it just does not say which."),
        rows: rows }])
}

function fwStorage() {
    var st = []
    try { st = sysmon.firmwareDetail("storage").storage || [] } catch (e) { return [] }
    if (!st.length) return []
    var rows = []
    for (var i = 0; i < st.length; ++i) {
        var d = st[i]
        rows.push(row(((d.vendor || "") + " " + (d.model || "")).trim() || d.name,
                      qsTr("firmware %1").arg(d.firmware)
                      + (d.hardware ? "  ·  " + qsTr("hardware %1").arg(d.hardware) : "")
                      + (d.date ? "  ·  " + d.date : ""),
                      { mono: true, right: d.name }))
    }
    return folded([{ title: qsTr("Controller firmware"),
        note: qsTr("The revision the storage controller reports for itself. It is the firmware running inside the chip, not the flash memory's condition — that is on the health card. A UFS or eMMC part is a small computer with its own release, and this is its version."),
        rows: rows }])
}

// Catalogue figures for the device itself, kept beside the hardware they
// describe rather than gathered on a page of their own.
function catSections(part) {
    var c = {}
    try { c = sysmon.deviceCatalogue(part) } catch (e) { return [] }
    var rs = c.rows || []
    if (!rs.length) return []
    var rows = []
    for (var i = 0; i < rs.length; ++i) {
        // Short on purpose: this column is narrow, and a long phrase runs
        // over the value beside it.
        var src = rs[i].src === "the maker" ? qsTr("maker")
                : rs[i].src === "observed" ? qsTr("observed")
                : rs[i].src
        rows.push(row(rs[i].k, rs[i].v, { right: src }))
    }
    var out = folded([{ title: qsTr("%1 — catalogue").arg(c.device),
        note: qsTr("Not measured. Parts of a phone that the kernel never names — the camera sensors sit behind a vendor layer this system does not run, the panel reports a resolution and not a part number, the memory package is a board decision no SoC datasheet knows. Where the maker of the device has stated such a figure it is carried here, with the source on the right, and where nobody published one the row is absent rather than guessed."),
        rows: rows }])
    // Two numbers for the same cell, and they disagree. Better to say so than
    // to let a reader find the gap and assume this app got one of them wrong.
    if (c.batteryRatedMah && c.batteryDesignMah
            && Math.abs(c.batteryRatedMah - c.batteryDesignMah) > 100)
        out[0].note += "\n\n" + qsTr("The cell is sold as %1 mAh and the gauge reports %2 mAh as its design capacity. Both figures are on this page and they cannot both describe the same cell. Nothing here resolves it: the gauge is reading its own battery profile, which is written by the vendor and can be wrong, and the marketed figure is not a measurement either. Treat the health percentage above with that in mind — it divides one of these numbers by the other kind.")
                    .arg(c.batteryRatedMah.toFixed(0)).arg(c.batteryDesignMah.toFixed(0))
    return out
}

function cpu() {
    var d = sysmon.cpuDetail()
    var s = []
    // Everything that answers a question one does not ask while getting an
    // overview is collected separately and folded away at the end of the page,
    // in the order at the bottom of this function.
    var socCat = [], halSec = [], bootSec = [], inputSec = [], blobSec = []
    // The catalogue knows the name the device is sold under, which the
    // adaptation file does not always carry.
    var devcat = {}
    try { devcat = sysmon.deviceCatalogue("body") } catch (eDc) { devcat = {} }
    s.push({ title: qsTr("Device"), rows: [
        row(qsTr("Product"), devcat.device
            ? devcat.device + (d.deviceName && d.deviceName !== devcat.device
                               ? "   (" + d.deviceName + ")" : "")
            : d.deviceName),
        row(qsTr("Model"), d.deviceModel
            + (d.deviceVendor ? "  ·  " + d.deviceVendor : ""), {mono:true}),
        row(qsTr("Board"), d.machine),
        row("SoC", d.socModel || d.socName, {mono:true}),
        row(qsTr("SoC (device tree)"), d.socCompatible, {mono:true})
    ]})
    var soc = {}
    try { soc = sysmon.socCatalogue() } catch (eSoc) { soc = {} }
    if (soc.rows && soc.rows.length) {
        var socRows = []
        for (var sq = 0; sq < soc.rows.length; ++sq) {
            var sp = soc.rows[sq]
            var src = sp.src === "vendor" ? qsTr("published by the chip vendor")
                    : sp.src === "third party" ? qsTr("third party")
                    : sp.src === "this device" ? qsTr("read from this device")
                    : qsTr("not published")
            socRows.push(row(sp.k, sp.v && sp.v.length ? sp.v : qsTr("not published"),
                             { right: src,
                               active: sp.v && sp.v.length ? undefined : false,
                               color: sp.src === "this device" ? "#8ef94a" : undefined }))
        }
        socCat.push({ title: qsTr("%1 — catalogue").arg(soc.name), collapsed: true,
                 note: qsTr("Not measured. These are the figures the chip vendor published for this part, carried in the app because the kernel does not hold them: the device tree names the SoC and stops. The right-hand column says who published each one — the vendor's own product page, a third party, or, for the last row, this device itself. Where the vendor published nothing, the row says so rather than borrowing a number from a spec database.")
                     + (soc.note ? "\n\n" + soc.note : ""),
                 rows: socRows })
    }
    s.push({ title: qsTr("Operating system"), rows: [
        row("Sailfish OS", d.os),
        row(qsTr("Release"), d.osVersion),
        row(qsTr("HW adaptation"), d.hwVersion),
        row(qsTr("Kernel"), d.kernel, {mono:true}),
        row(qsTr("Kernel build"), d.kernelVersion, {mono:true})
    ]})
    if (d.androidVersion || d.androidPatch) {
        var ar = [
            row(qsTr("Android version"), d.androidVersion),
            row(qsTr("Security patch level"), d.androidPatch, {mono:true}),
            row(qsTr("Vendor build"), d.androidBuild, {mono:true})
        ]
        if (d.androidFingerprint)
            ar.push(row(qsTr("Fingerprint"), d.androidFingerprint, {mono:true}))
        s.push({ title: qsTr("Android base"),
                 note: qsTr("The Android layer under libhybris — kernel and HAL blobs come from this base. The patch level dates the vendor's last security fixes."),
                 rows: ar })
    }
    var coreRows = []
    var cores = d.cores || []
    for (var i = 0; i < cores.length; ++i) {
        var mhz = (sysmon.coreFreqsMhz[i] > 0) ? "  ·  " + sysmon.coreFreqsMhz[i] + " MHz" : ""
        coreRows.push(row(qsTr("Core %1").arg(i), (cores[i].name || cores[i].part || "?") + mhz))
    }
    s.push({ title: qsTr("Processor"), rows: [
        row(qsTr("Architecture"), "ARMv" + d.architecture),
        row(qsTr("Cores"), d.count),
        row("Hardware", d.hardware || d.machine || d.socName)
    ].concat(coreRows)})

    // governors: active highlighted, available-but-unused grayed
    var govs = d.availGovernors || []
    if (govs.length) {
        var gr = []
        for (var g = 0; g < govs.length; ++g)
            gr.push(row(govs[g], govs[g] === d.governor ? qsTr("active") : qsTr("available"),
                        { active: govs[g] === d.governor }))
        s.push({ title: qsTr("CPU governor"),
                 note: qsTr("The scaling strategy in use; grayed ones the kernel supports but does not use."),
                 rows: gr })
    }
    var fr = d.availFreqsMhz || []
    if (fr.length) {
        var frRows = []
        for (var fq = 0; fq < fr.length; ++fq)
            frRows.push(row(qsTr("Step %1").arg(fq + 1), fr[fq] + " MHz",
                            { right: sysmon.coreFreqsMhz[0] === fr[fq] ? qsTr("now") : "" }))
        s.push({ title: qsTr("Frequency steps"), collapsed: true,
                 note: qsTr("Every frequency the scaling driver offers for the policy the first core belongs to. Other clusters keep lists of their own, which this node does not carry, and the step marked on the right is the one that core happens to sit on right now."),
                 rows: frRows })
    }

    var caches = d.caches || []
    if (caches.length) {
        var cr = []
        for (var c = 0; c < caches.length; ++c)
            cr.push(row("L" + caches[c].level + " " + caches[c].type, caches[c].size))
        s.push({ title: qsTr("Caches"), rows: cr })
    }
    if (d.features) {
        var feats = d.features.split(" ")
        var frows = []
        for (var k = 0; k < feats.length; ++k)
            if (feats[k].length) frows.push(row(feats[k], qsTr("supported")))
        s.push({ title: qsTr("CPU features"),
                 note: qsTr("Instruction-set capabilities the CPU reports."), rows: frows })
    }
    // The adaptation's registered Android services. Asked for only when the
    // reader opens the section: the listing runs an external tool once per
    // binder domain. Which domain carries the HALs depends on the age of the
    // Android base, so the section names all of them and says who serves each.
    if (sysmon.halBinderPresent()) {
        halSec.push({ title: qsTr("Android HAL services"), collapsed: true,
                 note: qsTr("The services the hardware adaptation registers on the Android binder — camera, sensors, graphics, audio. Which door they sit behind depends on the age of the Android base: an older port registers them as HIDL on /dev/hwbinder, a newer one as AIDL on /dev/binder, because Android 13 deprecated HIDL and 14 dropped its service manager. The first rows say which doors this device has and who is answering behind them. None of it depends on Android App Support — these belong to the adaptation and are there whether a container is installed or not."),
                 rowsFn: function () {
                     var hal = sysmon.halServices()
                     var doms = hal.domains || [], svcs = hal.services || []
                     var hr = []
                     for (var i = 0; i < doms.length; ++i) {
                         var dm = doms[i]
                         var state = !dm.running
                                 ? qsTr("no service manager — %1 is not running").arg(dm.manager)
                                 : dm.timedOut
                                 ? qsTr("service manager runs, but the listing did not answer")
                                 : qsTr("%n service(s)", "", dm.count)
                         // Not grayed: a grayed row is tagged "unused", and a
                         // domain nobody serves is not an unused capability of
                         // this device — it is a door the platform no longer
                         // has. The text says which it is.
                         hr.push(row(dm.path, state, { right: dm.protocol }))
                     }
                     for (var j = 0; j < svcs.length; ++j) {
                         // HIDL writes interface::name, AIDL writes
                         // interface/instance; both split into a name and the
                         // instance behind it.
                         var t = "" + svcs[j].name, k = t, v = ""
                         if (t.indexOf("::") >= 0) {
                             k = t.split("::")[0]
                             v = t.split("::")[1]
                         } else if (t.lastIndexOf("/") > 0) {
                             k = t.slice(0, t.lastIndexOf("/"))
                             v = t.slice(t.lastIndexOf("/") + 1)
                         }
                         hr.push(row(k, v, { mono: true, right: svcs[j].protocol }))
                     }
                     return hr
                 } })
    }

    // Copy-ready device block for bug reports. Deliberately English literals:
    // reports go to international trackers.
    var rep = "Date: " + Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm") + "\n"
        + "Device: " + (d.deviceName || "?") + (d.deviceModel ? " (" + d.deviceModel + ")" : "") + "\n"
        + "OS: " + (d.os || "Sailfish OS") + " " + (d.osVersion || "") + "\n"
        + "HW adaptation: " + (d.hwVersion || "?") + "\n"
        + "Kernel: " + (d.kernel || "?") + "\n"
        + (d.kernelVersion ? "Kernel build: " + d.kernelVersion + "\n" : "")
        + (d.socCompatible ? "SoC: " + d.socCompatible + "\n" : "")
        + "Arch: ARMv" + (d.architecture || "?") + ", " + (d.count || "?") + " cores\n"
        + (d.androidVersion ? "Android base: " + d.androidVersion
            + (d.androidPatch ? ", security patch " + d.androidPatch : "")
            + (d.androidBuild ? ", build " + d.androidBuild : "") + "\n" : "")
        + "Uptime: " + sysmon.fmtDuration(sysmon.uptimeSec) + "\n"
        + "\n--- fill in yourself ---\n"
        + "Affected app + exact version (rpm -q <package>): \n"
        + "Steps to reproduce: \n"
        + "Expected vs. actual behavior: \n"
        + "Frequency (always/sometimes) + since when/which update: \n"
        + "Exact error message (verbatim): \n"
        + "Logs around the event (journalctl/app log/dmesg): \n"
        + "Already tried: "
    // --- where the CPU time went since boot ------------------------------
    // Belongs to the processor, not to a general balance: it is this chip's
    // own bookkeeping, and it reads against the load figures above.
    var acc = sysmon.sinceBootDetail()
    if (acc.cpu && acc.cpu.total > 0) {
        var c = acc.cpu, cr = []
        var cnames = [["idle", qsTr("Idle")], ["user", qsTr("User programs")],
                      ["system", qsTr("Kernel")], ["iowait", qsTr("Waiting for storage")],
                      ["irq", qsTr("Interrupts")], ["softirq", qsTr("Soft interrupts")],
                      ["nice", qsTr("Background (nice)")], ["steal", qsTr("Stolen")]]
        for (var n = 0; n < cnames.length; ++n) {
            var val = c[cnames[n][0]]
            if (val === undefined || val <= 0) continue
            cr.push(row(cnames[n][1], sysmon.fmtDuration(Math.round(val))
                        + "  \u00b7  " + Math.round(100 * val / c.total) + " %"))
        }
        if (cr.length) {
            s.push({ title: qsTr("Where the CPU time went"), rows: cr })
            s.push({ note: qsTr("Counted only while a core was actually running: parked cores and deep sleep stop the bookkeeping. On a phone that keeps the sum well below the uptime even though it covers every core. Idle dominating is the healthy case."),
                     rows: [] })
        }
    }

    // ---- what the system was booted as, and how hard it is to attack ----

    var sysfw = {}
    try { sysfw = sysmon.firmwareDetail("system") } catch (eF) { sysfw = {} }

    var bootRows = []
    var bl = sysfw.boot || []
    for (var bi = 0; bi < bl.length; ++bi)
        bootRows.push(row(bl[bi].key, bl[bi].value, {mono:true}))
    if (bootRows.length)
        bootSec.push({ title: qsTr("Boot"), collapsed: true,
                 note: qsTr("What the bootloader recorded on the kernel command line about itself and about what it verified. The verified-boot state says whether the chain of signatures held from the boot ROM up to this system; a device with an unlocked bootloader reports so here, which is a fact about the device, not a fault."),
                 rows: bootRows })

    var hard = sysfw.hardening || []
    if (hard.length || sysfw.lockdown || sysfw.selinux) {
        var hr = []
        for (var hi = 0; hi < hard.length; ++hi) {
            var sw = hard[hi]
            // Three states, not two: weaker than recommended, at least as
            // strict, or a figure with no recommendation attached at all —
            // and that last one must not be painted as though it were good.
            var rated = sw.weaker !== undefined
            hr.push(row(sw.label,
                        sw.value + (rated && sw.weaker ? "  ·  " + qsTr("safer would be %1").arg(sw.safe) : ""),
                        { mono: true, right: sw.path,
                          color: !rated ? undefined : sw.weaker ? "#ffb44a" : "#8ef94a" }))
        }
        if (sysfw.lockdown) hr.push(row(qsTr("Kernel lockdown"), sysfw.lockdown, {mono:true}))
        if (sysfw.selinux) hr.push(row("SELinux", sysfw.selinux,
                                       {color: sysfw.selinux === "enforcing" ? "#8ef94a" : "#ffb44a"}))
        if (sysfw.tainted && sysfw.tainted !== "0")
            hr.push(row(qsTr("Kernel taint"), sysfw.tainted, {mono:true}))
        s.push({ title: qsTr("How exposed this kernel is"),
                 note: qsTr("Switches an ordinary process may read, each deciding whether a whole class of local attack is available at all. They are shown as they stand, with the safer setting named where there is one. This is not a verdict: a phone distribution turns several of them down on purpose so that ordinary tools keep working, and a value in amber means worth knowing, not broken."),
                 rows: hr })
    }

    var inp = []
    try { inp = sysmon.firmwareDetail("input").input || [] } catch (eI) { inp = [] }
    if (inp.length) {
        var ir = []
        for (var ii = 0; ii < inp.length; ++ii) {
            var d3 = inp[ii]
            ir.push(row(d3.name, qsTr("bus %1  ·  vendor %2  ·  product %3  ·  version %4")
                        .arg(d3.bus).arg(d3.vendor).arg(d3.product).arg(d3.version), {mono:true}))
        }
        inputSec.push({ title: qsTr("Input hardware"), collapsed: true,
                 note: qsTr("The touch controller, the buttons, the fingerprint reader and anything else that reports events. For several of them this is the only place the device is versioned at all — the vendor, product and version numbers come from the hardware itself, through the input core."),
                 rows: ir })
    }

    var blobs = {}
    try { blobs = sysmon.firmwareDetail("blobs") } catch (eB) { blobs = {} }
    var bfiles = blobs.blobs || []
    if (bfiles.length) {
        var br = []
        for (var fi2 = 0; fi2 < bfiles.length; ++fi2)
            br.push(row(bfiles[fi2].name,
                        sysmon.fmtBytes(bfiles[fi2].bytes) + "  ·  " + bfiles[fi2].date.slice(0, 10),
                        {mono:true, right: bfiles[fi2].root.replace("/lib/", "").replace("/vendor/", "v/")}))
        blobSec.push({ title: qsTr("Firmware files on disk"), collapsed: true,
                 note: qsTr("%1 files, %2 in total — the images the kernel loads into a radio, a DSP or a sensor when it starts them. The names carry the chip family, and the dates say when the vendor last touched them. This is what is available to load, not proof that any of it was loaded.")
                        .arg(blobs.blobCount).arg(sysmon.fmtBytes(blobs.blobBytes)),
                 rows: br })
    }

    // The kernel's exposure reads directly against the findings below it —
    // the diagnosis names the consequence of what this list states, so the two
    // belong together rather than at opposite ends of the page.
    s.push(diagnosisHere())

    // The detail, in the order a reader would go looking for it: what the
    // adaptation runs, what the parts are on paper, what is on the disk, what
    // the boot chain and the peripherals say, and the unlabelled remainder.
    var tail = halSec.concat(socCat)
        .concat(catSections("body"))
        .concat(blobSec)
        .concat(fwModules([""], qsTr("Every module and its version")))
        .concat(bootSec)
        .concat(inputSec)
        .concat(dtSections("", qsTr("Device tree — every part")))
        .concat(rawSections("device"))
        .concat(rawSections("cpu"))

    return { title: qsTr("System & CPU"), helpTopics: ["cpu","diagnosis","monitoring","raw","firmware"], sections: s.concat(tail), diagTopic: "cpu", report: rep }
}

function gfx() {
    var d = sysmon.graphicsDetail()
    var s = []
    s.push({ title: qsTr("GPU"), rows: [
        row(qsTr("Model"), d.gpuModel),
        row(qsTr("Driver"), d.gpuDriver || d.driver),
        row(qsTr("Driver release"), d.gpuDriverRelease
            ? d.gpuDriverRelease + (d.gpuDriverModule ? "  ·  " + d.gpuDriverModule : "")
            : qsTr("not exposed by this driver"),
            { mono: true, active: d.gpuDriverRelease ? undefined : false }),
        row(qsTr("Clock"), (d.gpuCurMhz ? d.gpuCurMhz + " MHz" : "—") + (d.gpuMaxMhz ? " / " + d.gpuMaxMhz + " MHz" : "")),
        row(qsTr("Busy"), d.gpuBusy !== undefined ? d.gpuBusy + " %" : "—")
    ]})
    var disp = d.displays || []
    if (disp.length) {
        var dr = []
        for (var i = 0; i < disp.length; ++i) {
            dr.push(row(qsTr("Connector"), disp[i].connector))
            dr.push(row(qsTr("Resolution"), disp[i].resolution))
            dr.push(row(qsTr("Status"), disp[i].status))
        }
        s.push({ title: qsTr("Display"), rows: dr })
    } else {
        s.push({ title: qsTr("Display"), note: qsTr("No connected DRM connector exposed by the kernel."), rows: [] })
    }
    s.push(diagnosisHere())
    return { title: qsTr("Graphics"), helpTopics: ["raw","firmware"], sections: s.concat(catSections("display")).concat(fwModules(["mali","gpufreq","ged","drm","kgsl","disp"])).concat(dtSections("gpu mali display dsi panel drm", qsTr("Device tree — graphics"))).concat(rawSections("gfx")), diagTopic: "gpu" }
}

function mem() {
    var d = sysmon.memoryDetail()
    var rows = []
    var mrows = d.rows || []
    for (var i = 0; i < mrows.length; ++i)
        rows.push(row(mrows[i].key, sysmon.fmtBytes(mrows[i].bytes)))
    var used = sysmon.memTotal - sysmon.memAvailable
    var sections = [
        { title: qsTr("Summary"),
          bars: [ { label: qsTr("Used"), value: used, max: sysmon.memTotal,
                    caption: sysmon.fmtBytes(used) + " / " + sysmon.fmtBytes(sysmon.memTotal) } ],
          rows: [
            row(qsTr("Total"), sysmon.fmtBytes(sysmon.memTotal)),
            row(qsTr("Available"), sysmon.fmtBytes(sysmon.memAvailable)),
            row(qsTr("Cached"), sysmon.fmtBytes(sysmon.cached)),
            row(qsTr("Buffers"), sysmon.fmtBytes(sysmon.buffers)),
            row(qsTr("Swap used"), sysmon.fmtBytes(sysmon.swapUsed) + " / " + sysmon.fmtBytes(sysmon.swapTotal))
          ]}
    ]

    // memory device: what is and isn't exposed for the DRAM itself
    var devRows = []
    if (d.ddrType)
        devRows.push(row(qsTr("Type"), d.ddrType + (d.ddrTypeCode !== undefined ? "  (code " + d.ddrTypeCode + ")" : ""), {mono:true}))
    else if (d.ddrTypeCode !== undefined)
        devRows.push(row(qsTr("Type"), qsTr("DDR code %1 (unmapped)").arg(d.ddrTypeCode), {mono:true}))
    // The DRAM controller, where there is one, names the grade outright — no
    // datasheet, no inference from a governor ceiling.
    if (d.dramType)
        devRows.push(row(qsTr("Grade (memory controller)"), d.dramType, {mono:true, color:"#8ef94a"}))
    // The live rate and the ceiling belong together: the governor drops the
    // memory to a low step when nothing is asking, and an idle figure on its
    // own reads like the rating.
    if (d.dramRate)
        devRows.push(row(qsTr("Data rate now"), d.dramRate.toFixed(0) + " MT/s"
                         + (d.dramRateMax ? "   " + qsTr("of %1 MT/s").arg(d.dramRateMax.toFixed(0)) : "")))
    else if (d.dramRateMax)
        devRows.push(row(qsTr("Data rate ceiling"), d.dramRateMax.toFixed(0) + " MT/s"))
    if (d.dramModeRegisters)
        devRows.push(row(qsTr("Mode registers"), d.dramModeRegisters, {mono:true}))
    devRows.push(row(qsTr("Manufacturer"), qsTr("not exposed — JEDEC MR5, read by the bootloader into SMEM, not surfaced here"), {active:false}))
    devRows.push(row(qsTr("Organisation (ranks / channels / dies)"), qsTr("not exposed — a JEDEC/datasheet property of the die (MR5–MR8), not a runtime register here"), {active:false}))
    sections.push({ title: qsTr("Memory device"),
        note: qsTr("The DRAM type is read from the bootloader-populated device tree. The chip's maker and internal organisation are not exposed to software on this platform."),
        rows: devRows })

    // physical memory map (address regions the kernel sees — not the die layout)
    // and the unabridged meminfo: both are lists to look something up in, so
    // they wait at the end of the page rather than in front of the figures.
    var memDumps = []
    var regs = d.regions || []
    if (regs.length) {
        var rrows = []
        for (var r = 0; r < regs.length; ++r)
            rrows.push(row("0x" + regs[r].base.toString(16), sysmon.fmtBytes(regs[r].size), {mono:true}))
        memDumps.push({ title: qsTr("Physical memory map"), collapsed: true,
            note: qsTr("The address regions the kernel maps, carved around reserved firmware areas — this is the address layout, not the chip's rank/channel structure."),
            rows: rrows })
    }

    memDumps.push({ title: qsTr("meminfo (full)"), collapsed: true, rows: rows })
    // --- memory pressure since boot --------------------------------------
    // The running cost of a full RAM, so it sits with the RAM figures rather
    // than in a balance of its own.
    var vacc = sysmon.sinceBootDetail()
    if (vacc.vm) {
        var vr = []
        if (vacc.vm.pgmajfault > 0) vr.push(row(qsTr("Major page faults"), Math.round(vacc.vm.pgmajfault)))
        if (vacc.vm.pswpin > 0) vr.push(row(qsTr("Swapped back in"), sysmon.fmtBytes(vacc.vm.pswpin * 4096)))
        if (vacc.vm.pswpout > 0) vr.push(row(qsTr("Swapped out"), sysmon.fmtBytes(vacc.vm.pswpout * 4096)))
        if (vacc.vm.oom_kill !== undefined)
            vr.push(row(qsTr("Killed for memory"), Math.round(vacc.vm.oom_kill),
                        {color: vacc.vm.oom_kill > 0 ? "#ffb44a" : undefined}))
        if (vr.length) {
            sections.push({ title: qsTr("Memory pressure since boot"), rows: vr })
            sections.push({ note: qsTr("Totals since the last start, not a current reading: the kernel counts these up and only a restart puts them back to zero. Swap traffic and major faults are the price of a full RAM — the system had to fetch pages back from storage. Growing slowly is normal; a kill for memory means a process was ended to keep the system alive."),
                     rows: [] })
        }
    }

    return { title: qsTr("RAM"), helpTopics: ["mem","raw","firmware"], sections: sections.concat(memDumps).concat(catSections("memory")).concat(fwModules(["dram","emi","dvfsrc","zram"])).concat(dtSections("dram emi memory", qsTr("Device tree — memory"))).concat(rawSections("mem")) }
}

// Wear rows, shared by the UFS and the eMMC/card block. JEDEC step 0x0B is an
// overflow, not a band -- the chip says "past my estimate" and gives no upper
// figure, so it is named rather than turned into a percentage. Pre-EOL is the
// second, independent register: when it still reads normal while the lifetime
// estimate claims to be exhausted, the chip contradicts itself and the estimate
// is not worth much.
function wearRows(h) {
    var out = []
    if (!h.healthVerdict) {
        out.push(row(qsTr("Assessment"), qsTr("not reported by device")))
        return out
    }
    var vtxt = h.healthVerdict === "good" ? qsTr("good")
             : h.healthVerdict === "warning" ? qsTr("warning") : qsTr("urgent")
    var vcol = h.healthVerdict === "good" ? "#31e0a0"
             : h.healthVerdict === "warning" ? "#ffb44a" : "#ff5a52"
    out.push(row(qsTr("Wear"),
                 h.lifeExceeded === true ? qsTr("estimated lifetime exceeded")
                                         : qsTr("~%1 % life used").arg(h.lifeUsedPct),
                 h.lifeExceeded === true ? {color: "#ff5a52"} : undefined))
    if (h.preEol > 0)
        out.push(row(qsTr("Spare blocks"),
                     h.preEol === 1 ? qsTr("normal, under 80 % used")
                   : h.preEol === 2 ? qsTr("80 % used")
                                    : qsTr("90 % used"),
                     {color: h.preEol >= 3 ? "#ff5a52" : h.preEol === 2 ? "#ffb44a" : undefined}))
    // Our reading of the two registers above, not a value the chip reports --
    // the thresholds behind it are in the glossary.
    out.push(row(qsTr("Assessment"), vtxt, {color: vcol}))
    return out
}

function storage() {
    var hw = sysmon.storageHardware()
    var mounts = sysmon.storageMounts()
    var s = []

    // split UFS LUNs from removable/eMMC
    var ufs = [], other = []
    for (var i = 0; i < hw.length; ++i)
        ((("" + hw[i].bus).indexOf("UFS") >= 0) ? ufs : other).push(hw[i])

    // UFS: main user LUN carries the descriptors; pick the largest
    if (ufs.length) {
        var main = ufs[0]
        for (var u = 1; u < ufs.length; ++u) if (ufs[u].size > main.size) main = ufs[u]
        var mrows = [
            row(qsTr("Bus"), main.ufsSpec ? ("UFS " + main.ufsSpec) : main.bus),
            row(qsTr("Vendor"), main.vendor),
            row(qsTr("Model"), main.model, {mono:true}),
            row(qsTr("Revision"), main.rev),
            row(qsTr("Serial"), main.serial, {mono:true})
        ]
        if (main.mfrId) mrows.push(row(qsTr("Manufacturer ID"), main.mfrId, {mono:true}))
        if (main.writeBooster !== undefined) mrows.push(row("WriteBooster", main.writeBooster ? qsTr("supported (SLC cache)") : qsTr("no")))
        if (main.queueDepth) mrows.push(row(qsTr("Queue depth"), main.queueDepth))
        mrows = mrows.concat(wearRows(main))
        s.push({ title: qsTr("Internal storage (UFS)"), rows: mrows })
        if (main.lifeExceeded === true && main.preEol === 1)
            s.push({ note: qsTr("The chip reports its estimated lifetime as exceeded while its spare blocks still read normal. The two registers contradict each other, so the lifetime estimate is unreliable on this device — the spare-block figure is the one to watch."),
                     rows: [] })

        // capacity composition: how the package presents itself
        var crows = []
        var tot = 0
        for (var v = 0; v < ufs.length; ++v) {
            var L = ufs[v]
            tot += L.size
            var big = L.size > 1024 * 1024 * 1024
            crows.push(row(L.dev + (big ? qsTr(" — user area") : qsTr(" — boot LUN")),
                           sysmon.fmtBytes(L.size), {mono:true, active: big}))
        }
        var extra = []
        if (main.numWluns) extra.push(qsTr("%1 well-known LUNs (boot, RPMB, device)").arg(main.numWluns))
        s.push({ title: qsTr("Capacity composition"), collapsed: true,
            note: qsTr("A single UFS package, not multiple cards. The controller presents it as %1 data LUN(s): one large user area plus tiny boot LUNs, plus %2. There is no software-visible “2×64” die split — the flash dies sit behind the controller.")
                    .arg(main.numLuns || ufs.length).arg(main.numWluns ? qsTr("well-known LUNs (RPMB etc.)") : qsTr("well-known LUNs")),
            rows: crows })

        s.push({ title: qsTr("Raw vs usable"), collapsed: true,
            note: qsTr("The size shown is the usable user LUN in GiB (powers of two). The advertised capacity counts raw NAND in GB (powers of ten) and includes over-provisioning kept hidden by the controller — which is why e.g. 128 GB shows as ~119 GiB."),
            rows: [] })
    }

    // eMMC / SD card
    for (var o = 0; o < other.length; ++o) {
        var h = other[o]
        var rows = [
            row(qsTr("Bus"), h.bus),
            row(qsTr("Vendor"), h.vendor),
            row(qsTr("Model"), h.model, {mono:true}),
            row(qsTr("Revision"), h.rev),
            row(qsTr("Serial"), h.serial, {mono:true}),
            row(qsTr("Size"), sysmon.fmtBytes(h.size))
        ]
        if (h.date) rows.push(row(qsTr("Mfg date"), h.date))
        rows = rows.concat(wearRows(h))
        if (h.hostNode || h.hostDriver)
            rows.push(row(qsTr("Card reader"), (h.hostDriver || "") + (h.hostNode ? "  ·  " + h.hostNode : ""), {mono:true}))
        var busU = ("" + h.bus).toUpperCase()
        var title = busU.indexOf("SD") >= 0 ? qsTr("microSD card")
                  : busU.indexOf("MMC") >= 0 ? qsTr("Internal storage (eMMC)")
                  : (h.bus + " " + h.dev)
        s.push({ title: title, rows: rows })
    }

    // bytes actually moved to and from the device since boot -- the wear figures
    // above are an estimate, this is the traffic that produced them
    var moved = sysmon.sinceBootDetail()
    if (moved.disks && moved.disks.length) {
        var mv = []
        for (var dk = 0; dk < moved.disks.length; ++dk) {
            mv.push(row(qsTr("%1 read").arg(moved.disks[dk].name), sysmon.fmtBytes(moved.disks[dk].readBytes)))
            mv.push(row(qsTr("%1 written").arg(moved.disks[dk].name), sysmon.fmtBytes(moved.disks[dk].writeBytes)))
        }
        s.push({ title: qsTr("Data moved"), rows: mv })
        s.push({ note: qsTr("Counted from the moment the kernel registered the device: for built-in storage that is seconds after the start, for a memory card the moment it was inserted. Only traffic that reached the device is counted, not cache hits — and nothing of what the flash saw before this start."),
                 rows: [] })
    }

    s.push({ note: qsTr("Got a backup? You know what the admins say: no backup, no mercy! Make one if you don't have one yet — and keep it current."),
             italic: true, rows: [] })

    var bars = []
    for (var m = 0; m < mounts.length; ++m) {
        var mn = mounts[m]
        bars.push({ label: mn.mount + "  (" + mn.fstype + ")", value: mn.used, max: mn.total,
                    caption: sysmon.fmtBytes(mn.used) + " / " + sysmon.fmtBytes(mn.total),
                    color: mn.pct > 90 ? "#ff5a52" : mn.pct > 75 ? "#ffb44a" : "#31e0a0" })
    }
    s.push({ title: qsTr("Partitions"), bars: bars, rows: [] })
    return { title: qsTr("Storage"), helpTopics: ["storage","raw","firmware"], sections: s.concat(catSections("storage")).concat(fwStorage()).concat(fwModules(["ufs","mmc","blocktag","scsi"])).concat(dtSections("ufs mmc sdhci storage", qsTr("Device tree — storage"))).concat(rawSections("storage")) }
}

// The counters below start with the interface, not with the phone. Name the
// span they cover, and say when it coincides with the system start.
function countedSince(n) {
    if (n.countingSec === undefined) return ""
    var since = sysmon.fmtDuration(Math.round(n.countingSec))
    // Interfaces created with the system are a couple of seconds younger than
    // the uptime; anything later came up with its connection.
    return (sysmon.uptimeSec - n.countingSec) < 120
           ? since + "  ·  " + qsTr("since system start")
           : since + "  ·  " + qsTr("since this interface came up")
}

function net() {
    var nics = sysmon.networkHardware()
    var wifi = sysmon.wifiDetail()
    nics.sort(function(a, b) {
        function rank(n) {
            if (n.kind === "wifi") return 0
            if (n.iface === "lo") return 9
            if (n.state === "up") return 1
            return 2
        }
        return rank(a) - rank(b)
    })
    var s = []

    // The address is not in sysfs, so it arrives with the interface list; find
    // the entry belonging to the Wi-Fi interface to fill the connection block.
    function nicFor(name) {
        for (var q = 0; q < nics.length; ++q)
            if (nics[q].iface === name) return nics[q]
        return {}
    }

    if (wifi.iface !== undefined) {
        if (wifi.connected) {
            var wn = nicFor(wifi.iface)
            var wrows = [
                row(qsTr("SSID"), wifi.ssid),
                row(qsTr("IP address"), wn.ipv4),
                row(qsTr("Own MAC"), wifi.mac, {mono:true}),
                row(qsTr("Access point (BSSID)"), wifi.bssid, {mono:true}),
                row(qsTr("Band"), wifi.band),
                row(qsTr("Channel"), (wifi.channel ? wifi.channel : "?") + (wifi.freqMhz ? "  (" + wifi.freqMhz + " MHz)" : "")),
                row(qsTr("Signal"), wifi.signalDbm !== undefined ? wifi.signalDbm + " dBm" : "—"),
                row(qsTr("TX rate"), wifi.txBitrate),
                row(qsTr("RX rate"), wifi.rxBitrate),
                row(qsTr("TX power"), wifi.txpower)
            ]
            if (wn.rxBytes !== undefined) {
                wrows.push(row(qsTr("Download / Upload"),
                               sysmon.fmtBytes(wn.rxBytes) + "  /  " + sysmon.fmtBytes(wn.txBytes)))
                if (countedSince(wn)) wrows.push(row(qsTr("Counted since"), countedSince(wn)))
            }
            if (wn.ipv6) wrows.push(row(qsTr("IPv6"), wn.ipv6, {mono:true}))
            s.push({ title: qsTr("Wi-Fi connection"), rows: wrows })
        } else {
            s.push({ title: qsTr("Wi-Fi connection"), rows: [ row(qsTr("Status"), qsTr("not connected")) ] })
        }
        s.push({ title: qsTr("Wi-Fi adapter"), rows: [
            row(qsTr("Interface"), wifi.iface),
            row(qsTr("Chip vendor"), wifi.vendor),
            row(qsTr("Driver"), wifi.driver, {mono:true}),
            row("PHY", wifi.phy)
        ]})
        var bands = wifi.bands || []
        for (var b = 0; b < bands.length; ++b) {
            var bd = bands[b]
            var capstr = (bd.ht ? "HT " : "") + (bd.vht ? "VHT " : "") + (bd.he ? "HE " : "")
            var brows = [
                row(qsTr("Standards"), capstr.replace(/ $/, "") || "—"),
                row(qsTr("Channels (usable)"), (bd.channelsEnabled || []).join(", ") || "—")
            ]
            var dis = bd.channelsDisabled || []
            if (dis.length)
                brows.push(row(qsTr("Channels (blocked here)"), dis.join(", "), {active:false}))
            s.push({ title: qsTr("Band: %1").arg(bd.name), collapsed: true,
                     note: qsTr("Capabilities of the Wi-Fi chip on this band; blocked channels are grayed."),
                     rows: brows })
        }
    }

    var ifaceSecs = []
    for (var i = 0; i < nics.length; ++i) {
        var n = nics[i]
        var rows = [
            row(qsTr("Type"), n.kind),
            row(qsTr("State"), n.state + (n.carrier ? " · " + qsTr("carrier") : "")),
            row(qsTr("IP address"), n.ipv4),
            row("MAC", n.mac, {mono:true}),
            row("MTU", n.mtu),
            row(qsTr("Driver"), n.driver),
            row(qsTr("Chip"), (n.vendor ? n.vendor : "") + (n.model ? " " + n.model : ""), {mono:true})
        ]
        if (n.speedMbit) rows.push(row(qsTr("Link speed"), n.speedMbit + " Mbit/s"))
        if (n.ipv6) rows.push(row(qsTr("IPv6"), n.ipv6, {mono:true}))
        rows.push(row(qsTr("Download / Upload"),
                      sysmon.fmtBytes(n.rxBytes) + "  /  " + sysmon.fmtBytes(n.txBytes)))
        if (countedSince(n)) rows.push(row(qsTr("Counted since"), countedSince(n)))
        if (n.rxErrors || n.txErrors) rows.push(row(qsTr("Errors"), "rx " + n.rxErrors + " · tx " + n.txErrors))
        ifaceSecs.push({ title: n.iface, rows: rows })
    }
    s = s.concat(group(ifaceSecs, "iface", qsTr("Every network interface"),
        qsTr("Every interface the kernel has registered, whether or not anything is using it: the loopback, the ones a modem brings up per data context, and the virtual ones a container or a tether creates. Each keeps counters of its own, which start when the interface does — not when the phone did.")))
    // Ultimate: WLAN chipset identity
    var w = sysmon.wirelessDetail()
    if (w.wlanDriver || w.wlanDtNode || w.wlanFwBuild) {
        var wr = []
        if (w.wlanDriver) wr.push(row(qsTr("Driver"), w.wlanDriver, {mono:true}))
        if (w.wlanDevice) wr.push(row(qsTr("Device"), w.wlanDevice, {mono:true}))
        if (w.wlanDtNode) wr.push(row(qsTr("Device-tree node"), w.wlanDtNode, {mono:true}))
        if (w.wlanDtCompatible) wr.push(row(qsTr("Compatible"), w.wlanDtCompatible, {mono:true}))
        if (w.wlanFwVersion) wr.push(row(qsTr("Firmware version"), w.wlanFwVersion, {mono:true}))
        if (w.wlanFwBuild) wr.push(row(qsTr("Firmware build"), w.wlanFwBuild, {mono:true}))
        if (!w.wlanFwVersion && !w.wlanFwBuild) {
            var rootOn = false
            try { rootOn = (typeof rootmon !== "undefined") && rootmon.active } catch (eW) {}
            if (!rootOn)
                wr.push(row(qsTr("Firmware version"),
                            qsTr("root mode shows version and build of the running WLAN firmware here"),
                            {active:false}))
        }
        if (w.rfkill) wr.push(row("rfkill", w.rfkill))
        // The blob names are a list, not a sentence: one per line, with the
        // directory each was found in, and folded away because nobody needs
        // them to see which chip this is.
        var fwf = w.firmwareFileList || []
        if (fwf.length) {
            var fwr = []
            for (var fx = 0; fx < fwf.length; ++fx)
                fwr.push(row(fwf[fx].name, fwf[fx].dir, {mono:true}))
            s.unshift({ title: qsTr("Firmware files for this radio"), collapsed: true,
                note: qsTr("The firmware images shipped for this radio, by name and by the directory each sits in. The names carry the chip family, which is what makes them worth listing at all. That a file is present says it is available to load — not that this system loaded it."),
                rows: fwr })
        }
        s.unshift({ title: qsTr("WLAN chipset"),
                    note: qsTr("Chip identity from driver, device tree and firmware."),
                    rows: wr })
    }
    s.push(diagnosisHere())
    return { title: qsTr("Network"), helpTopics: ["conn","raw","firmware"], sections: s.concat(catSections("radio")).concat(fwNet()).concat(fwModules(["wlan","cfg80211","mac80211","conninfra","connfem","connadp"])).concat(dtSections("wifi wlan consys conninfra ethernet", qsTr("Device tree — network"))).concat(rawSections("net")), diagTopic: "network" }
}

function batt() {
    var h = sysmon.batteryHardware()
    var c = sysmon.chargerDetail()
    var cp = sysmon.chargingPath()
    var supplies = []
    try { supplies = sysmon.powerSupplyDump() } catch (eD) { supplies = [] }
    var sections = []
    // The driver's own view of the charging path: worth having, not worth
    // standing between the reader and the state of the cell.
    var chargeDetail = []

    if (c.online) {
        var crows = [
            row(qsTr("Status"), c.status),
            row(qsTr("Protocol"), c.protocol + (c.typeRaw ? "  (" + c.typeRaw + ")" : "")),
            row(qsTr("Charge type"), c.chargeType),
            row(qsTr("Charging power"), c.chargePower !== undefined ? c.chargePower.toFixed(1) + " W" : "—", {color:"#8ef94a"}),
            row(qsTr("Into battery"), (c.chargeCurrent !== undefined ? (c.chargeCurrent*1000).toFixed(0) + " mA" : "—")
                + (c.batteryVoltage ? "  @ " + c.batteryVoltage.toFixed(2) + " V" : "")),
            row(qsTr("Input"), (c.inputVoltage ? c.inputVoltage.toFixed(2) + " V"
                    : (cp.vbus ? cp.vbus.toFixed(2) + " V  " + qsTr("(charger ADC)") : "—"))
                + (c.inputCurrentMax ? "  ·  max " + c.inputCurrentMax.toFixed(2) + " A" : ""))
        ]
        if (c.pdActive !== undefined)
            crows.push(row("USB-PD", c.pdActive
                ? qsTr("active — up to %1 V / %2 A").arg(c.inputVoltageMax.toFixed(0)).arg(c.pdCurrentMax.toFixed(1))
                : qsTr("not active")))
        if (c.typecPowerRole)
            crows.push(row(qsTr("Type-C role"), c.typecPowerRole + (c.typecDataRole ? "  ·  " + c.typecDataRole : "")))
        if (c.typecCurrent)
            crows.push(row(qsTr("Type-C current (CC advertise)"), c.typecCurrent))
        if (c.pdRevision || c.typecRevision)
            crows.push(row(qsTr("PD / Type-C revision"), (c.pdRevision || "?") + " / " + (c.typecRevision || "?")))
        if (c.partnerPd !== undefined)
            crows.push(row(qsTr("Partner supports PD"), c.partnerPd ? qsTr("yes") : qsTr("no")))
        crows.push(row(qsTr("Cable e-marker"), c.cablePresent
            ? ((c.cableType || "") + (c.cableProduct ? "  ·  " + c.cableProduct : ""))
            : qsTr("not exposed by this chipset"), { active: c.cablePresent === true }))
        sections.push({ title: qsTr("Charging"),
                        note: qsTr("Live charger negotiation and the rate into the battery."), rows: crows })

        // What the source offers, decoded from the raw PDOs. Unprivileged —
        // this block works with the root helper switched off.
        if (c.pdos && c.pdos.length) {
            var prows = []
            var texts = []
            for (var p = 0; p < c.pdos.length; ++p) {
                var o = c.pdos[p]
                if (o.kind === "fixed")
                    texts.push(o.voltage.toFixed(1) + " V  ·  " + o.current.toFixed(1) + " A  ·  "
                               + (o.voltage * o.current).toFixed(0) + " W")
                else if (o.kind === "pps")
                    texts.push(qsTr("PPS") + "  " + o.voltageMin.toFixed(1) + "–" + o.voltageMax.toFixed(1)
                               + " V  ·  " + o.current.toFixed(1) + " A")
                else if (o.kind === "variable")
                    texts.push(qsTr("variable") + "  " + o.voltageMin.toFixed(1) + "–" + o.voltageMax.toFixed(1)
                               + " V  ·  " + o.current.toFixed(1) + " A")
                else if (o.kind === "battery")
                    texts.push(qsTr("battery") + "  " + o.voltageMin.toFixed(1) + "–" + o.voltageMax.toFixed(1)
                               + " V  ·  " + o.power.toFixed(0) + " W")
                else if (o.kind === "eprAvs")
                    texts.push(qsTr("EPR adjustable voltage (not decoded, raw 0x%1)").arg(o.raw))
                else
                    texts.push(qsTr("SPR adjustable voltage (not decoded, raw 0x%1)").arg(o.raw))
            }

            // One line for the outcome. The specification calls this the
            // "contract"; here it says what was negotiated, which is what the
            // reader wants at this spot.
            var outcome
            if (c.pdContract !== "explicit") {
                outcome = qsTr("not negotiated — 5 V from the CC resistors")
            } else if (c.ppsVoltage) {
                outcome = qsTr("PPS  %1 V / %2 A").arg(c.ppsVoltage.toFixed(2)).arg(c.ppsCurrent.toFixed(2))
                    + (c.pdRequestedObject ? "   " + qsTr("(object %1)").arg(c.pdRequestedObject) : "")
            } else if (c.pdRequestedObject && texts[c.pdRequestedObject - 1]) {
                outcome = texts[c.pdRequestedObject - 1]
                    + "   " + qsTr("(object %1)").arg(c.pdRequestedObject)
            } else {
                outcome = qsTr("negotiated")
            }
            prows.push(row(qsTr("Negotiated"), outcome,
                           {color: c.pdContract === "explicit" ? "#8ef94a" : undefined}))

            for (var q = 0; q < c.pdos.length; ++q)
                prows.push(row("PDO " + c.pdos[q].index, texts[q],
                               {color: c.pdRequestedObject === c.pdos[q].index ? "#8ef94a" : undefined}))

            if (c.pdMaxPower)
                prows.push(row(qsTr("Maximum offered"), c.pdMaxPower.toFixed(0) + " W"))
            prows.push(row(qsTr("Extensions"),
                "PPS " + (c.pdPps ? qsTr("yes") : qsTr("no"))
                + "  ·  EPR " + (c.pdEpr ? qsTr("yes") : qsTr("no"))
                + "  ·  AVS " + (c.pdAvs ? qsTr("yes") : qsTr("no"))))
            if (c.pdSpecFloor)
                prows.push(row(qsTr("Source implements at least"), "PD " + c.pdSpecFloor))
            sections.push({ title: qsTr("Power source capabilities"),
                note: qsTr("Every object the charger offered, read from the raw PDOs. The revision in the PD message header can only express 1.0, 2.0 or 3.0 — 3.1 and 3.2 keep it at 3.0 on purpose. What a source implements beyond 3.0 is therefore derived from the extensions it offers: PPS means 3.0 or later, EPR means 3.1, SPR-AVS means 3.2. An absent extension means it was not offered here, not that the charger cannot do it."),
                rows: prows })
        }

        // handshake / kernel-log (root mode only; full PD packets are not in sysfs)
        var rootActive = false
        try { rootActive = (typeof rootmon !== "undefined") && rootmon.active } catch (eR) { rootActive = false }
        if (rootActive) {
            var log = []
            try { log = rootmon.chargerLog() } catch (eL) { log = [] }
            // human-readable timeline of what happened
            var events = humanizeChargerLog(log)
            var erows = []
            for (var k = 0; k < events.length; ++k)
                erows.push(row(events[k].t, events[k].text, {color: events[k].color}))
            if (!erows.length) erows.push(row(qsTr("Events"), qsTr("no charger events in the kernel buffer")))
            sections.push({ title: qsTr("Charger handshake — what happened"),
                note: qsTr("Plain-language reading of the charger driver's negotiation. The full USB-PD packet exchange is not exposed by this chipset."),
                rows: erows })
            // raw excerpt as expandable detail (MoreToggle collapses it)
            var hrows = []
            for (var j = 0; j < log.length; ++j) hrows.push(row("", log[j], {mono:true}))
            if (hrows.length)
                chargeDetail.push({ title: qsTr("Kernel log (raw excerpt)"), collapsed: true, rows: hrows })
        } else {
            sections.push({ title: qsTr("Charger handshake"),
                rows: [ row(qsTr("Log"), qsTr("Root mode required — start the helper to read the kernel charger log.")) ] })
        }
    }

    // ---- what the charger driver itself exports -------------------------
    // The power-supply class is the common denominator between chipsets; the
    // vendor driver keeps a directory of its own beside it, and on MediaTek
    // that is where the bus voltage, the negotiated adapter and the throttling
    // flag actually live.
    if (cp.vendor) {
        var vr = []
        if (cp.vbus !== undefined)
            vr.push(row(qsTr("Bus voltage"), cp.vbus.toFixed(3) + " V", {color:"#8ef94a"}))
        if (cp.adcCurrent !== undefined)
            vr.push(row(qsTr("Charging current"), (cp.adcCurrent * 1000).toFixed(0) + " mA", {color:"#8ef94a"}))
        vr.push(row(qsTr("Adapter"), cp.adapterType))
        vr.push(row(qsTr("Charging mode"), cp.chargingMode, {mono:true}))
        vr.push(row(qsTr("Charger type (driver)"), cp.chargerType, {mono:true}))
        vr.push(row(qsTr("Pump Express"), cp.pumpExpress))
        vr.push(row(qsTr("High-voltage charging"), cp.highVoltage))
        vr.push(row(qsTr("Software JEITA"), cp.swJeita))
        vr.push(row(qsTr("Smart charging"), cp.smartCharging))
        vr.push(row(qsTr("Power path"), cp.powerPath))
        vr.push(row(qsTr("Over-voltage threshold"),
                    cp.ovpVolt ? cp.ovpVolt.toFixed(1) + " V" : "—"))
        vr.push(row(qsTr("Fast-charge indicator"), cp.fastChargeIndicator, {mono:true}))
        if (cp.scTargetSoc || cp.scCurrentLimit)
            vr.push(row(qsTr("Smart-charge schedule"),
                        (cp.scTargetSoc ? qsTr("hold at %1 %").arg(cp.scTargetSoc) : "")
                        + (cp.scCurrentLimit ? "  ·  " + qsTr("limit %1 mA").arg(cp.scCurrentLimit) : "")
                        + (cp.scStart || cp.scEnd ? "  ·  " + (cp.scStart || "0") + "–" + (cp.scEnd || "0") + " s" : "")))
        if (cp.safetyTimer !== undefined && cp.safetyTimer !== "")
            vr.push(row(qsTr("Safety timer"), cp.safetyTimer))
        if (cp.setCv !== undefined && cp.setCv !== "")
            vr.push(row(qsTr("Charge-voltage override"), cp.setCv, {mono:true}))
        vr.push(row(qsTr("Corrosion detection"), cp.rustDetect))
        vr.push(row(qsTr("Throttle flag"),
                    cp.throttleFlag === "1" ? qsTr("1 — has tripped at least once") : cp.throttleFlag,
                    {color: cp.throttleFlag === "1" ? "#ffb44a" : undefined}))
        chargeDetail.push({ title: qsTr("Charging path (%1)").arg(cp.vendor), collapsed: true,
            note: qsTr("From the charger driver's own directory, which sits below the power-supply class. The two figures at the top are its ADC readings — bus voltage in millivolt, charging current in milliamp — and they are the only ones here converted into units, because that scale was checked against the battery node at the same operating point. The throttle flag latches: it says the driver's thermal limit has tripped at some point, not that it is limiting now. It has been observed standing at 1 while full current flowed again, so it answers whether, never how much."),
            rows: vr })
    }

    // ---- the charging chain, stage by stage -----------------------------
    if (supplies.length) {
        var sr = []
        for (var si = 0; si < supplies.length; ++si) {
            var su = supplies[si]
            var st = []
            if (su.type && su.type !== "Unknown") st.push(su.type)
            if (su.status) st.push(su.status)
            if (su.usbTypeActive && su.usbTypeActive !== "Unknown") st.push(su.usbTypeActive)
            var up = su.online !== "" && su.online !== "0"
            if (su.online !== "") st.push(up ? qsTr("online") : qsTr("offline"))
            if (su.model) st.push(su.model)
            var live = up || su.status === "Charging"
            sr.push(row(su.name + (su.driver ? "  ·  " + su.driver : ""), st.join("  ·  "),
                        { active: live ? undefined : false,
                          right: (su.attrs ? su.attrs.length : 0) + "" }))
        }
        chargeDetail.push({ title: qsTr("Charging chain"), collapsed: true,
            note: qsTr("Every node the kernel registered under the power-supply class, the driver behind it and — on the right — how many attributes it exports. A single-path charger registers one input and the battery; a divider topology registers each silicon stage separately, and only the ones actually carrying current come up online. The grayed ones are present but idle."),
            rows: sr })
    }

    // ---- what is able to throttle the charge ----------------------------
    var th = {}
    try { th = sysmon.thermalDetail() } catch (eT) { th = {} }
    var cool = th.cooling || []
    var chgCool = []
    for (var ci = 0; ci < cool.length; ++ci)
        if (/charg|batt|bcl/i.test(cool[ci].type || ""))
            chgCool.push(cool[ci])
    if (chgCool.length) {
        var kr = []
        for (var ki = 0; ki < chgCool.length; ++ki) {
            var cd = chgCool[ki]
            kr.push(row(cd.type, cd.cur + " / " + cd.max + "  ·  "
                        + (cd.boundTo ? qsTr("bound to %1").arg(cd.boundTo)
                                      : qsTr("not bound to any zone")),
                        {color: cd.cur > 0 ? "#ffb44a" : undefined}))
        }
        chargeDetail.push({ title: qsTr("Charge throttling"), collapsed: true,
            note: qsTr("Cooling devices the kernel offers for the charging path, as current step out of maximum step. A cooling device that no thermal zone binds cannot be driven by the kernel's governor at all — it exists, and nothing reaches for it. That does not mean nothing throttles: a vendor charger driver can limit the current entirely inside itself, where the thermal framework never sees it."),
            rows: kr })
    }

    if (cp.modules && cp.modules.length)
        chargeDetail.push({ title: qsTr("Charging drivers loaded"), collapsed: true,
            note: qsTr("Kernel modules with a part in charging. Each negotiation protocol ships as its own module, so this says what the hardware is able to negotiate at all — independently of what happens to be plugged in. Names are the drivers' own."),
            rows: [ row(qsTr("Modules"), cp.modules.join("   "), {mono:true}) ] })

    sections.push({ title: qsTr("Identity"), rows: [
        row(qsTr("Supply"), h.supply, {mono:true}),
        row(qsTr("Manufacturer"), h.manufacturer),
        row(qsTr("Model"), h.model),
        row(qsTr("Serial"), h.serial, {mono:true}),
        row(qsTr("Technology"), h.technology)
    ]})
    var hrows = [
        row(qsTr("Design capacity"), (h.designCapacity ? h.designCapacity.toFixed(0) + " " + h.capacityUnit : "—")),
        row(qsTr("Full capacity"), (h.fullCapacity ? h.fullCapacity.toFixed(0) + " " + h.capacityUnit : "—")),
        row(qsTr("Full ÷ design"), sysmon.battHealthExact >= 0 ? sysmon.battHealthExact.toFixed(1) + " %" : "—",
            {color: sysmon.battHealthPct >= 80 ? "#8ef94a" : sysmon.battHealthPct >= 65 ? "#ffb44a" : "#ff5a52"})
    ]
    // The register that claims to know, kept beside the ratio instead of
    // replacing it, so a disagreement stays visible.
    if (sysmon.battSohRegister >= 0)
        hrows.push(row(qsTr("soh register"), sysmon.battSohRegister + " %", {mono:true}))
    if (h.learnEvents !== undefined)
        hrows.push(row(qsTr("Capacity measurements"), h.learnEvents))
    if (h.profileId) hrows.push(row(qsTr("Battery profile"), h.profileId, {mono:true}))
    hrows.push(row(qsTr("Design voltage"), h.voltageDesign ? h.voltageDesign.toFixed(2) + " V" : "—"))
    hrows.push(row(qsTr("Driver health"), h.health))
    hrows.push(row(qsTr("Quality"), sysmon.battQuality))
    hrows.push(row(qsTr("Based on"), sysmon.battQualityBasis))
    sections.push({ title: qsTr("Capacity & health"), rows: hrows })

    // Without a single learned capacity, "full capacity" is a profile entry and
    // the ratio above compares two catalogue numbers -- worth saying, because
    // the same figure would be an ageing measurement on a gauge that had learned.
    if (h.learnEvents === 0)
        sections.push({ note: qsTr("This gauge has never measured a capacity of its own: all of its learning counters stand at zero, and charge_full comes from the battery profile. The percentage above therefore compares the profile of the installed cell against the design capacity of the original — it is not a measurement of ageing. On a replacement cell with a smaller nominal capacity it will read low from the first day and never move."),
                        rows: [] })

    // Charge cycles: the headline figure is a mean, and the bands it averages
    // say more than it does.
    var crows2 = [ row(qsTr("Equivalent full cycles"), h.cycles) ]
    if (h.cycleBandTotal !== undefined) {
        crows2.push(row(qsTr("Band crossings"), Math.round(h.cycleBandTotal)))
        if (h.cycleBuckets) crows2.push(row(qsTr("Per band"), h.cycleBuckets.join("  ·  "), {mono:true}))
    }
    if (h.ageLevel !== undefined)
        crows2.push(row(qsTr("Ageing profile"), qsTr("step %1").arg(h.ageLevel)))
    if (h.esrMilliOhm)
        crows2.push(row(qsTr("Internal resistance (ESR)"), h.esrMilliOhm.toFixed(0) + " mΩ"))
    if (h.storedMilliOhm)
        crows2.push(row(qsTr("Total resistance (stored)"), h.storedMilliOhm.toFixed(0) + " mΩ"))
    if (h.batteryIdOhm)
        crows2.push(row(qsTr("Battery ID resistor"), (h.batteryIdOhm / 1000).toFixed(1) + " kΩ"))
    sections.push({ title: qsTr("Wear indicators"), rows: crows2 })
    if (h.cycleBandTotal !== undefined)
        sections.push({ note: qsTr("The gauge counts how often each of eight state-of-charge bands was crossed; the cycle figure it reports is the mean of those eight, which is why it reads far lower than the charging actually done. The resistances come from three separate registers of the Qualcomm gauge, each on its own scale — the ESR is the one that tracks ageing, while the ID resistor only identifies the cell."),
                        rows: [] })
    // Both numbers are on the page now; flag it when they contradict each other,
    // because then at most one of them can be describing the cell.
    if (sysmon.battSohRegister >= 0 && sysmon.battHealthExact > 0
            && Math.abs(sysmon.battSohRegister - sysmon.battHealthExact) >= 5)
        sections.push({ note: qsTr("The soh register claims %1 % while the capacities work out to %2 %. Both cannot describe the same cell. A flat 100 from that register is a common stand-in on Qualcomm gauges — it is answering, not measuring.")
                              .arg(sysmon.battSohRegister).arg(sysmon.battHealthExact.toFixed(1)),
                        rows: [] })
    sections.push({ title: qsTr("Live"), rows: [
        row(qsTr("Level"), sysmon.battCapacity + " %  ·  " + sysmon.battStatus),
        row(qsTr("Voltage"), sysmon.battVoltageV.toFixed(3) + " V"),
        row(qsTr("Current"), (sysmon.battCurrentA * 1000).toFixed(0) + " mA"),
        row(qsTr("Power"), sysmon.battPowerW.toFixed(2) + " W"),
        row(qsTr("Temperature"), sysmon.battTempC.toFixed(1) + " °C")
    ]})

    // Where the current goes. The gauge measures the sum; the kernel's energy
    // model splits the CPU's share of it. The page states both and their rest.
    var top = []
    try { if (typeof procs !== "undefined" && procs.topByPower) top = procs.topByPower(8) } catch (eP) { top = [] }
    var pw = (typeof power !== "undefined") ? power : null
    var haveMa = pw && pw.available
    var prows = []

    if (pw && pw.totalMilliAmp > 0) {
        prows.push(row(qsTr("From the battery"), pw.totalMilliAmp.toFixed(0) + " mA"))
        if (haveMa && pw.cpuMilliAmp >= 0) {
            var rest = pw.totalMilliAmp - pw.cpuMilliAmp
            prows.push(row(qsTr("Of that, the CPU"), pw.cpuMilliAmp.toFixed(0) + " mA"))
            prows.push(row(qsTr("Everything else"), (rest > 0 ? rest.toFixed(0) : "0") + " mA"))
        }
    }

    for (var i = 0; i < top.length; ++i) {
        var t = top[i]
        var val = (haveMa && t.mA >= 0) ? (t.mA < 10 ? t.mA.toFixed(1) : t.mA.toFixed(0)) + " mA"
                                        : t.share.toFixed(0) + " %"
        prows.push(row((i + 1) + ". " + t.name + "  (" + t.pid + ")", val,
                       { right: t.cpu.toFixed(0) + " % CPU" }))
    }
    if (!top.length) prows.push(row(qsTr("Processes"), qsTr("none")))

    // One line on the page; the reasoning lives in the glossary.
    var pnote
    if (haveMa && pw.scaleFromGauge)
        pnote = qsTr("Approximations. The CPU's share of the drain, split by the kernel's energy model and scaled against the gauge (±%1 %). The rest is display, radios and idle — no process caused it.")
                .arg((pw.spread * 100).toFixed(0))
    else if (haveMa)
        pnote = qsTr("Approximations, and a floor: the CPU's share of the drain from the kernel's energy model, which counts core power only. The true figures are higher.")
    else
        pnote = qsTr("Shares of the CPU work. This kernel publishes no energy model, so there is nothing to convert into milliamps.")
    sections.push({ title: qsTr("Where the current goes"), note: pnote, rows: prows })

    // What keeps the device awake. Not an estimate at all: the kernel counts
    // this itself, and on a phone that will not suspend it answers the battery
    // question that CPU time cannot.
    var wk = sysmon.wakeupSources()
    if (wk && wk.length) {
        var wrows = []
        var upS = sysmon.uptimeSec
        for (var wi = 0; wi < wk.length && wi < 8; ++wi) {
            var held = sysmon.fmtDuration(Math.round(wk[wi].heldSec))
            if (upS > 0) held += "  ·  " + (100 * wk[wi].heldSec / upS).toFixed(0) + " %"
            wrows.push(row(wk[wi].name, held,
                           { right: wk[wi].count > 0 ? wk[wi].count.toFixed(0) + " ×" : "" }))
        }
        sections.push({ title: qsTr("What keeps the device awake"),
            note: qsTr("How long each source held the system out of suspend since boot, and how often. Counted by the kernel, not estimated here."),
            rows: wrows })
    }

    // Every register of every supply, unabridged. On a divider topology this
    // is where the gauge's own hundred-odd registers live, and none of them
    // reach the curated sections above because no one has named them.
    var dumps = []
    for (var di = 0; di < supplies.length; ++di) {
        var sd = supplies[di]
        var at = sd.attrs || []
        if (!at.length) continue
        var ar = []
        for (var ai = 0; ai < at.length; ++ai)
            ar.push(row(at[ai].name, at[ai].value, {mono:true}))
        dumps.push({ title: sd.name + (sd.driver ? "  ·  " + sd.driver : ""), rows: ar })
    }
    var dumpNote = qsTr("Every attribute of every power-supply node, exactly as the kernel wrote it. Units are the drivers' own and are not uniform: the same key can count microamps on one node and milliamps on the next, so nothing here is converted. An empty value is a node that answered with nothing.")
    if (dumps.length && dumps.length <= 3)
        dumps[0].note = dumpNote
    sections.push(diagnosisHere())
    sections = sections.concat(chargeDetail).concat(catSections("battery"))
        .concat(group(folded(dumps), "psy", qsTr("Every power-supply node"), dumpNote))
        .concat(fwModules(["charg","chg","gauge","battery","ufcs","adapter","pmic","tcpc"]))
        .concat(dtSections("charg batt gauge fuel pmic", qsTr("Device tree — power")))
        .concat(rawSections("battery"))

    return { title: qsTr("Battery"), helpTopics: ["battery","usb","raw","firmware"], sections: sections, diagTopic: "battery" }
}

// The thermal framework as a page of its own: the live card on the overview
// shows the zones that carry a temperature, this shows the register behind it
// — every zone including the ones the card drops, every trip point, and every
// cooling device with the zone that binds it.
function thermal() {
    var t = sysmon.thermalDetail()
    var zones = t.zones || []
    var s = []

    var reason = {
        empty:    qsTr("no reading"),
        watchdog: qsTr("not a temperature — a battery watchdog wired into the thermal framework"),
        zero:     qsTr("reads zero — stage not powered"),
        range:    qsTr("out of range")
    }

    var lim = t.limits || {}
    var limRows = []
    function limitRow(label, v) {
        if (v === undefined) return
        limRows.push(row(label, v === "1" ? qsTr("being limited now") : qsTr("not limited"),
                         { color: v === "1" ? "#ffb44a" : "#8ef94a" }))
    }
    limitRow(qsTr("Processor"), lim.cpuLimited)
    limitRow(qsTr("Graphics"), lim.gpuLimited)
    limitRow(qsTr("AI accelerator"), lim.apuLimited)
    if (lim.junctionTarget)
        limRows.push(row(qsTr("Junction target (CPU, GPU, AI)"), lim.junctionTarget))
    if (lim.junctionMin)
        limRows.push(row(qsTr("Lowest junction target it will fall to"), lim.junctionMin))
    if (lim.powerBudget)
        limRows.push(row(qsTr("Power budget (CPU, GPU, AI)"), lim.powerBudget))
    if (lim.headroom)
        limRows.push(row(qsTr("Headroom to target (per core, then board)"), lim.headroom))
    if (lim.cpuTemps)
        limRows.push(row(qsTr("Per-core temperature"), lim.cpuTemps))
    if (lim.gpuTemp)
        limRows.push(row(qsTr("Graphics temperature"), lim.gpuTemp))
    if (lim.gpuClock)
        limRows.push(row(qsTr("Graphics clock (now / cap)"), lim.gpuClock))
    if (lim.skinTarget)
        limRows.push(row(qsTr("Skin target"), lim.skinTarget))
    if (lim.skinTemp)
        limRows.push(row(qsTr("Skin temperature"), lim.skinTemp))
    if (lim.throttleFloor)
        limRows.push(row(qsTr("Clock floor while throttling"), lim.throttleFloor))
    if (lim.dsuCeiling)
        limRows.push(row(qsTr("Cluster interconnect ceiling"), lim.dsuCeiling))
    if (limRows.length)
        s.push({ title: qsTr("Is anything being limited right now?"),
                 note: qsTr("From the vendor's thermal interface, which is the only place that answers this. The zone list gives temperatures and the trip points give intentions; these flags give the state of the limiter itself. Where they are absent, the platform does not publish the answer and it can only be inferred from clocks that fail to reach their ceiling.")
                     + "\n\n" + qsTr("The junction target is the die temperature the limiter steers towards — not a measurement and not a shutdown threshold: as the silicon approaches it, clock is taken away to keep it there. It is listed once per domain the limiter governs, in the order processor, graphics, AI accelerator, which is why the same figure can appear three times. The lowest target below it is how far the limiter may push that goal down when the outside of the phone gets warm.")
                     + "\n\n" + qsTr("Two of these fields carry a stand-in rather than a value: this driver writes 666666666 when no limit is set, and a temperature below absolute zero when no sensor is fitted. Both are shown as words here, because printed as numbers they would read as a 666 kW budget and a temperature of −274 °C."),
                 rows: limRows })

    var live = [], quiet = []
    for (var i = 0; i < zones.length; ++i) {
        var z = zones[i]
        if (z.tempC !== undefined)
            // The raw figure stays beside the reading: a zone that is really a
            // voltage shows it there, in millivolts, next to neighbours in
            // millidegrees.
            live.push(row((z.name || z.node) + (z.suspect ? "  ⚠" : ""),
                          z.tempC.toFixed(1) + " °C"
                          + (z.suspect ? "   " + qsTr("does not fit the other zones") : ""),
                          { right: z.raw,
                            color: z.suspect ? "#ffb44a"
                                 : z.tempC > 70 ? "#ff5a52"
                                 : z.tempC > 55 ? "#ffb44a" : undefined }))
        else
            quiet.push(row(z.name || z.node, reason[z.skipped] || z.skipped || "—", { right: z.node }))
    }
    if (live.length) {
        var anySuspect = false
        for (var q = 0; q < zones.length; ++q) if (zones[q].suspect) anySuspect = true
        s.push({ title: qsTr("Zones with a reading"),
                 note: qsTr("Every thermal zone the kernel registers, with the raw figure it exported on the right — millidegrees, for a zone that is really a temperature. Names are the kernel's; where a vendor names a zone after a component, the sensor usually sits near it rather than on it.")
                     + (anySuspect ? "\n\n" + qsTr("A zone marked ⚠ is sitting far below every other sensor in this phone. That cannot happen to a real one: the board has a single ambient and the parts on it only ever sit above it. The usual explanation is a node that carries millivolts or milliamps and was registered in the thermal framework anyway, where the framework then labels it °C. Compare the raw figure on the right with its neighbours — a reading of 4400 among readings of 33000 is a voltage.") : ""),
                 rows: live })
    }
    if (quiet.length)
        s.push({ title: qsTr("Zones without a usable reading"), collapsed: true,
                 note: qsTr("Registered zones the live card leaves out, each with the reason. A zone reading zero is generally a stage that is not powered; one carrying milliamps or millivolts is a battery watchdog that the vendor hung into the thermal framework because throttling runs through it. They stay listed here because the kernel does register them."),
                 rows: quiet })

    var tr = []
    for (var j = 0; j < zones.length; ++j) {
        var zz = zones[j]
        var tp = zz.trips || []
        for (var k = 0; k < tp.length; ++k)
            tr.push(row((zz.name || zz.node) + "  ·  " + tp[k].kind,
                        tp[k].tempC.toFixed(0) + " °C"
                        + (tp[k].hystC ? "  ·  " + qsTr("hysteresis %1 K").arg(tp[k].hystC.toFixed(0)) : "")))
    }
    s.push({ title: qsTr("Trip points"), collapsed: true,
             note: tr.length
                 ? qsTr("The temperatures at which the kernel is meant to act. A passive trip asks for throttling, a critical trip shuts the device down. Trip points in the region of 115 °C are silicon emergency stops, not operating limits — a device whose only trips sit up there does its everyday regulation somewhere else, or not at all.")
                 : qsTr("This kernel registers no trip points at all. Whatever regulates temperature here does so outside the thermal framework, where it cannot be read."),
             rows: tr.length ? tr : [ row(qsTr("Trip points"), qsTr("none registered")) ] })

    var cool = t.cooling || []
    var cr = []
    for (var m = 0; m < cool.length; ++m) {
        var cd = cool[m]
        cr.push(row(cd.type || cd.node,
                    cd.cur + " / " + cd.max + "  ·  "
                    + (cd.boundTo ? qsTr("bound to %1").arg(cd.boundTo) : qsTr("not bound to any zone")),
                    { right: cd.node, color: cd.cur > 0 ? "#ffb44a" : undefined }))
    }
    if (cr.length)
        s.push({ title: qsTr("Cooling devices"), collapsed: true,
                 note: t.bindings === 0
                     ? qsTr("Current step out of maximum step. Not one of these is bound to a thermal zone on this device: the kernel's governor has nothing to reach for, and every one of them stands where its driver left it. Throttling that does happen therefore happens inside a vendor driver, not here — so a cooling device resting at zero is no evidence that nothing is being limited.")
                     : qsTr("Current step out of maximum step, and the zone that drives each one. A cooling device no zone binds cannot be driven by the kernel's governor at all — it exists, and nothing reaches for it."),
                 rows: cr })

    return { title: qsTr("Thermal"), helpTopics: ["thermal","raw"],
             sections: s.concat(dtSections("thermal therm ntc lvts cooler", qsTr("Device tree — thermal")))
                        .concat(rawSections("thermal")) }
}

function bluetooth() {
    var a = bt.adapter || {}
    var s = []
    s.push({ title: qsTr("Controller"), rows: [
        row(qsTr("Name"), a.name || a.alias),
        row(qsTr("Address"), a.address, {mono:true}),
        row(qsTr("Address type"), a.addressType),
        row(qsTr("Chip"), a.modalias, {mono:true}),
        row(qsTr("Class"), a.class ? "0x" + a.class.toString(16) : "—")
    ]})
    // capability-style: powers/modes the adapter has; grayed when off.
    // Discoverable/pairable are BlueZ *configuration* that persists while the
    // adapter is powered down — showing them plain "on" then reads as if the
    // radio were visible. Gate them on powered and say what they really are.
    function mode(v) {
        if (!a.powered)
            return v ? qsTr("on, once Bluetooth is switched on")
                     : qsTr("off, also once switched on")
        return v ? qsTr("on") : qsTr("off")
    }
    s.push({ title: qsTr("State"),
        note: qsTr("Adapter capabilities; grayed ones are supported but currently off."),
        rows: [
            row(qsTr("Powered"), a.powered ? qsTr("on") : qsTr("off"), { active: a.powered === true }),
            row(qsTr("Discoverable"), mode(a.discoverable), { active: a.powered === true && a.discoverable === true }),
            row(qsTr("Pairable"), mode(a.pairable), { active: a.powered === true && a.pairable === true }),
            row(qsTr("Scanning"), a.discovering ? qsTr("on") : qsTr("off"), { active: a.discovering === true })
        ]})
    var devs = bt.devices || []
    var drows = []
    for (var i = 0; i < devs.length; ++i)
        drows.push(row(devs[i].name || devs[i].address,
                       (devs[i].connected ? qsTr("connected") : qsTr("paired"))
                       + (devs[i].icon ? " · " + devs[i].icon : ""),
                       { active: devs[i].connected === true }))
    s.push({ title: qsTr("Devices"), rows: drows })
    // Ultimate: BT chipset identity (shares the combo chip with WLAN)
    var wb = sysmon.wirelessDetail()
    if (wb.btDtNode || wb.btAdapters) {
        var br = []
        if (wb.btDtNode) br.push(row(qsTr("Device-tree node"), wb.btDtNode, {mono:true}))
        if (wb.btDtCompatible) br.push(row(qsTr("Compatible"), wb.btDtCompatible, {mono:true}))
        if (wb.btAdapters) br.push(row(qsTr("Adapters"), wb.btAdapters, {mono:true}))
        if (wb.rfkill) br.push(row("rfkill", wb.rfkill))
        s.unshift({ title: qsTr("Bluetooth chipset"),
                    note: qsTr("On most ports BT shares the WLAN combo chip; the device-tree node names it."),
                    rows: br })
    }
    s.push(diagnosisHere())
    return { title: qsTr("Bluetooth"), helpTopics: ["raw","firmware"], sections: s.concat(fwModules(["bluetooth","btmtk","bt_drv","hci","rfkill"])).concat(dtSections("bluetooth btif consys connfem", qsTr("Device tree — Bluetooth"))).concat(rawSections("bt")), diagTopic: "bluetooth" }
}

function audio() {
    var d = sysmon.audioDetail()
    var s = []
    var cards = d.cards || [], cardRows = []
    for (var i = 0; i < cards.length; ++i)
        cardRows.push(row(qsTr("Card %1").arg(cards[i].index), cards[i].name))
    if (!cardRows.length) cardRows.push(row(qsTr("Cards"), qsTr("none")))
    s.push({ title: qsTr("Sound cards"), rows: cardRows })

    var codecs = d.codecs || [], codecRows = []
    for (var c = 0; c < codecs.length; ++c) codecRows.push(row(qsTr("Codec"), codecs[c], {mono:true}))
    if (codecRows.length)
        s.push({ title: qsTr("Codec chip"), rows: codecRows })

    var jack = d.jackState
    var jtxt = jack === 2 ? qsTr("headphones plugged") : jack === 1 ? qsTr("headset plugged")
             : jack === 0 ? qsTr("nothing plugged") : qsTr("not reported")
    s.push({ title: qsTr("Connectors & status"),
        note: qsTr("The kernel does not report hardware faults; shown are jack state and stream activity."),
        rows: [
            row(qsTr("Headphone jack"), jtxt, { active: jack > 0 }),
            row(qsTr("Playback"), d.playing ? qsTr("active") : qsTr("idle"), { active: d.playing === true }),
            row(qsTr("Capture"), d.capturing ? qsTr("active") : qsTr("idle"), { active: d.capturing === true })
        ]})

    var pa = sysmon.audioStreams()
    var sinks = pa.sinks || [], sources = pa.sources || []
    var srows = []
    for (var i = 0; i < sinks.length; ++i) {
        var sk = sinks[i]
        srows.push(row(sk.description || sk.name,
            (sk.volume !== undefined ? sk.volume + " %" : "—")
            + (sk.mute ? "  ·  " + qsTr("muted") : "") + "  ·  " + sk.state,
            { active: sk.state === "RUNNING" }))
    }
    if (srows.length)
        s.push({ title: qsTr("Outputs (sinks)"),
            note: qsTr("PulseAudio playback devices, with volume and state."), rows: srows })

    var qrows = []
    for (var j = 0; j < sources.length; ++j) {
        var so = sources[j]
        var isMic = (so.name && so.name.indexOf("primary_input") >= 0)
                 || (so.description && so.description.toLowerCase().indexOf("mic") >= 0)
        qrows.push(row((isMic ? "🎤 " : "") + (so.description || so.name),
            (so.volume !== undefined ? so.volume + " %" : "—")
            + (so.mute ? "  ·  " + qsTr("muted") : "") + "  ·  " + so.state,
            { active: so.state === "RUNNING", color: isMic ? "#31e0a0" : undefined }))
    }
    if (qrows.length)
        s.push({ title: qsTr("Inputs (sources)"),
            note: qsTr("PulseAudio capture devices. The microphone gain is the primary input's volume — reflects the harbour-mic-gain fix."), rows: qrows })

    s.push(diagnosisHere())
    return { title: qsTr("Audio"), helpTopics: ["raw","firmware"], sections: s.concat(catSections("audio")).concat(fwModules(["snd","spk","amp","audio","accdet"])).concat(dtSections("audio codec amp speaker snd accdet", qsTr("Device tree — audio"))).concat(rawSections("audio")), diagTopic: "audio" }
}

function camera() {
    var d = sysmon.cameraDetail()
    var s = []

    var cams = d.cameras || []
    if (cams.length) {
        var crows = []
        for (var i = 0; i < cams.length; ++i) {
            var c = cams[i]
            var label = c.maker ? (c.maker + " " + c.model.toUpperCase()) : c.model.toUpperCase()
            crows.push(row(c.role ? roleName(c.role) : qsTr("Camera %1").arg(i + 1), label, {mono:true}))
        }
        s.push({ title: qsTr("Image sensors"),
            note: qsTr("Recovered from the vendor camera modules (sensormodule/*.bin) — the actual sensor part numbers behind the HAL."),
            rows: crows })
    }

    s.push({ title: qsTr("Camera subsystem (CAMSS)"),
        note: qsTr("The cameras run behind the Android camera HAL (camx). The kernel exposes only the CAMSS infrastructure — these counts are real."),
        rows: [
            row(qsTr("Image sensors"), d.sensors),
            row(qsTr("Calibration EEPROMs"), d.eeproms),
            row(qsTr("Flash units"), d.flashes),
            row(qsTr("ISP"), d.isp ? qsTr("present") : qsTr("no")),
            row(qsTr("CAMSS sub-devices"), (d.subdevs || []).length)
        ]})

    s.push({ title: qsTr("Sensor characteristics"),
        note: qsTr("Mobile image sensors of this class use a Bayer colour-filter array — three primaries (RGB), one colour per pixel, demosaiced in the ISP. Raw output is typically 10-bit per channel."),
        rows: [
            row(qsTr("Colour filter"), qsTr("Bayer RGGB (3 primaries)")),
            row(qsTr("Optical format / pixel pitch"), qsTr("datasheet spec of the model above — not queryable on-device"))
        ]})

    s.push({ title: qsTr("Only available live"),
        note: qsTr("The static paths are exhausted — the rest requires opening the camera through the HAL."),
        rows: [
            row(qsTr("Resolutions / capture modes"), qsTr("Defined in the HAL, not in V4L2. Enumerable only by starting the camera (QtMultimedia / Camera2 supportedResolutions).")),
            row(qsTr("Pixel format (YUV/RAW)"), qsTr("Negotiated per session with the HAL — read it from a running Camera via viewfinder/imageCapture formats.")),
            row(qsTr("ISO / exposure range"), qsTr("Camera2 SENSOR_INFO_SENSITIVITY_RANGE — only on a live session, not from sysfs.")),
            row(qsTr("Front / back mapping"), qsTr("The role tags above (wide/tele/front/uwide) come from the module names; the V4L2 nodes themselves are CAMSS control interfaces and carry no position."))
        ]})

    var nodes = d.captureNodes || []
    var nrows = []
    for (var j = 0; j < nodes.length; ++j)
        nrows.push(row(nodes[j].node, nodes[j].label || qsTr("(unnamed)"), {mono:true}))
    if (nrows.length)
        s.push({ title: qsTr("Kernel video nodes"), collapsed: true,
            note: qsTr("Kernel V4L2 interfaces — control, JPEG and video-codec blocks, not user-facing cameras."), rows: nrows })

    // MediaTek: sensors sit behind imgsensor/mtkcam, not in V4L2 like Qualcomm CAMSS
    if (d.platform === "mediatek" && !cams.length)
        s.push({ title: qsTr("Image sensors"),
            note: qsTr("This is a MediaTek imgsensor/mtkcam stack. The image sensors are driven through the camera HAL, not exposed as V4L2 sensor sub-devices — so their models are not enumerable from sysfs. The video nodes above are the JPEG and video codecs."),
            rows: [ row(qsTr("Sensor models"), qsTr("not exposed by the MediaTek kernel")) ] })

    s.push(diagnosisHere())
    return { title: qsTr("Camera"), helpTopics: ["camera","raw","firmware"], sections: s.concat(catSections("camera")).concat(fwModules(["imgsensor","camera","seninf","flashlight","vcodec","jpeg"])).concat(dtSections("cam sensor seninf imgsensor flash lens", qsTr("Device tree — camera"))).concat(rawSections("camera")), diagTopic: "camera" }
}

function roleName(r) {
    r = ("" + r).toLowerCase()
    if (r.indexOf("uwide") >= 0 || r.indexOf("ultra") >= 0) return qsTr("Ultra-wide")
    if (r.indexOf("wide") >= 0) return qsTr("Wide (main)")
    if (r.indexOf("tele") >= 0) return qsTr("Telephoto")
    if (r.indexOf("front") >= 0) return qsTr("Front")
    if (r.indexOf("macro") >= 0) return qsTr("Macro")
    if (r.indexOf("depth") >= 0) return qsTr("Depth")
    return r
}

function usb() {
    var d = sysmon.usbDetail()
    var c = sysmon.chargerDetail()
    var s = []
    // controller identity first — present even with nothing plugged in
    var ctl = d.controller || {}
    if (ctl.name) {
        var ur = [ row(qsTr("Controller"), ctl.name, {mono:true}) ]
        if (ctl.compatible) ur.push(row(qsTr("Compatible"), ctl.compatible, {mono:true}))
        if (ctl.maxSpeed) ur.push(row(qsTr("Maximum speed"), ctl.maxSpeed))
        if (ctl.curSpeed) ur.push(row(qsTr("Current speed"), ctl.curSpeed))
        if (ctl.powerRole) ur.push(row(qsTr("Power role"), ctl.powerRole))
        if (ctl.dataRole) ur.push(row(qsTr("Data role"), ctl.dataRole))
        s.push({ title: qsTr("USB controller"),
                 note: qsTr("The SoC's USB IP core (dwc3 = Synopsys DesignWare USB3, musb = Mentor, mtu3 = MediaTek). Roles show the active side in [brackets]."),
                 rows: ur })
    }
    if (c.online) {
        s.push({ title: qsTr("Charging (USB-C input)"),
            note: qsTr("A charger is not a USB data device, so it is shown here as the power input. Full details are under Battery."),
            rows: [
                row(qsTr("Protocol"), c.protocol + (c.typeRaw ? "  (" + c.typeRaw + ")" : "")),
                row(qsTr("Charging power"), c.chargePower !== undefined ? c.chargePower.toFixed(1) + " W" : "—", {color:"#8ef94a"}),
                row(qsTr("Input"), (c.inputVoltage ? c.inputVoltage.toFixed(2) + " V" : "—")
                    + (c.inputCurrentMax ? "  ·  max " + c.inputCurrentMax.toFixed(2) + " A" : "")),
                row(qsTr("Type-C role"), c.typecPowerRole ? (c.typecPowerRole + (c.typecDataRole ? "  ·  " + c.typecDataRole : "")) : "—")
            ]})
    }
    var ctrls = d.controllers || [], crows = []
    for (var i = 0; i < ctrls.length; ++i)
        crows.push(row(ctrls[i].product || qsTr("Host controller"), ctrls[i].speed))
    s.push({ title: qsTr("USB host controllers"),
             note: qsTr("The SoC's integrated USB (dwc3/xHCI); root hubs are shown here."),
             rows: crows.length ? crows : [ row(qsTr("Controllers"), qsTr("none")) ] })

    var devs = d.devices || []
    if (!devs.length) {
        s.push({ title: qsTr("Connected devices"), rows: [ row(qsTr("Devices"), qsTr("none connected")) ] })
    } else {
        var devSecs = []
        for (var j = 0; j < devs.length; ++j) {
            var v = devs[j]
            var title = v.product || v.productName || (v.vid + ":" + v.pid)
            var drows = [
                row(qsTr("Product"), v.product || v.productName),
                row(qsTr("Manufacturer"), v.manufacturer || v.vendorName),
                row(qsTr("Vendor (USB-ID DB)"), v.vendorName),
                row(qsTr("Product (USB-ID DB)"), v.productName),
                row(qsTr("USB ID"), v.idPair, {mono:true}),
                row(qsTr("Serial"), v.serial, {mono:true}),
                row(qsTr("Class"), v.class),
                row(qsTr("Speed"), v.speed),
                row(qsTr("Max power"), v.maxPower),
                row(qsTr("USB version"), v.version),
                row(qsTr("Bus / device"), v.busnum + " / " + v.devnum),
                row(qsTr("Driver"), v.driver)
            ]
            var nodes = v.nodes || []
            for (var k = 0; k < nodes.length; ++k) {
                var nd = nodes[k]
                if (nd.subsystem === "tty")
                    drows.push(row(qsTr("Serial port"), nd.node, {mono:true, color:"#31e0a0"}))
                else if (nd.subsystem === "block")
                    drows.push(row(qsTr("Storage node"), nd.node + (nd.mount ? "  ·  " + qsTr("mounted at %1").arg(nd.mount) : "  ·  " + qsTr("not mounted")), {mono:true, color:"#31e0a0"}))
                else if (nd.subsystem === "net")
                    drows.push(row(qsTr("Network interface"), nd.name, {mono:true, color:"#31e0a0"}))
                else if (nd.subsystem === "video4linux")
                    drows.push(row(qsTr("Video node"), nd.node, {mono:true, color:"#31e0a0"}))
                else if (nd.subsystem === "hidraw")
                    drows.push(row(qsTr("HID node"), nd.node, {mono:true}))
                else if (nd.subsystem === "input")
                    drows.push(row(qsTr("Input node"), nd.node, {mono:true}))
            }
            if (!nodes.length)
                drows.push(row(qsTr("Device nodes"), qsTr("none exposed")))
            devSecs.push({ title: title, rows: drows })
        }
        s = s.concat(group(devSecs, "usbdev", qsTr("Connected devices"),
            qsTr("What the bus enumerated, one entry per device — a hub counts as one of them, and so does every function a composite device registers.")))
    }
    return { title: qsTr("USB"), helpTopics: ["usb","raw","firmware"], sections: s.concat(catSections("expansion")).concat(fwUsb()).concat(fwModules(["usb","tcpc","typec","extcon","xhci","dwc3","musb"])).concat(dtSections("usb typec tcpc", qsTr("Device tree — USB"))).concat(rawSections("usb")) }
}

// Turn raw charger kernel-log lines into a plain-language timeline.
function humanizeChargerLog(lines) {
    function classify(msg) {
        var m = msg.toLowerCase()
        if (m.indexOf("type-c none") >= 0 || m.indexOf("typec none") >= 0)
            return { text: qsTr("Cable disconnected (Type-C removed)"), color: "#ffb44a" }
        if (m.indexOf("typec-attach") >= 0 || (m.indexOf("type-c") >= 0 && m.indexOf("detected") >= 0 && m.indexOf("none") < 0))
            return { text: qsTr("Cable attached (Type-C)"), color: "#31e0a0" }
        if (m.indexOf("usbin-collapse") >= 0)
            return { text: qsTr("Input voltage collapsed — charger current limit reached"), color: "#ffb44a" }
        if (m.indexOf("aicl-done") >= 0)
            return { text: qsTr("Input-current detection finished (AICL)") }
        var icl = msg.match(/icl_settled=(\d+)/)
        if (icl)
            return { text: qsTr("Input current limit set to %1 A").arg((parseInt(icl[1]) / 1e6).toFixed(2)), color: "#31e0a0" }
        if (m.indexOf("input-current-limiting") >= 0)
            return { text: qsTr("Adjusting input current limit (AICL)") }
        if (m.indexOf("apsd") >= 0 || m.indexOf("real_charger") >= 0)
            return { text: qsTr("Charger type detected") }
        if (m.indexOf("hvdcp") >= 0)
            return { text: qsTr("Quick Charge negotiation") }
        if (m.indexOf("pd hard") >= 0)
            return { text: qsTr("USB-PD hard reset"), color: "#ffb44a" }
        if (m.indexOf("pd_active") >= 0 || m.indexOf("usbpd") >= 0 || m.indexOf("pd_") >= 0)
            return { text: qsTr("USB Power Delivery negotiation") }
        return null
    }
    var out = [], t0 = null, lastText = null
    for (var i = 0; i < lines.length; ++i) {
        var c = classify(lines[i])
        if (!c || c.text === lastText) continue
        lastText = c.text
        var tm = lines[i].match(/\[\s*(\d+)\.(\d+)\]/)
        var tlabel = "—"
        if (tm) {
            var t = parseFloat(tm[1] + "." + tm[2])
            if (t0 === null) t0 = t
            tlabel = "+" + (t - t0).toFixed(1) + " s"
        }
        out.push({ t: tlabel, text: c.text, color: c.color })
    }
    if (out.length > 25) out = out.slice(out.length - 25)
    return out
}

function techName(t) {
    t = ("" + t).toLowerCase()
    if (t === "lte") return "4G / LTE"
    if (t === "umts" || t === "hspa") return "3G / UMTS"
    if (t === "gsm" || t === "edge") return "2G / GSM"
    if (t === "nr" || t === "5gnr") return "5G / NR"
    return t || "—"
}

function modem() {
    var d = sysmon.modemDetail()
    var s = []
    if (!d.present || !(d.modems && d.modems.length)) {
        s.push({ title: qsTr("Modem"),
            note: qsTr("No ofono modem is registered. Flight mode, or ofono is not running."),
            rows: [ row(qsTr("Status"), qsTr("unavailable")) ] })
        return { title: qsTr("Modem / SIM"), helpTopics: ["modem","raw","firmware"], sections: s.concat(fwModules(["ccci","md_","modem","mddp","dpmaif"])).concat(dtSections("modem ccci mddp md1", qsTr("Device tree — modem"))).concat(rawSections("modem")) }
    }
    var ms = d.modems
    for (var i = 0; i < ms.length; ++i) {
        var m = ms[i]
        var pfx = ms.length > 1 ? (qsTr("Modem %1").arg(i + 1) + " · ") : ""

        s.push({ title: pfx + qsTr("Modem"),
            rows: [
                row(qsTr("Manufacturer"), m.manufacturer),
                row(qsTr("Model"), m.model),
                row(qsTr("Firmware"), m.revision, {mono:true}),
                row("IMEI", m.serial, {mono:true}),
                row(qsTr("Power"), m.online ? qsTr("online") : (m.powered ? qsTr("powered, offline") : qsTr("off")),
                    {color: m.online ? undefined : "#d08770"})
            ]})

        var sim = m.sim
        if (sim) {
            s.push({ title: pfx + qsTr("SIM"),
                rows: [
                    row(qsTr("Present"), sim.present ? qsTr("yes") : qsTr("no"),
                        {color: sim.present ? undefined : "#d08770"}),
                    row(qsTr("Provider"), sim.spn),
                    row(qsTr("Phone number"), sim.number, {mono:true}),
                    row("IMSI", sim.imsi, {mono:true}),
                    row("ICCID", sim.iccid, {mono:true}),
                    row(qsTr("MCC / MNC"), sim.mcc && sim.mnc ? (sim.mcc + " / " + sim.mnc) : "—", {mono:true}),
                    row(qsTr("PIN lock"), sim.pin && sim.pin !== "none" ? sim.pin : qsTr("none"))
                ]})
        }

        var net = m.network
        if (net) {
            var reg = net.status === "registered" ? qsTr("registered")
                    : net.status === "searching" ? qsTr("searching")
                    : net.status === "denied" ? qsTr("denied") : net.status
            s.push({ title: pfx + qsTr("Network"),
                rows: [
                    row(qsTr("Operator"), net.name),
                    row(qsTr("Registration"), reg,
                        {color: net.status === "registered" ? undefined : "#d08770"}),
                    row(qsTr("Technology"), techName(net.tech)),
                    row(qsTr("Selection"), net.mode === "auto" ? qsTr("automatic") : net.mode),
                    row(qsTr("Signal"), (net.strength !== undefined ? net.strength + " %" : "—")),
                    row(qsTr("Cell ID"), net.cellId ? net.cellId : "—", {mono:true}),
                    row(qsTr("Area code (LAC/TAC)"), net.lac ? net.lac : "—", {mono:true}),
                    row(qsTr("MCC / MNC"), net.mcc && net.mnc ? (net.mcc + " / " + net.mnc) : "—", {mono:true})
                ]})
        }

        var data = m.data
        if (data) {
            s.push({ title: pfx + qsTr("Mobile data"),
                rows: [
                    row(qsTr("Attached"), data.attached ? qsTr("yes") : qsTr("no")),
                    row(qsTr("APN"), data.apn),
                    row(qsTr("Roaming"), data.roaming ? qsTr("allowed") : qsTr("blocked"))
                ]})
        }
    }
    return { title: qsTr("Modem / SIM"), helpTopics: ["modem","raw","firmware"], sections: s.concat(fwModules(["ccci","md_","modem","mddp","dpmaif"])).concat(dtSections("modem ccci mddp md1", qsTr("Device tree — modem"))).concat(rawSections("modem")) }
}

// ---- accumulated: what the kernel has been tallying all along --------------
// Every figure here is read once from a counter the system keeps anyway. The
// app writes nothing and remembers nothing between runs — which is also the
// limit of the section: it can show totals, never a history.
function sinceBoot() {
    var d = sysmon.sinceBootDetail()
    var s = []
    var up = d.uptimeSec > 0 ? d.uptimeSec : 0
    function pctOfUp(sec) { return up > 0 ? Math.round(100 * sec / up) + " %" : "" }
    function dur(sec) { return sysmon.fmtDuration(Math.round(sec)) }
    function withPct(sec) { var p = pctOfUp(sec); return dur(sec) + (p ? "  ·  " + p : "") }

    // --- times ------------------------------------------------------------
    var t = []
    if (up > 0) t.push(row(qsTr("Uptime"), dur(up)))
    if (d.awakeSec !== undefined) {
        t.push(row(qsTr("Awake"), withPct(d.awakeSec)))
        t.push(row(qsTr("Deep sleep"), withPct(up - d.awakeSec)))
    }
    if (d.screenSec !== undefined) {
        t.push(row(qsTr("Screen on"), withPct(d.screenSec)))
        if (d.screenCycles > 0) t.push(row(qsTr("Times switched on"), d.screenCycles))
        if (d.screenLongestSec > 0) t.push(row(qsTr("Longest session"), dur(d.screenLongestSec)))
        // The interesting residue: awake with nobody looking. Small is healthy;
        // large means something kept the phone up on its own.
        if (d.awakeSec !== undefined && d.awakeSec > d.screenSec)
            t.push(row(qsTr("Awake, screen off"), withPct(d.awakeSec - d.screenSec)))
    }
    if (t.length) s.push({ title: qsTr("Times"), rows: t })

    if (d.screenSec !== undefined) {
        s.push({ note: qsTr("Screen-on time is MCE's own tally, good to about 1 %: the counter is released a few seconds after the display goes dark, so every switch-off adds a little surplus. Time on the charger is counted in and cannot be separated out — the kernel keeps no cumulative charging time, and two totals never yield their overlap. Read it as usage, not as battery drain."),
                 rows: [] })
    }

    // --- suspend ----------------------------------------------------------
    if (d.suspend) {
        var su = d.suspend, sr = []
        var ok = parseFloat(su.success), bad = parseFloat(su.fail)
        // Result first, mechanism after. A five-figure count of aborted attempts
        // at the top of the section reads as a fault; it is the rhythm of the
        // autosleep loop, and the reader has to see the outcome before the tally.
        if (d.awakeSec !== undefined && up > d.awakeSec)
            sr.push(row(qsTr("Time asleep"),
                        sysmon.fmtDuration(Math.round(up - d.awakeSec))
                        + "  ·  " + Math.round(100 * (up - d.awakeSec) / up) + " " + qsTr("% of uptime")))
        if (!isNaN(ok)) {
            var avg = ""
            if (d.awakeSec !== undefined && ok > 0 && up > d.awakeSec)
                avg = "  ·  " + qsTr("~%1 s at a time").arg(((up - d.awakeSec) / ok).toFixed(1))
            sr.push(row(qsTr("Sleep mode entered"), qsTr("%1 ×").arg(ok) + avg))
        }
        // "Failed" is the kernel's word, not a verdict: something arrived and
        // interrupted the attempt, and the next one follows seconds later.
        if (!isNaN(bad)) {
            var rate = up > 0 ? "  ·  " + qsTr("~%1 / hour").arg(Math.round(bad / (up / 3600))) : ""
            sr.push(row(qsTr("Attempts interrupted"), qsTr("%1 ×").arg(bad) + rate))
        }
        var steps = [["failed_freeze", qsTr("blocked while freezing tasks")],
                     ["failed_prepare", qsTr("blocked while preparing")],
                     ["failed_suspend", qsTr("blocked by a driver")],
                     ["failed_suspend_late", qsTr("blocked late in suspend")],
                     ["failed_suspend_noirq", qsTr("blocked with interrupts off")],
                     ["failed_resume", qsTr("failed on the way back up")]]
        for (var i = 0; i < steps.length; ++i) {
            var v = parseFloat(su[steps[i][0]])
            if (!isNaN(v) && v > 0) sr.push(row(steps[i][1], v))
        }
        if (su.last_failed_dev) sr.push(row(qsTr("Last blocked by"), su.last_failed_dev, {mono:true}))
        if (su.last_failed_step) sr.push(row(qsTr("Stopped at step"), su.last_failed_step, {mono:true}))
        if (su.last_failed_errno) {
            var en = parseFloat(su.last_failed_errno)
            var meaning = en === -16 ? qsTr("device busy") : en === -11 ? qsTr("try again") : ""
            sr.push(row(qsTr("Error code"), su.last_failed_errno + (meaning ? "  ·  " + meaning : "")))
        }
        s.push({ title: qsTr("Sleep mode"), rows: sr })
        s.push({ note: qsTr("Nothing is broken when attempts are interrupted — that is how the phone works. Whenever nothing holds it awake it halts, freezes the processes and asks every driver for permission; anything arriving in between interrupts the attempt, and the next one follows seconds later. Hundreds an hour are the rhythm of that loop, and the kernel files them under \"failed\", which is its word and not a verdict. What is worth reading is the time asleep at the top, how long it stays down each time, and the device and step named below — that is who was still busy at the last attempt. The per-step figures need not add up to the total exactly; some aborts belong to no single step. Note that the average is an average: long spells at night and second-long ones during the day both feed it."),
                 rows: [] })
    }

    // --- wake sources -----------------------------------------------------
    if (d.wakers && d.wakers.length) {
        var wr = []
        for (var w = 0; w < d.wakers.length && w < 12; ++w) {
            var k = d.wakers[w]
            // Count on the left, held time on the right: as one string the two
            // figures ran together and no column lined up.
            wr.push(row(k.name, qsTr("%1 ×").arg(k.count),
                        {mono:true, right: k.heldSec > 1 ? dur(k.heldSec) : ""}))
        }
        s.push({ title: qsTr("What wakes the phone"), rows: wr })
        s.push({ note: qsTr("How often each source signalled a wakeup, and how long it held the system awake in total. A high count is not automatically bad — the clock ticking and the modem receiving are what a phone does. It becomes interesting when a single source dominates and deep sleep is short."),
                 rows: [] })
    }

    if (!s.length)
        s.push({ note: qsTr("This kernel exposes none of the accumulated counters."), rows: [] })

    return { title: qsTr("Since boot"), helpTopics: ["sinceboot"], sections: s }
}
