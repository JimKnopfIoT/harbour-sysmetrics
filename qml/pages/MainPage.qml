import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sysmetrics 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["cpu","procstate","sched","mem","procid","monitoring"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) { _attachHelp(); list.positionViewAtBeginning(); _pinTop.restart() }
    Timer { id: _pinTop; interval: 60; repeat: false; onTriggered: list.positionViewAtBeginning() }

    // collapsed = only the top consumers are shown; expand for the full list
    property bool expanded: false
    readonly property int topCount: 10


    DiagBackground {}

    // Freeze row re-ordering while the user interacts with the list, so a row
    // does not jump away under the finger; values keep updating in place. A
    // grace period lets the order settle before it catches up again.
    property bool touching: list.moving || list.dragging || list.flicking
    onTouchingChanged: {
        if (touching) { thawTimer.stop(); procs.frozen = true }
        else thawTimer.restart()
    }
    Timer { id: thawTimer; interval: 2500; onTriggered: procs.frozen = false }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: procs
        clip: true

        PullDownMenu {
            MenuItem {
                text: sysmon.paused ? qsTr("Resume") : qsTr("Pause")
                onClicked: sysmon.paused = !sysmon.paused
            }
            MenuItem {
                text: qsTr("Record load")
                onClicked: pageStack.push(Qt.resolvedUrl("RecordPage.qml"))
            }
            MenuItem {
                text: qsTr("Connections")
                onClicked: pageStack.push(Qt.resolvedUrl("ConnectionsPage.qml"))
            }
            MenuItem {
                text: qsTr("System overview")
                onClicked: pageStack.push(Qt.resolvedUrl("OverviewPage.qml"))
            }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("SysMetrics")
                description: qsTr("%1 processes · %2 threads")
                             .arg(sysmon.processCount).arg(sysmon.threadCount)
            }

            // --- system pulse: total CPU + memory + graph -------------------
            Item {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Theme.itemSizeMedium

                Column {
                    width: parent.width * 0.42
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.paddingSmall
                    Row {
                        spacing: Theme.paddingSmall
                        Label {
                            text: Math.round(sysmon.cpuPercent) + "%"
                            font.pixelSize: Theme.fontSizeExtraLarge
                            color: Diag.loadColor(sysmon.cpuPercent)
                        }
                        Label {
                            text: qsTr("CPU")
                            anchors.baseline: parent.children[0].baseline
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                        }
                    }
                    Label {
                        text: qsTr("load %1  ·  %2 cores")
                              .arg(sysmon.load1.toFixed(2)).arg(sysmon.coreCount)
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                    }
                }

                HistoryGraph {
                    anchors.right: parent.right
                    width: parent.width * 0.54
                    height: parent.height
                    values: sysmon.cpuHistory
                    maxValue: 100
                    lineColor: Diag.cyan
                    fillColor: Qt.rgba(Diag.cyan.r, Diag.cyan.g, Diag.cyan.b, 0.18)
                    gridColor: Diag.grid
                }
            }

            // --- per-core load, compared to the aggregate -------------------
            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                Row {
                    width: parent.width
                    Label {
                        text: qsTr("Cores")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                        width: parent.width / 2
                    }
                    Label {
                        text: qsTr("avg %1%").arg(Math.round(sysmon.cpuPercent))
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        horizontalAlignment: Text.AlignRight
                        width: parent.width / 2
                    }
                }

                Grid {
                    width: parent.width
                    columns: 2
                    columnSpacing: Theme.paddingLarge
                    rowSpacing: Theme.paddingSmall
                    Repeater {
                        model: sysmon.corePercents
                        LoadBar {
                            width: (parent.width - Theme.paddingLarge) / 2
                            value: modelData
                            label: qsTr("c%1").arg(index)
                            caption: Math.round(modelData) + "%"
                                     + (sysmon.coreFreqsMhz.length > index
                                        ? " · " + sysmon.coreFreqsMhz[index] + "M" : "")
                        }
                    }
                }
            }

            // --- search + sort + filters -----------------------------------
            SearchField {
                width: parent.width
                placeholderText: qsTr("Filter by name, cmdline or PID")
                onTextChanged: procs.search = text
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }

            Flickable {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Theme.itemSizeExtraSmall * 0.8
                contentWidth: sortRow.width
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                Row {
                    id: sortRow
                    spacing: Theme.paddingSmall
                    property var keys: [
                        { k: "cpu", t: qsTr("CPU") },
                        { k: "mem", t: qsTr("Memory") },
                        { k: "name", t: qsTr("Name") },
                        { k: "pid", t: qsTr("PID") },
                        { k: "threads", t: qsTr("Threads") }
                    ]
                    Repeater {
                        model: sortRow.keys
                        FilterPill {
                            text: modelData.t + (procs.sortBy === modelData.k
                                  ? (procs.descending ? " ↓" : " ↑") : "")
                            active: procs.sortBy === modelData.k
                            onClicked: {
                                if (procs.sortBy === modelData.k)
                                    procs.descending = !procs.descending
                                else
                                    procs.sortBy = modelData.k
                            }
                        }
                    }
                }
            }

            Row {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                FilterPill {
                    text: qsTr("Apps only"); accent: Diag.teal
                    active: procs.appsOnly
                    onClicked: procs.appsOnly = !procs.appsOnly
                }
                FilterPill {
                    text: qsTr("Kernel threads"); accent: Diag.violet
                    active: procs.showKernel
                    onClicked: procs.showKernel = !procs.showKernel
                }
                Item { width: parent.width - x; height: 1 }
            }

            Item {
                width: parent.width
                height: sectionH.height
                SectionHeader {
                    id: sectionH
                    text: page.expanded ? qsTr("All processes (%1)").arg(procs.count)
                                        : qsTr("Top consumers")
                }
                Label {
                    visible: page.expanded
                    anchors { right: sectionH.right; rightMargin: Theme.horizontalPageMargin
                        verticalCenter: sectionH.verticalCenter }
                    text: "▲"
                    color: Diag.cyan
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: page.expanded
                    onClicked: page.expanded = false
                }
            }
        }

        delegate: ProcessRow {
            rank: index
            // collapsed: keep only the leading rows; a plain search overrides
            visible: page.expanded || procs.search.length > 0 || index < page.topCount
            height: visible ? contentHeight : 0
            onPressedChanged: {
                if (pressed) { thawTimer.stop(); procs.frozen = true }
                else thawTimer.restart()
            }
            onClicked: pageStack.push(Qt.resolvedUrl("ProcessDetailPage.qml"),
                                      { pid: pid, pname: name })
        }

        footer: BackgroundItem {
            width: list.width
            height: Theme.itemSizeMedium
            visible: procs.search.length === 0 && procs.count > page.topCount
            onClicked: page.expanded = !page.expanded
            Row {
                anchors.centerIn: parent
                spacing: Theme.paddingSmall
                Label {
                    text: page.expanded ? qsTr("Collapse")
                          : qsTr("Show all %1 processes").arg(procs.count)
                    color: Diag.cyan
                    font.pixelSize: Theme.fontSizeSmall
                }
                Label {
                    text: page.expanded ? "▲" : "▼"
                    color: Diag.cyan
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
