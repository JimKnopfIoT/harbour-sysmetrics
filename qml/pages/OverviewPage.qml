import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sysmetrics 1.0
import "../components"
import "HwInfo.js" as HwInfo

Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["cpu","mem","ioenergy","thermal","battery","monitoring"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) _attachHelp()

    property bool thermalExpanded: false
    property bool thermalSuspect: {
        for (var i = 0; i < sysmon.thermalZones.length; ++i)
            if (sysmon.thermalZones[i].suspect) return true
        return false
    }
    property bool thermalCorrected: {
        for (var i = 0; i < sysmon.thermalZones.length; ++i)
            if (sysmon.thermalZones[i].corrected) return true
        return false
    }
    property bool ifaceExpanded: false
    readonly property int listCap: 10

    function openDetail(d) {
        pageStack.push(Qt.resolvedUrl("InfoDetailPage.qml"),
                       { title: d.title, sections: d.sections,
                         helpTopics: (d.helpTopics ? d.helpTopics : []),
                         diagTopic: (d.diagTopic ? d.diagTopic : "") })
    }

    // worst diagnostics level per topic — feeds the dots on the cards
    property var diagLevels: ({})

    Component.onCompleted: {
        bt.refresh()
        var all = diagnostics.run(sysmon.cpuPercent, sysmon.load1)
        var lv = {}
        for (var i = 0; i < all.length; ++i)
            if (all[i].level > (lv[all[i].topic] || 0)) lv[all[i].topic] = all[i].level
        diagLevels = lv
    }
    Timer { interval: 5000; running: true; repeat: true; onTriggered: bt.refresh() }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingLarge

            PageHeader { title: qsTr("System overview") }

            // ---- CPU ---------------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Processor")
                value: Math.round(sysmon.cpuPercent)
                unit: "%"
                accent: Diag.loadColor(sysmon.cpuPercent)
                drilldown: true
                diagLevel: page.diagLevels["cpu"] || 0
                onClicked: page.openDetail(HwInfo.cpu())
                Column {
                    width: parent.width
                    spacing: Theme.paddingSmall
                    HistoryGraph {
                        width: parent.width; height: Theme.itemSizeMedium
                        values: sysmon.cpuHistory; maxValue: 100
                        lineColor: Diag.loadColor(sysmon.cpuPercent)
                        fillColor: Qt.rgba(Diag.cyan.r, Diag.cyan.g, Diag.cyan.b, 0.16)
                        gridColor: Diag.grid
                    }
                    Grid {
                        width: parent.width; columns: 2
                        columnSpacing: Theme.paddingLarge; rowSpacing: Theme.paddingSmall
                        Repeater {
                            model: sysmon.corePercents
                            LoadBar {
                                width: (parent.width - Theme.paddingLarge) / 2
                                // -1 marks a parked core, see MainPage
                                value: modelData < 0 ? 0 : modelData
                                color: modelData < 0 ? Theme.secondaryColor
                                                     : Diag.loadColor(modelData)
                                label: qsTr("core %1").arg(index)
                                caption: modelData < 0 ? qsTr("offline")
                                         : Math.round(modelData) + "%"
                                           + (sysmon.coreFreqsMhz[index] > 0
                                              ? " · " + sysmon.coreFreqsMhz[index] + " MHz" : "")
                            }
                        }
                    }
                    KeyValue { label: qsTr("Load 1/5/15"); value:
                        sysmon.load1.toFixed(2) + " / " + sysmon.load5.toFixed(2)
                        + " / " + sysmon.load15.toFixed(2) }
                    KeyValue { label: qsTr("Runnable"); value: sysmon.runnable + "" }
                    KeyValue { label: qsTr("Uptime"); value: sysmon.fmtDuration(sysmon.uptimeSec) }
                }
            }

            // ---- Memory ------------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("RAM")
                value: sysmon.memTotal > 0 ? Math.round(100 * sysmon.memUsed / sysmon.memTotal) : 0
                unit: "%"
                note: "·  " + sysmon.fmtBytes(sysmon.memUsed) + " / " + sysmon.fmtBytes(sysmon.memTotal)
                accent: Diag.teal
                drilldown: true
                onClicked: page.openDetail(HwInfo.mem())
                Column {
                    width: parent.width; spacing: Theme.paddingSmall
                    HistoryGraph {
                        width: parent.width; height: Theme.itemSizeSmall
                        values: sysmon.memHistory; maxValue: 100
                        lineColor: Diag.teal
                        fillColor: Qt.rgba(Diag.teal.r, Diag.teal.g, Diag.teal.b, 0.16)
                        gridColor: Diag.grid
                    }
                    KeyValue { label: qsTr("Available"); value: sysmon.fmtBytes(sysmon.memAvailable) }
                    KeyValue { label: qsTr("Cached"); value: sysmon.fmtBytes(sysmon.cached) }
                    LoadBar {
                        width: parent.width; visible: sysmon.swapTotal > 0
                        value: sysmon.swapUsed; maxValue: Math.max(1, sysmon.swapTotal)
                        color: Diag.violet
                        label: qsTr("Swap")
                        caption: sysmon.fmtBytes(sysmon.swapUsed) + " / " + sysmon.fmtBytes(sysmon.swapTotal)
                    }
                }
            }

            // ---- Network -----------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Network")
                // The headline says which interface carries the traffic, not a
                // rate: the rates are one line below, and repeating the download
                // figure here filled the most prominent spot with nothing new.
                value: sysmon.netIface.length ? sysmon.netIface : qsTr("none")
                note: sysmon.netIface.length ? "\u00b7  " + qsTr("default route")
                                             : "\u00b7  " + qsTr("no route out")
                accent: Diag.green
                drilldown: true
                diagLevel: page.diagLevels["network"] || 0
                onClicked: page.openDetail(HwInfo.net())
                Column {
                    width: parent.width; spacing: Theme.paddingSmall
                    Row {
                        width: parent.width; spacing: Theme.paddingMedium
                        HistoryGraph {
                            width: (parent.width - Theme.paddingMedium) / 2; height: Theme.itemSizeSmall
                            values: sysmon.rxHistory
                            lineColor: Diag.green
                            fillColor: Qt.rgba(Diag.green.r, Diag.green.g, Diag.green.b, 0.16)
                            gridColor: Diag.grid
                        }
                        HistoryGraph {
                            width: (parent.width - Theme.paddingMedium) / 2; height: Theme.itemSizeSmall
                            values: sysmon.txHistory
                            lineColor: Diag.amber
                            fillColor: Qt.rgba(Diag.amber.r, Diag.amber.g, Diag.amber.b, 0.16)
                            gridColor: Diag.grid
                        }
                    }
                    // Each figure sits under the graph it belongs to, in the
                    // same halves: the colour alone had to carry the pairing
                    // while both numbers stood together on the left.
                    Row {
                        width: parent.width
                        spacing: Theme.paddingMedium
                        Column {
                            width: (parent.width - Theme.paddingMedium) / 2
                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("Download")
                                font.pixelSize: Theme.fontSizeTiny
                                color: Theme.secondaryColor
                            }
                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: sysmon.fmtRate(sysmon.netRxRate)
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: Diag.green
                            }
                        }
                        Column {
                            width: (parent.width - Theme.paddingMedium) / 2
                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("Upload")
                                font.pixelSize: Theme.fontSizeTiny
                                color: Theme.secondaryColor
                            }
                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: sysmon.fmtRate(sysmon.netTxRate)
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: Diag.amber
                            }
                        }
                    }
                    // The two rows above are a rate and move with every sample;
                    // everything below is a running total and only ever grows.
                    // Nothing but the unit said so, which is why the changing
                    // figures read as erratic.
                    Label {
                        text: qsTr("Total per interface")
                        font.pixelSize: Theme.fontSizeTiny
                        font.letterSpacing: 1.2
                        color: Theme.secondaryHighlightColor
                        topPadding: Theme.paddingSmall
                    }
                    Repeater {
                        model: page.ifaceExpanded ? sysmon.interfaces
                                                  : sysmon.interfaces.slice(0, page.listCap)
                        // Same order as the two rows above: download, then upload.
                        KeyValue {
                            label: modelData.name
                            value: sysmon.fmtBytes(modelData.rx) + "  /  " + sysmon.fmtBytes(modelData.tx)
                        }
                    }
                    MoreToggle {
                        total: sysmon.interfaces.length; shown: page.listCap
                        expanded: page.ifaceExpanded
                        onToggle: page.ifaceExpanded = !page.ifaceExpanded
                    }
                }
            }

            // ---- Storage I/O -------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Storage I/O")
                value: sysmon.fmtRate(sysmon.diskReadRate + sysmon.diskWriteRate)
                accent: Diag.amber
                drilldown: true
                onClicked: page.openDetail(HwInfo.storage())
                KeyValue { label: qsTr("Read / Write"); value:
                    sysmon.fmtRate(sysmon.diskReadRate) + "  /  " + sysmon.fmtRate(sysmon.diskWriteRate) }
            }

            // ---- Graphics ----------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Graphics")
                accent: Diag.violet
                drilldown: true
                diagLevel: page.diagLevels["gpu"] || 0
                onClicked: page.openDetail(HwInfo.gfx())
                Label {
                    width: parent.width
                    text: qsTr("GPU, display — tap for details")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            // ---- Audio -------------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Audio")
                accent: Diag.teal
                drilldown: true
                diagLevel: page.diagLevels["audio"] || 0
                onClicked: page.openDetail(HwInfo.audio())
                Label {
                    width: parent.width
                    text: qsTr("Codec, connectors, streams — tap for details")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            // ---- Sensors -----------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Sensors & GPS")
                accent: Diag.green
                drilldown: true
                onClicked: pageStack.push(Qt.resolvedUrl("SensorsPage.qml"))
                Label {
                    width: parent.width
                    text: qsTr("Gyro, compass, light, GPS — tap for live values")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            // ---- Camera ------------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Camera")
                accent: Diag.violet
                drilldown: true
                diagLevel: page.diagLevels["camera"] || 0
                onClicked: page.openDetail(HwInfo.camera())
                Label {
                    width: parent.width
                    text: qsTr("Sensors, ISP, capture nodes — tap for details")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            // ---- USB ---------------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("USB")
                accent: Diag.cyan
                drilldown: true
                onClicked: page.openDetail(HwInfo.usb())
                Label {
                    width: parent.width
                    text: qsTr("Controller & connected devices — tap for details")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            // ---- Modem / SIM -------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                title: qsTr("Modem / SIM")
                accent: Diag.violet
                drilldown: true
                onClicked: page.openDetail(HwInfo.modem())
                Label {
                    width: parent.width
                    text: qsTr("Operator, SIM, signal, mobile data — tap for details")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }

            // ---- Battery -----------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                visible: sysmon.battCapacity >= 0
                title: qsTr("Battery")
                value: sysmon.battCapacity
                unit: "%"
                note: "·  " + sysmon.battStatus
                accent: sysmon.battCapacity < 20 ? Diag.red : Diag.green
                drilldown: true
                onClicked: page.openDetail(HwInfo.batt())
                Column {
                    width: parent.width; spacing: Theme.paddingSmall
                    HistoryGraph {
                        width: parent.width; height: Theme.itemSizeSmall
                        values: sysmon.battHistory; minSpan: 2
                        lineColor: Diag.amber
                        fillColor: Qt.rgba(Diag.amber.r, Diag.amber.g, Diag.amber.b, 0.14)
                        gridColor: Diag.grid
                    }
                    KeyValue { label: qsTr("Power draw"); value: sysmon.battPowerW.toFixed(2) + " W"
                        valueColor: Diag.amber }
                    KeyValue { label: qsTr("Current"); value: (sysmon.battCurrentA * 1000).toFixed(0) + " mA" }
                    KeyValue { label: qsTr("Voltage"); value: sysmon.battVoltageV.toFixed(3) + " V" }
                    KeyValue { label: qsTr("Temperature"); value: sysmon.battTempC.toFixed(1) + " °C" }
                    KeyValue { label: qsTr("Quality"); value: sysmon.battQuality
                        valueColor: sysmon.battHealthPct < 0 ? Theme.primaryColor
                            : sysmon.battHealthPct >= 80 ? Diag.green
                            : sysmon.battHealthPct >= 65 ? Diag.amber : Diag.red }
                    LoadBar {
                        width: parent.width; visible: sysmon.battHealthPct >= 0
                        value: sysmon.battHealthPct; maxValue: 100
                        color: sysmon.battHealthPct >= 80 ? Diag.green
                            : sysmon.battHealthPct >= 65 ? Diag.amber : Diag.red
                        label: qsTr("State of health")
                        // The ratio of the two capacities, to one decimal, and
                        // named as such: the gauge's own soh register answers a
                        // flat 100 here and is shown on the detail page instead.
                        caption: (sysmon.battHealthExact >= 0
                                  ? sysmon.battHealthExact.toFixed(1) : sysmon.battHealthPct) + " % "
                            + (sysmon.battHealthFromGauge ? qsTr("(gauge)") : qsTr("(full ÷ design)"))
                    }
                    KeyValue { visible: sysmon.battChargeDesign > 0
                        label: qsTr("Capacity"); value:
                        (sysmon.battChargeFull / 1000).toFixed(0) + " / "
                        + (sysmon.battChargeDesign / 1000).toFixed(0) + " mAh"
                        + " (" + Math.round(100 * sysmon.battChargeFull / sysmon.battChargeDesign) + " %)" }
                    KeyValue { visible: sysmon.battCycles >= 0
                        label: qsTr("Full cycles"); value: sysmon.battCycles
                        + " " + qsTr("(gauge, equiv. full cycles)") }
                    KeyValue { visible: sysmon.battTech.length > 0
                        label: qsTr("Technology"); value: sysmon.battTech
                        + (sysmon.battModel.length ? " · " + sysmon.battModel : "") }
                    KeyValue { visible: sysmon.battHealthReport.length > 0
                        label: qsTr("Driver health"); value: sysmon.battHealthReport }
                }
            }

            // ---- Thermal -----------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                visible: sysmon.thermalZones.length > 0
                title: qsTr("Thermal")
                property var maxZone: {
                    var best = null
                    for (var i = 0; i < sysmon.thermalZones.length; ++i) {
                        if (sysmon.thermalZones[i].suspect) continue
                        if (!best || sysmon.thermalZones[i].temp > best.temp)
                            best = sysmon.thermalZones[i]
                    }
                    return best
                }
                value: maxZone ? maxZone.temp.toFixed(1) : "0.0"
                unit: "°C"
                // A corrected figure must not look like a plain reading, not
                // even when it happens to be the hottest one.
                note: maxZone && maxZone.corrected ? "max *" : "max"
                accent: Diag.red
                drilldown: true
                onClicked: page.openDetail(HwInfo.thermal())
                Column {
                    width: parent.width; spacing: Theme.paddingSmall / 2
                    Repeater {
                        model: page.thermalExpanded ? sysmon.thermalZones
                                                    : sysmon.thermalZones.slice(0, page.listCap)
                        LoadBar {
                            width: parent.width
                            value: modelData.temp; maxValue: 90
                            color: modelData.temp > 70 ? Diag.red : modelData.temp > 55 ? Diag.amber : Diag.teal
                            label: modelData.name + (modelData.suspect ? "  ⚠" : "")
                            caption: modelData.temp.toFixed(1) + " °C"
                                     + (modelData.corrected ? " *" : "")
                                     + (modelData.suspect ? " ⚠" : "")
                        }
                    }
                    MoreToggle {
                        total: sysmon.thermalZones.length; shown: page.listCap
                        expanded: page.thermalExpanded
                        onToggle: page.thermalExpanded = !page.thermalExpanded
                    }
                    Label {
                        width: parent.width
                        visible: page.thermalSuspect
                        text: qsTr("⚠  This zone sits far below every other sensor in the phone, which cannot happen to a real one — it is most likely a voltage or a current that the vendor registered in the thermal framework. It is left out of the maximum above. Tap for the raw figure.")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Diag.amber
                        wrapMode: Text.Wrap
                    }
                    Label {
                        width: parent.width
                        visible: page.thermalCorrected
                        text: qsTr("*  Recomputed by this app: the kernel reads this zone through the wrong lookup table. See Help → Thermal.")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ---- Bluetooth ---------------------------------------------
            MetricCard {
                width: page.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                visible: bt.available
                title: qsTr("Bluetooth")
                drilldown: true
                diagLevel: page.diagLevels["bluetooth"] || 0
                onClicked: page.openDetail(HwInfo.bluetooth())
                value: {
                    var n = 0
                    for (var i = 0; i < bt.devices.length; ++i)
                        if (bt.devices[i].connected) n++
                    return n
                }
                note: qsTr("connected") + (bt.powered ? "" : "  ·  " + qsTr("adapter off"))
                accent: Diag.violet
                Column {
                    width: parent.width; spacing: Theme.paddingSmall / 2
                    Repeater {
                        model: bt.devices
                        KeyValue {
                            visible: modelData.connected || modelData.paired
                            height: visible ? implicitHeight : 0
                            label: modelData.name.length ? modelData.name : modelData.address
                            value: (modelData.connected ? qsTr("connected") : qsTr("paired"))
                                   + (modelData.icon.length ? " · " + modelData.icon : "")
                                   + (modelData.rssi !== 0 ? " · " + modelData.rssi + " dBm" : "")
                            valueColor: modelData.connected ? Diag.green : Theme.secondaryColor
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator {}
    }
}
