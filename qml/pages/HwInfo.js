// Builders that turn the sysmon/bt getters into InfoDetailPage sections.
// Non-library JS: shares the importing component's scope (sysmon, bt, qsTr).

function row(k, v, opt) {
    var r = { k: k, v: (v === undefined || v === null || v === "") ? "—" : ("" + v) }
    if (opt) { if (opt.mono) r.mono = true; if (opt.active === false) r.active = false; if (opt.color) r.color = opt.color }
    return r
}

function cpu() {
    var d = sysmon.cpuDetail()
    var s = []
    s.push({ title: qsTr("Device"), rows: [
        row(qsTr("Product"), d.deviceName),
        row(qsTr("Model"), d.deviceModel),
        row(qsTr("Board"), d.machine),
        row("SoC", d.socName, {mono:true})
    ]})
    s.push({ title: qsTr("Operating system"), rows: [
        row("Sailfish OS", d.os),
        row(qsTr("Release"), d.osVersion),
        row(qsTr("HW adaptation"), d.hwVersion),
        row(qsTr("Kernel"), d.kernel, {mono:true}),
        row(qsTr("Kernel build"), d.kernelVersion, {mono:true})
    ]})
    var coreRows = []
    var cores = d.cores || []
    for (var i = 0; i < cores.length; ++i) {
        var mhz = (sysmon.coreFreqsMhz.length > i) ? "  ·  " + sysmon.coreFreqsMhz[i] + " MHz" : ""
        coreRows.push(row(qsTr("Core %1").arg(i), (cores[i].name || cores[i].part || "?") + mhz))
    }
    s.push({ title: qsTr("Processor"), rows: [
        row(qsTr("Architecture"), "ARMv" + d.architecture),
        row(qsTr("Cores"), d.count),
        row("Hardware", d.hardware)
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
    if (fr.length)
        s.push({ title: qsTr("Frequency steps"), rows: [ row(qsTr("Available"), fr.join(", ") + " MHz") ] })

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
    return { title: qsTr("System & CPU"), helpTopics: ["cpu","monitoring"], sections: s }
}

function gfx() {
    var d = sysmon.graphicsDetail()
    var s = []
    s.push({ title: qsTr("GPU"), rows: [
        row(qsTr("Model"), d.gpuModel),
        row(qsTr("Driver"), d.driver),
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
    return { title: qsTr("Graphics"), helpTopics: [], sections: s }
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
    devRows.push(row(qsTr("Manufacturer"), qsTr("not exposed — JEDEC MR5, read by the bootloader into SMEM, not surfaced here"), {active:false}))
    devRows.push(row(qsTr("Organisation (ranks / channels / dies)"), qsTr("not exposed — a JEDEC/datasheet property of the die (MR5–MR8), not a runtime register here"), {active:false}))
    sections.push({ title: qsTr("Memory device"),
        note: qsTr("The DRAM type is read from the bootloader-populated device tree. The chip's maker and internal organisation are not exposed to software on this platform."),
        rows: devRows })

    // physical memory map (address regions the kernel sees — not the die layout)
    var regs = d.regions || []
    if (regs.length) {
        var rrows = []
        for (var r = 0; r < regs.length; ++r)
            rrows.push(row("0x" + regs[r].base.toString(16), sysmon.fmtBytes(regs[r].size), {mono:true}))
        sections.push({ title: qsTr("Physical memory map"),
            note: qsTr("The address regions the kernel maps, carved around reserved firmware areas — this is the address layout, not the chip's rank/channel structure."),
            rows: rrows })
    }

    sections.push({ title: qsTr("meminfo (full)"), rows: rows })
    return { title: qsTr("RAM"), helpTopics: ["mem"], sections: sections }
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
        if (main.healthVerdict) {
            var vtxt = main.healthVerdict === "good" ? qsTr("good") : main.healthVerdict === "warning" ? qsTr("warning") : qsTr("urgent")
            mrows.push(row(qsTr("Wear"), qsTr("~%1 % life used").arg(main.lifeUsedPct)))
            mrows.push(row(qsTr("Health"), vtxt,
                {color: main.healthVerdict === "good" ? "#31e0a0" : main.healthVerdict === "warning" ? "#ffb44a" : "#ff5a52"}))
        }
        s.push({ title: qsTr("Internal storage (UFS)"), rows: mrows })

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
        s.push({ title: qsTr("Capacity composition"),
            note: qsTr("A single UFS package, not multiple cards. The controller presents it as %1 data LUN(s): one large user area plus tiny boot LUNs, plus %2. There is no software-visible “2×64” die split — the flash dies sit behind the controller.")
                    .arg(main.numLuns || ufs.length).arg(main.numWluns ? qsTr("well-known LUNs (RPMB etc.)") : qsTr("well-known LUNs")),
            rows: crows })

        s.push({ title: qsTr("Raw vs usable"),
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
        if (h.healthVerdict) {
            var vt = h.healthVerdict === "good" ? qsTr("good") : h.healthVerdict === "warning" ? qsTr("warning") : qsTr("urgent")
            rows.push(row(qsTr("Wear"), qsTr("~%1 % life used").arg(h.lifeUsedPct)))
            rows.push(row(qsTr("Health"), vt,
                {color: h.healthVerdict === "good" ? "#31e0a0" : h.healthVerdict === "warning" ? "#ffb44a" : "#ff5a52"}))
        } else {
            rows.push(row(qsTr("Health"), qsTr("not reported by device")))
        }
        if (h.hostNode || h.hostDriver)
            rows.push(row(qsTr("Card reader"), (h.hostDriver || "") + (h.hostNode ? "  ·  " + h.hostNode : ""), {mono:true}))
        var isSd = ("" + h.bus).toUpperCase().indexOf("SD") >= 0
        s.push({ title: isSd ? qsTr("microSD card") : (h.bus + " " + h.dev), rows: rows })
    }

    var bars = []
    for (var m = 0; m < mounts.length; ++m) {
        var mn = mounts[m]
        bars.push({ label: mn.mount + "  (" + mn.fstype + ")", value: mn.used, max: mn.total,
                    caption: sysmon.fmtBytes(mn.used) + " / " + sysmon.fmtBytes(mn.total),
                    color: mn.pct > 90 ? "#ff5a52" : mn.pct > 75 ? "#ffb44a" : "#31e0a0" })
    }
    s.push({ title: qsTr("Partitions"), bars: bars, rows: [] })
    return { title: qsTr("Storage"), helpTopics: ["storage"], sections: s }
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

    if (wifi.iface !== undefined) {
        if (wifi.connected) {
            s.push({ title: qsTr("Wi-Fi connection"), rows: [
                row(qsTr("SSID"), wifi.ssid),
                row(qsTr("Own MAC"), wifi.mac, {mono:true}),
                row(qsTr("Access point (BSSID)"), wifi.bssid, {mono:true}),
                row(qsTr("Band"), wifi.band),
                row(qsTr("Channel"), (wifi.channel ? wifi.channel : "?") + (wifi.freqMhz ? "  (" + wifi.freqMhz + " MHz)" : "")),
                row(qsTr("Signal"), wifi.signalDbm !== undefined ? wifi.signalDbm + " dBm" : "—"),
                row(qsTr("TX rate"), wifi.txBitrate),
                row(qsTr("RX rate"), wifi.rxBitrate),
                row(qsTr("TX power"), wifi.txpower)
            ]})
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
            s.push({ title: qsTr("Band: %1").arg(bd.name),
                     note: qsTr("Capabilities of the Wi-Fi chip on this band; blocked channels are grayed."),
                     rows: brows })
        }
    }

    for (var i = 0; i < nics.length; ++i) {
        var n = nics[i]
        var rows = [
            row(qsTr("Type"), n.kind),
            row(qsTr("State"), n.state + (n.carrier ? " · " + qsTr("carrier") : "")),
            row("MAC", n.mac, {mono:true}),
            row("MTU", n.mtu),
            row(qsTr("Driver"), n.driver),
            row(qsTr("Chip"), (n.vendor ? n.vendor : "") + (n.model ? " " + n.model : ""), {mono:true})
        ]
        if (n.speedMbit) rows.push(row(qsTr("Link speed"), n.speedMbit + " Mbit/s"))
        rows.push(row(qsTr("Traffic"), "↓ " + sysmon.fmtBytes(n.rxBytes) + "   ↑ " + sysmon.fmtBytes(n.txBytes)))
        if (n.rxErrors || n.txErrors) rows.push(row(qsTr("Errors"), "rx " + n.rxErrors + " · tx " + n.txErrors))
        s.push({ title: n.iface, rows: rows })
    }
    return { title: qsTr("Network"), helpTopics: ["conn"], sections: s }
}

function batt() {
    var h = sysmon.batteryHardware()
    var c = sysmon.chargerDetail()
    var sections = []

    if (c.online) {
        var crows = [
            row(qsTr("Status"), c.status),
            row(qsTr("Protocol"), c.protocol + (c.typeRaw ? "  (" + c.typeRaw + ")" : "")),
            row(qsTr("Charge type"), c.chargeType),
            row(qsTr("Charging power"), c.chargePower !== undefined ? c.chargePower.toFixed(1) + " W" : "—", {color:"#8ef94a"}),
            row(qsTr("Into battery"), (c.chargeCurrent !== undefined ? (c.chargeCurrent*1000).toFixed(0) + " mA" : "—")
                + (c.batteryVoltage ? "  @ " + c.batteryVoltage.toFixed(2) + " V" : "")),
            row(qsTr("Input"), (c.inputVoltage ? c.inputVoltage.toFixed(2) + " V" : "—")
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
                sections.push({ title: qsTr("Kernel log (raw excerpt)"), rows: hrows })
        } else {
            sections.push({ title: qsTr("Charger handshake"),
                rows: [ row(qsTr("Log"), qsTr("Root mode required — start the helper to read the kernel charger log.")) ] })
        }
    }

    sections.push({ title: qsTr("Identity"), rows: [
        row(qsTr("Supply"), h.supply, {mono:true}),
        row(qsTr("Manufacturer"), h.manufacturer),
        row(qsTr("Model"), h.model),
        row(qsTr("Serial"), h.serial, {mono:true}),
        row(qsTr("Technology"), h.technology)
    ]})
    sections.push({ title: qsTr("Capacity & health"), rows: [
        row(qsTr("Design capacity"), (h.designCapacity ? h.designCapacity.toFixed(0) + " " + h.capacityUnit : "—")),
        row(qsTr("Full capacity"), (h.fullCapacity ? h.fullCapacity.toFixed(0) + " " + h.capacityUnit : "—")),
        row(qsTr("State of health"), sysmon.battHealthPct >= 0 ? sysmon.battHealthPct + " %" : "—",
            {color: sysmon.battHealthPct >= 80 ? "#8ef94a" : sysmon.battHealthPct >= 65 ? "#ffb44a" : "#ff5a52"}),
        row(qsTr("Charge cycles"), h.cycles),
        row(qsTr("Design voltage"), h.voltageDesign ? h.voltageDesign.toFixed(2) + " V" : "—"),
        row(qsTr("Driver health"), h.health)
    ]})
    sections.push({ title: qsTr("Live"), rows: [
        row(qsTr("Level"), sysmon.battCapacity + " %  ·  " + sysmon.battStatus),
        row(qsTr("Voltage"), sysmon.battVoltageV.toFixed(3) + " V"),
        row(qsTr("Current"), (sysmon.battCurrentA * 1000).toFixed(0) + " mA"),
        row(qsTr("Power"), sysmon.battPowerW.toFixed(2) + " W"),
        row(qsTr("Temperature"), sysmon.battTempC.toFixed(1) + " °C")
    ]})

    var top = []
    try { if (typeof procs !== "undefined" && procs.topByCpu) top = procs.topByCpu(10) } catch (eP) { top = [] }
    var trows = []
    for (var i = 0; i < top.length; ++i)
        trows.push(row((i + 1) + ". " + top[i].name + "  (" + top[i].pid + ")",
                       top[i].cpu.toFixed(1) + " %  ·  " + sysmon.fmtBytes(top[i].mem)))
    if (!trows.length) trows.push(row(qsTr("Processes"), qsTr("none")))
    sections.push({ title: qsTr("Top consumers (CPU)"),
        note: qsTr("CPU time is the dominant battery drain — this ranks current CPU use. An estimate, not a per-app power meter."),
        rows: trows })

    return { title: qsTr("Battery"), helpTopics: ["battery","usb"], sections: sections }
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
    // capability-style: powers/modes the adapter has; grayed when off
    s.push({ title: qsTr("State"),
        note: qsTr("Adapter capabilities; grayed ones are supported but currently off."),
        rows: [
            row(qsTr("Powered"), a.powered ? qsTr("on") : qsTr("off"), { active: a.powered === true }),
            row(qsTr("Discoverable"), a.discoverable ? qsTr("on") : qsTr("off"), { active: a.discoverable === true }),
            row(qsTr("Pairable"), a.pairable ? qsTr("on") : qsTr("off"), { active: a.pairable === true }),
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
    return { title: qsTr("Bluetooth"), helpTopics: [], sections: s }
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

    return { title: qsTr("Audio"), helpTopics: [], sections: s }
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
        s.push({ title: qsTr("CAMSS V4L2 nodes"),
            note: qsTr("Kernel camera-subsystem interfaces — not user-facing cameras."), rows: nrows })
    return { title: qsTr("Camera"), helpTopics: ["camera"], sections: s }
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
            s.push({ title: title, rows: drows })
        }
    }
    return { title: qsTr("USB"), helpTopics: ["usb"], sections: s }
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
        return { title: qsTr("Modem / SIM"), helpTopics: ["modem"], sections: s }
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
    return { title: qsTr("Modem / SIM"), helpTopics: ["modem"], sections: s }
}
