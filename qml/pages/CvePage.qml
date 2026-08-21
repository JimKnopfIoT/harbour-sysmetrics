// CVE search for this device's own stack (Ultimate builds only). Search terms
// are seeded from the live system identity — kernel, SoC, Android base — and
// stay editable: vulnerability databases index components under many names.
import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: cvePage
    allowedOrientations: Orientation.All

    // The subsystem this page was opened from; presets follow it so a search
    // from the Network page targets the radio chip and its stack, not Android
    // in general. The search field itself always starts empty.
    property string topic: ""

    property var _d: sysmon.cpuDetail()

    // Local fix evidence for the package behind the active search (rpm
    // changelog + build time); empty when no package is associated.
    property var fixInfo: ({})
    function startSearch(q, pkg) {
        fixInfo = cve.packageFixInfo(pkg && pkg.length ? pkg : q)
        cve.search(q)
    }
    // leading dotted-numeric part of an rpm version ("1.32+git32.5-1.5.1" → "1.32")
    function upstreamVersion(v) {
        var m = /^(\d+(?:\.\d+)*)/.exec(v || "")
        return m ? m[1] : ""
    }
    function cmpVer(a, b) {
        var A = a.split("."), B = b.split(".")
        for (var i = 0; i < Math.max(A.length, B.length); ++i) {
            var x = parseInt(A[i] || "0"), y = parseInt(B[i] || "0")
            if (x !== y) return x < y ? -1 : 1
        }
        return 0
    }
    // Affected upper bound stated in the CVE text ("before 1.33", "through
    // 1.32", "1.32 and earlier", "up to and including 1.32"). Returns
    // { bound, inclusive } or null when the text names no range.
    function affectedRange(summary) {
        var s = summary || ""
        var m = /(?:before|prior to)\s+v?(\d+(?:\.\d+)+)/i.exec(s)
        if (m) return { bound: m[1], inclusive: false }
        m = /(?:through|up to(?: and including)?)\s+v?(\d+(?:\.\d+)+)/i.exec(s)
        if (m) return { bound: m[1], inclusive: true }
        m = /v?(\d+(?:\.\d+)+)\s+and (?:earlier|below)/i.exec(s)
        if (m) return { bound: m[1], inclusive: true }
        return null
    }
    // fixed: CVE id named in the installed package's changelog, or our
    //        installed version lies above the affected range the CVE names.
    // open:  our version falls inside the stated affected range, or the
    //        package was built before the CVE was published (fix cannot be
    //        inside) — "probably affected", backports without a changelog
    //        entry can still exist.
    // unknown: no package context or no evidence either way.
    function fixState(id, published, summary) {
        var fi = fixInfo
        if (!fi || !fi.installed) return "unknown"
        if (fi.cves && fi.cves.indexOf(id) >= 0) return "fixed"
        var ver = upstreamVersion(fi.version)
        var r = affectedRange(summary)
        if (ver && r) {
            var c = cmpVer(ver, r.bound)
            if (c > 0 || (c === 0 && !r.inclusive)) return "fixed"
            return "open"
        }
        if (published && fi.buildTime) {
            var pd = new Date(published)
            if (!isNaN(pd.getTime()) && (pd.getTime() / 1000) > fi.buildTime) return "open"
        }
        return "unknown"
    }

    // Always open empty — never show a previous context's query.
    Component.onCompleted: cve.reset()

    // first DT compatible entry → bare part name ("qca,wcn3990-wifi" → wcn3990)
    function chipPart(compat) {
        if (!compat || !compat.length) return ""
        var first = compat.split(", ")[0]
        var part = first.indexOf(",") >= 0 ? first.split(",")[1] : first
        return part.replace(/-wifi$|-bt$/, "")
    }

    // one-tap query presets: derived from the device, scoped to the topic
    function presets() {
        var p = []
        var kern = (_d.kernel || "").split(".").slice(0, 2).join(".")
        if (topic === "network") {
            var w = sysmon.wirelessDetail()
            var chip = chipPart(w.wlanDtCompatible || w.btDtCompatible)
            if (chip) p.push({ label: chip, q: chip })
            if (w.wlanDriver) p.push({ label: w.wlanDriver, q: w.wlanDriver })
            p.push({ label: "wpa_supplicant", q: "wpa_supplicant", pkg: "wpa_supplicant" })
            p.push({ label: "ConnMan", q: "ConnMan", pkg: "connman" })
        } else if (topic === "bluetooth") {
            var wb = sysmon.wirelessDetail()
            var bchip = chipPart(wb.btDtCompatible || wb.wlanDtCompatible)
            if (bchip) p.push({ label: bchip, q: bchip })
            p.push({ label: "BlueZ", q: "BlueZ", pkg: "bluez5" })
            if (kern.length) p.push({ label: qsTr("Kernel BT"), q: "Linux kernel " + kern + " Bluetooth" })
        } else if (topic === "gpu") {
            var g = sysmon.graphicsDetail()
            if (g.gpuModel && g.gpuModel.length) p.push({ label: g.gpuModel, q: g.gpuModel })
            var gdrv = g.gpuDriver || g.driver || ""
            if (gdrv.length) p.push({ label: gdrv, q: gdrv + " GPU driver" })
            p.push({ label: "Mesa", q: "Mesa", pkg: "mesa-llvmpipe" })
        } else if (topic === "camera") {
            var c = sysmon.cameraDetail()
            if (c.platform === "qualcomm") p.push({ label: "CamX", q: "Qualcomm CamX camera" })
            if (c.platform === "mediatek") p.push({ label: "mtkcam", q: "MediaTek camera driver" })
            var cams = c.cameras || []
            for (var ci = 0; ci < cams.length && ci < 2; ++ci)
                if (cams[ci].model) p.push({ label: cams[ci].model, q: (cams[ci].maker || "") + " " + cams[ci].model })
        } else if (topic === "audio") {
            p.push({ label: "PulseAudio", q: "PulseAudio", pkg: "pulseaudio" })
            if (kern.length) p.push({ label: "ALSA", q: "Linux kernel " + kern + " ALSA" })
        } else { // cpu / system
            if (kern.length) p.push({ label: qsTr("Kernel %1").arg(kern), q: "Linux kernel " + kern })
            // SoC: the part number CVE texts actually use (e.g. "SM6350"),
            // never raw device-tree strings like "somc,pdx213"
            if (_d.socModel && _d.socModel.length)
                p.push({ label: "SoC", q: _d.socModel.split(" ")[0] })
            else if (_d.socName && _d.socName.length && _d.socName.indexOf(",") < 0)
                p.push({ label: "SoC", q: _d.socName })
            if (_d.hardware && _d.hardware.length && _d.hardware !== _d.socName)
                p.push({ label: "Hardware", q: _d.hardware })
            if (_d.androidVersion && _d.androidVersion.length)
                p.push({ label: "Android", q: "Android " + _d.androidVersion })
            if (_d.osVersion && _d.osVersion.length)
                p.push({ label: "Sailfish", q: "Sailfish OS" })
        }
        return p
    }

    // Context note shown above the presets: says when the subsystem has no
    // chip of its own to search for (combo/SoC-integrated — covered by the
    // SoC search under System & CPU).
    function contextNote() {
        if (topic === "audio")
            return qsTr("No dedicated audio chip to search for — the codec is integrated in the SoC/PMIC; chip-level CVEs are covered by the SoC search under System & CPU. Below: the audio software stack only.")
        return ""
    }

    function sevColor(s) {
        var u = ("" + s).toUpperCase()
        if (u === "CRITICAL") return Diag.red
        if (u === "HIGH") return "#f44336"
        if (u === "MEDIUM") return Diag.amber
        if (u === "LOW") return Diag.green
        return Theme.secondaryColor
    }

    DiagBackground {}

    SilicaListView {
        anchors.fill: parent
        model: cve.results

        header: Column {
            width: cvePage.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("CVE search")
                description: qsTr("EUVD (ENISA) · flagged against CISA KEV")
            }

            SearchField {
                id: search
                width: parent.width
                placeholderText: qsTr("Component + version")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: cvePage.startSearch(text, text)
            }

            Label {
                visible: cvePage.contextNote().length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: cvePage.contextNote()
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }

            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Repeater {
                    model: cvePage.presets()
                    delegate: BackgroundItem {
                        width: chipLbl.width + Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall * 0.8
                        Rectangle {
                            anchors.fill: parent
                            radius: height / 4
                            color: Theme.rgba(Theme.highlightBackgroundColor, 0.35)
                        }
                        Label {
                            id: chipLbl
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        onClicked: { search.text = modelData.q; cvePage.startSearch(modelData.q, modelData.pkg || "") }
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: cve.busy ? Theme.highlightColor : Theme.secondaryColor
                text: cve.status
            }
            BusyIndicator { running: cve.busy; anchors.horizontalCenter: parent.horizontalCenter }

            // legend for the per-CVE fix state on THIS system
            Label {
                visible: cve.results.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                text: (cvePage.fixInfo && cvePage.fixInfo.installed
                       ? qsTr("Fix state vs. installed %1 %2:  ").arg(cvePage.fixInfo.package).arg(cvePage.fixInfo.version)
                       : qsTr("Fix state:  "))
                      + qsTr("✔ not affected or fixed (changelog / installed version above the stated range) · ✘ probably affected (version inside the range, or package older than the CVE) · ▢ unknown")
            }
        }

        delegate: ListItem {
            id: del
            width: ListView.view.width
            contentHeight: Theme.itemSizeLarge
            onClicked: Qt.openUrlExternally(modelData.url)

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Open on NVD")
                    onClicked: Qt.openUrlExternally(modelData.url)
                }
                MenuItem {
                    text: qsTr("Search Exploit-DB")
                    onClicked: Qt.openUrlExternally(modelData.exploitdb)
                }
            }

            Row {
                anchors {
                    left: parent.left; right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.paddingMedium

                // fix state on THIS system: ✔ fixed / ✘ cannot be inside / ▢ unknown
                Label {
                    id: fixMark
                    anchors.verticalCenter: parent.verticalCenter
                    property string st: cvePage.fixState(modelData.id, modelData.published, modelData.summary)
                    text: st === "fixed" ? "✔" : st === "open" ? "✘" : "▢"
                    font.pixelSize: Theme.fontSizeMedium
                    font.bold: true
                    color: st === "fixed" ? Diag.green : st === "open" ? Diag.red : Theme.secondaryColor
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeExtraSmall * 0.9
                    height: width; radius: Theme.paddingSmall / 2
                    color: sevColor(modelData.severity)
                    Label {
                        anchors.centerIn: parent
                        text: modelData.score >= 0 ? ("" + modelData.score) : "?"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true; color: "white"
                    }
                }
                Column {
                    width: parent.width - Theme.itemSizeExtraSmall * 0.9
                           - fixMark.width - 2 * Theme.paddingMedium
                    Row {
                        spacing: Theme.paddingSmall
                        Label {
                            text: modelData.id + "  ·  " + modelData.severity
                            font.pixelSize: Theme.fontSizeSmall
                            color: del.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            visible: modelData.kev === true
                            text: "⚠ KEV"
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                            color: Diag.red
                        }
                    }
                    Label {
                        width: parent.width
                        text: modelData.summary
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        truncationMode: TruncationMode.Elide
                    }
                }
            }
        }

        // no ViewPlaceholder: the header's status label already says it —
        // a second, bold copy of the same sentence is noise
        VerticalScrollDecorator {}
    }
}
