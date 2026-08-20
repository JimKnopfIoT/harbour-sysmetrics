import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sysmetrics 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["cpu","procstate","sched","mem","ioenergy","procid","devices","conn","access","battery"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) _attachHelp()

    property int pid
    property string pname

    // per-list "show first 10 / show all" state
    property bool devExpanded: false
    property bool sockExpanded: false
    property bool fileExpanded: false
    property bool threadExpanded: false
    readonly property int listCap: 10

    ProcessDetail {
        id: d
        pid: page.pid
    }


    DiagBackground {}

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Send SIGKILL")
                visible: d.alive
                onClicked: remorse.execute(qsTr("Killing %1").arg(pname),
                           function() { sysmon.sendSignal(page.pid, 9) })
            }
            MenuItem {
                text: qsTr("Send SIGTERM")
                visible: d.alive
                onClicked: remorse.execute(qsTr("Terminating %1").arg(pname),
                           function() { sysmon.sendSignal(page.pid, 15) })
            }
            MenuItem {
                text: d.info.state === "T" ? qsTr("Continue (SIGCONT)") : qsTr("Stop (SIGSTOP)")
                visible: d.alive
                onClicked: sysmon.sendSignal(page.pid, d.info.state === "T" ? 18 : 19)
            }
        }

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: d.info.name ? d.info.name : pname
                description: qsTr("PID %1 · %2 · %3").arg(page.pid)
                    .arg(d.info.user ? d.info.user : "")
                    .arg(Diag.stateLabel(d.info.state ? d.info.state : "?"))
            }

            ViewPlaceholder {
                enabled: !d.alive
                text: qsTr("Process has exited")
            }

            // ---- assessment -------------------------------------------
            Column {
                visible: d.notes.length > 0
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Repeater {
                    model: d.notes
                    Row {
                        width: parent.width
                        spacing: Theme.paddingMedium
                        Rectangle {
                            width: Theme.paddingSmall; height: Theme.paddingSmall
                            radius: width / 2; color: Diag.levelColor(modelData.level)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width - Theme.paddingSmall - Theme.paddingMedium
                            text: modelData.text
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: modelData.level >= 2 ? Diag.levelColor(modelData.level)
                                                        : Theme.primaryColor
                        }
                    }
                }
            }

            // ---- CPU ---------------------------------------------------
            MetricCard {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                title: qsTr("CPU")
                value: (d.cpu.pct !== undefined ? d.cpu.pct : 0).toFixed(1)
                unit: "%  ·  " + qsTr("%1 of %2 cores").arg(d.cpu.coresUsed || 0).arg(d.cpu.coreCount || 0)
                accent: Diag.loadColor(d.cpu.pct || 0)
                Column {
                    width: parent.width; spacing: Theme.paddingSmall
                    HistoryGraph {
                        width: parent.width; height: Theme.itemSizeSmall
                        values: d.cpuHistory; maxValue: 100
                        lineColor: Diag.loadColor(d.cpu.pct || 0)
                        fillColor: Qt.rgba(Diag.cyan.r, Diag.cyan.g, Diag.cyan.b, 0.16)
                        gridColor: Diag.grid
                    }
                    // per-core: process load (bright) over system load (faint)
                    Grid {
                        width: parent.width; columns: 2
                        columnSpacing: Theme.paddingLarge; rowSpacing: Theme.paddingSmall
                        Repeater {
                            model: d.cpu.perCore ? d.cpu.perCore.length : 0
                            Item {
                                width: (parent.width - Theme.paddingLarge) / 2
                                height: Theme.itemSizeExtraSmall * 0.7
                                LoadBar {
                                    anchors.fill: parent
                                    value: d.cpu.sysPerCore[index]; maxValue: 100
                                    color: Diag.grid
                                    label: qsTr("c%1").arg(index)
                                    caption: Math.round(d.cpu.perCore[index]) + "%"
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    height: Theme.paddingSmall / 1.5; radius: height / 2
                                    width: parent.width * Math.min(1, (d.cpu.perCore[index] || 0) / 100)
                                    color: Diag.loadColor(d.cpu.perCore[index] || 0)
                                }
                            }
                        }
                    }
                    KeyValue { label: qsTr("Share of busy CPU"); value:
                        (d.cpu.shareOfBusyPct || 0).toFixed(1) + " %" }
                    KeyValue { label: qsTr("CPU time"); value:
                        sysmon.fmtDuration(Math.round(d.cpu.timeSec || 0)) }
                    KeyValue { label: qsTr("Context switches/s"); value:
                        qsTr("%1 vol · %2 invol").arg(Math.round(d.cpu.vctxPerSec || 0))
                        .arg(Math.round(d.cpu.nvctxPerSec || 0)) }
                    KeyValue { label: qsTr("Wakeups/s"); value: Math.round(d.cpu.wakeupsPerSec || 0) + "";
                        valueColor: (d.cpu.wakeupsPerSec || 0) > 100 ? Diag.amber : Theme.primaryColor }
                    KeyValue { label: qsTr("Page faults/s"); value:
                        qsTr("%1 minor · %2 major").arg(Math.round(d.cpu.minfltPerSec || 0))
                        .arg(Math.round(d.cpu.majfltPerSec || 0)) }
                    KeyValue { label: qsTr("CPU affinity"); value: d.info.cpusAllowed || "" }
                    KeyValue { label: qsTr("Nice / priority"); value:
                        (d.info.nice !== undefined ? d.info.nice : "") + " / " + (d.info.prio || "") }
                }
            }

            // ---- Memory -----------------------------------------------
            MetricCard {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                title: qsTr("Memory")
                value: sysmon.fmtBytes(d.mem.pss !== undefined ? d.mem.pss : d.mem.rss || 0)
                unit: d.mem.pss !== undefined ? "PSS" : "RSS"
                accent: Diag.teal
                Column {
                    width: parent.width; spacing: Theme.paddingSmall / 2
                    KeyValue { label: qsTr("RSS (resident)"); value: sysmon.fmtBytes(d.mem.rss || 0) }
                    KeyValue { label: "PSS"; value: d.mem.pss !== undefined ? sysmon.fmtBytes(d.mem.pss) : "—" }
                    KeyValue { label: qsTr("USS (private)"); value: d.mem.uss !== undefined ? sysmon.fmtBytes(d.mem.uss) : "—" }
                    KeyValue { label: qsTr("Swapped"); value: d.mem.swap !== undefined ? sysmon.fmtBytes(d.mem.swap) : "—";
                        valueColor: (d.mem.swap || 0) > 0 ? Diag.violet : Theme.primaryColor }
                    KeyValue { label: qsTr("Virtual"); value: sysmon.fmtBytes(d.mem.vmsize || 0) }
                }
            }

            // ---- I/O + Energy -----------------------------------------
            MetricCard {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                title: qsTr("I/O & energy")
                value: sysmon.fmtRate((d.io.readRate || 0) + (d.io.writeRate || 0))
                accent: Diag.amber
                Column {
                    width: parent.width; spacing: Theme.paddingSmall / 2
                    KeyValue { label: qsTr("Disk read/write"); value:
                        sysmon.fmtRate(d.io.readRate || 0) + " / " + sysmon.fmtRate(d.io.writeRate || 0) }
                    KeyValue { label: qsTr("Total read/write"); value:
                        sysmon.fmtBytes(d.io.readTotal || 0) + " / " + sysmon.fmtBytes(d.io.writeTotal || 0) }
                    KeyValue { label: qsTr("Est. power share"); value:
                        d.energy.discharging ? (d.energy.estimateW * 1000).toFixed(0) + " mW ("
                            + (d.energy.sharePct || 0).toFixed(1) + " %)" : qsTr("charging");
                        valueColor: (d.energy.estimateW || 0) > 0.3 ? Diag.amber : Theme.primaryColor }
                }
            }

            // ---- Access watch (other processes reaching in) -----------
            SectionHeader { text: qsTr("Access monitor") }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                KeyValue {
                    label: qsTr("Traced by")
                    value: (d.watch.tracerPid || 0) > 0
                        ? d.watch.tracerName + " (PID " + d.watch.tracerPid + ")" : qsTr("no")
                    valueColor: (d.watch.tracerPid || 0) > 0 ? Diag.red : Diag.teal
                }
                Label {
                    width: parent.width
                    visible: !d.watch.complete
                    text: qsTr("Full access-monitor coverage needs root mode.")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryColor
                }
                Repeater {
                    model: d.watch.watchers ? d.watch.watchers : []
                    KeyValue {
                        label: modelData.name + " (" + modelData.pid + ")"
                        value: modelData.what
                        valueColor: Diag.amber
                    }
                }
                Label {
                    width: parent.width
                    visible: d.watch.watchers && d.watch.watchers.length === 0
                    text: qsTr("No other process holds handles into this process.")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }

            // ---- Devices ----------------------------------------------
            ExpandingSectionGroup {
                width: page.width
                ExpandingSection {
                    title: qsTr("Devices (%1)").arg(d.devices.length)
                    content.sourceComponent: Component { Column {
                        width: page.width
                        Repeater {
                            model: page.devExpanded ? d.devices : d.devices.slice(0, page.listCap)
                            Column {
                                x: Theme.horizontalPageMargin
                                width: page.width - 2 * Theme.horizontalPageMargin
                                Label {
                                    width: parent.width
                                    text: (modelData.product || modelData.path) + "  [" + modelData.mode + "]"
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Diag.cyan
                                    truncationMode: TruncationMode.Fade
                                }
                                KeyValue { label: qsTr("Node"); value: modelData.path; mono: true }
                                KeyValue { label: qsTr("Subsystem"); value: modelData.subsystem || "—" }
                                KeyValue { label: qsTr("Driver"); value: modelData.driver || "—" }
                                KeyValue { label: qsTr("Vendor"); value: modelData.vendor || "—" }
                                KeyValue { label: qsTr("Serial"); value: modelData.serial || "—"; mono: true }
                                KeyValue { visible: modelData.size !== undefined
                                    label: qsTr("Size"); value: modelData.size ? sysmon.fmtBytes(modelData.size) : "—" }
                                Item { width: 1; height: Theme.paddingMedium }
                            }
                        }
                        MoreToggle {
                            total: d.devices.length; shown: page.listCap; expanded: page.devExpanded
                            onToggle: page.devExpanded = !page.devExpanded
                        }
                        Label {
                            visible: d.devices.length === 0
                            x: Theme.horizontalPageMargin
                            text: qsTr("No device nodes open")
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                        }
                    } }
                }
            }

            // ---- Network sockets --------------------------------------
            ExpandingSectionGroup {
                width: page.width
                ExpandingSection {
                    title: qsTr("Network (%1)").arg(d.sockets.length)
                    content.sourceComponent: Component { Column {
                        width: page.width
                        Repeater {
                            model: page.sockExpanded ? d.sockets : d.sockets.slice(0, page.listCap)
                            Item {
                                x: Theme.horizontalPageMargin
                                width: page.width - 2 * Theme.horizontalPageMargin
                                height: Theme.itemSizeSmall
                                Rectangle {
                                    id: dot
                                    width: Theme.paddingSmall; height: Theme.paddingSmall
                                    radius: width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: modelData.active ? Diag.green : Theme.secondaryColor
                                    opacity: modelData.active ? 1 : 0.4
                                    SequentialAnimation on opacity {
                                        running: modelData.active; loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 700 }
                                        NumberAnimation { to: 1.0; duration: 700 }
                                    }
                                }
                                Column {
                                    anchors { left: dot.right; leftMargin: Theme.paddingMedium
                                        right: parent.right; verticalCenter: parent.verticalCenter }
                                    Label {
                                        width: parent.width
                                        text: (modelData.proto || "?").toUpperCase() + "  "
                                              + (modelData.remote || modelData.state || "")
                                        font.pixelSize: Theme.fontSizeExtraSmall
                                        font.family: "monospace"
                                        truncationMode: TruncationMode.Fade
                                        color: modelData.active ? Diag.green : Theme.primaryColor
                                    }
                                    Label {
                                        width: parent.width
                                        visible: modelData.local !== undefined
                                        text: qsTr("local ") + (modelData.local || "") + "  " + (modelData.state || "")
                                        font.pixelSize: Theme.fontSizeTiny
                                        color: Theme.secondaryColor
                                        truncationMode: TruncationMode.Fade
                                    }
                                }
                            }
                        }
                        MoreToggle {
                            total: d.sockets.length; shown: page.listCap; expanded: page.sockExpanded
                            onToggle: page.sockExpanded = !page.sockExpanded
                        }
                        Label {
                            visible: d.sockets.length === 0
                            x: Theme.horizontalPageMargin
                            text: qsTr("No sockets open")
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                        }
                    } }
                }
            }

            // ---- Open files -------------------------------------------
            ExpandingSectionGroup {
                width: page.width
                ExpandingSection {
                    title: qsTr("Open files (%1)").arg(d.files.length)
                    content.sourceComponent: Component { Column {
                        width: page.width
                        Repeater {
                            model: page.fileExpanded ? d.files : d.files.slice(0, page.listCap)
                            Item {
                                x: Theme.horizontalPageMargin
                                width: page.width - 2 * Theme.horizontalPageMargin
                                height: fcol.height + Theme.paddingSmall
                                Column {
                                    id: fcol
                                    width: parent.width
                                    Label {
                                        width: parent.width
                                        text: "[" + modelData.mode + "] " + modelData.path
                                              + (modelData.deleted ? " " + qsTr("(deleted)") : "")
                                        font.pixelSize: Theme.fontSizeTiny
                                        font.family: "monospace"
                                        color: modelData.deleted ? Diag.amber : Theme.primaryColor
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 2
                                    }
                                }
                            }
                        }
                        MoreToggle {
                            total: d.files.length; shown: page.listCap; expanded: page.fileExpanded
                            onToggle: page.fileExpanded = !page.fileExpanded
                        }
                        Label {
                            visible: d.files.length === 0
                            x: Theme.horizontalPageMargin
                            text: qsTr("No regular files open")
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                        }
                    } }
                }
            }

            // ---- Threads ----------------------------------------------
            ExpandingSectionGroup {
                width: page.width
                ExpandingSection {
                    title: qsTr("Threads (%1)").arg(d.threads.length)
                    content.sourceComponent: Component { Column {
                        width: page.width
                        Repeater {
                            model: page.threadExpanded ? d.threads : d.threads.slice(0, page.listCap)
                            Row {
                                x: Theme.horizontalPageMargin
                                width: page.width - 2 * Theme.horizontalPageMargin
                                height: Theme.itemSizeExtraSmall * 0.8
                                Label {
                                    width: parent.width * 0.2
                                    text: modelData.tid
                                    font.pixelSize: Theme.fontSizeTiny
                                    color: Theme.secondaryColor
                                }
                                Label {
                                    width: parent.width * 0.5
                                    text: modelData.name
                                    font.pixelSize: Theme.fontSizeTiny
                                    truncationMode: TruncationMode.Fade
                                }
                                Label {
                                    width: parent.width * 0.15
                                    text: qsTr("c%1").arg(modelData.core)
                                    font.pixelSize: Theme.fontSizeTiny
                                    color: Theme.secondaryColor
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    width: parent.width * 0.15
                                    text: Math.round(modelData.cpu) + "%"
                                    font.pixelSize: Theme.fontSizeTiny
                                    color: Diag.loadColor(modelData.cpu)
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                        MoreToggle {
                            total: d.threads.length; shown: page.listCap; expanded: page.threadExpanded
                            onToggle: page.threadExpanded = !page.threadExpanded
                        }
                    } }
                }
            }

            // ---- Identity ----------------------------------------------
            SectionHeader { text: qsTr("Process") }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                KeyValue { label: qsTr("Command line"); value: d.info.cmdline || ""; mono: true }
                KeyValue { label: qsTr("Executable"); value: d.info.exe || ""; mono: true }
                KeyValue { label: qsTr("Working dir"); value: d.info.cwd || ""; mono: true }
                KeyValue { label: qsTr("Parent"); value:
                    (d.info.ppidName || "") + " (" + (d.info.ppid || 0) + ")" }
                KeyValue { label: qsTr("cgroup"); value: d.info.cgroup || ""; mono: true }
                KeyValue { label: qsTr("Threads / fds"); value:
                    (d.info.threadCount || 0) + " / " + (d.info.fdCount || 0) }
                KeyValue { label: qsTr("Timer fds"); value: (d.info.timerfdCount || 0) + "" }
                KeyValue { label: qsTr("OOM score"); value: (d.info.oomScore || 0) + "" }
                KeyValue { label: qsTr("Age"); value: sysmon.fmtDuration(d.info.ageSec || 0) }
                KeyValue { label: qsTr("Data source"); value:
                    d.info.source === "root" ? qsTr("root helper")
                    : d.info.source === "self" ? qsTr("direct")
                    : d.info.source === "restricted" ? qsTr("restricted — sandboxed, enable root mode")
                    : qsTr("limited (foreign user)")
                    valueColor: d.info.source === "restricted" ? Diag.amber : Theme.primaryColor }
            }

            // ---- renice -----------------------------------------------
            SectionHeader { text: qsTr("Priority") }
            Slider {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                minimumValue: -20; maximumValue: 19; stepSize: 1
                value: d.info.nice !== undefined ? d.info.nice : 0
                valueText: value
                label: qsTr("nice (lower = more CPU; needs privilege to lower)")
                onReleased: sysmon.setNice(page.pid, value)
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator {}
    }
}
