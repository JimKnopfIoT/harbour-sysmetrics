import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sysmetrics 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property string proto
    property string local
    property string remote
    property string state
    property string direction
    property string user
    property int pid
    property string pname
    property int threat: 0
    property bool ssh: false
    property bool active: false

    // live process facts for the owning app, if known
    ProcessDetail { id: d; pid: page.pid > 0 ? page.pid : 0 }

    function dirLabel(dir) {
        if (dir === "in") return qsTr("inbound")
        if (dir === "out") return qsTr("outbound")
        return qsTr("listening")
    }

    DiagBackground {}

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: direction === "listen" ? qsTr("Listening socket") : remote
                description: proto.toUpperCase() + " · " + dirLabel(direction)
            }

            // threat chip
            Rectangle {
                x: Theme.horizontalPageMargin
                width: chip.width + 2 * Theme.paddingMedium
                height: chip.height + Theme.paddingSmall
                radius: height / 2
                color: Qt.rgba(Diag.levelColor(threat).r, Diag.levelColor(threat).g,
                               Diag.levelColor(threat).b, 0.15)
                border.width: 1; border.color: Diag.levelColor(threat)
                Label {
                    id: chip
                    anchors.centerIn: parent
                    text: ssh ? qsTr("SSH — remote shell")
                          : threat >= 3 ? qsTr("High risk")
                          : threat === 2 ? qsTr("Elevated")
                          : threat === 1 ? qsTr("Watch")
                          : qsTr("Benign")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.levelColor(threat)
                }
            }

            SectionHeader { text: qsTr("Connection") }
            Column {
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                KeyValue { label: qsTr("Protocol"); value: proto.toUpperCase() }
                KeyValue { label: qsTr("Direction"); value: dirLabel(direction)
                    valueColor: direction === "in" ? Diag.green : direction === "out" ? Diag.cyan : Diag.violet }
                KeyValue { label: qsTr("State"); value: state }
                KeyValue { label: qsTr("Local endpoint"); value: local; mono: true }
                KeyValue { label: qsTr("Remote endpoint"); value: remote.length ? remote : "—"; mono: true }
                KeyValue { label: qsTr("Owner (user)"); value: user }
                KeyValue { label: qsTr("Activity"); value: active ? qsTr("exchanging data") : qsTr("idle")
                    valueColor: active ? Diag.green : Theme.secondaryColor }
            }

            SectionHeader { text: qsTr("Owning process") }

            // known owner: live facts + link to the full process page
            Column {
                visible: page.pid > 0
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall / 2
                KeyValue { label: qsTr("Name"); value: d.info.name ? d.info.name : pname }
                KeyValue { label: qsTr("PID"); value: page.pid + "" }
                KeyValue { label: qsTr("State"); value: d.alive
                    ? Diag.stateLabel(d.info.state ? d.info.state : "?") : qsTr("exited") }
                KeyValue { label: qsTr("CPU now"); value: (d.cpu.pct !== undefined ? d.cpu.pct : 0).toFixed(1) + " %"
                    valueColor: Diag.loadColor(d.cpu.pct || 0) }
                KeyValue { label: qsTr("Memory"); value: sysmon.fmtBytes(d.mem.pss !== undefined ? d.mem.pss : d.mem.rss || 0) }
                KeyValue { label: qsTr("Executable"); value: d.info.exe || ""; mono: true }
                KeyValue { label: qsTr("Command line"); value: d.info.cmdline || ""; mono: true }
                KeyValue { label: qsTr("User"); value: d.info.user || user }
            }

            Button {
                visible: page.pid > 0
                x: Theme.horizontalPageMargin
                text: qsTr("Full process details")
                onClicked: pageStack.push(Qt.resolvedUrl("ProcessDetailPage.qml"),
                                          { pid: page.pid, pname: d.info.name ? d.info.name : pname })
            }

            // unknown owner (foreign uid, no root helper)
            Column {
                visible: page.pid <= 0
                x: Theme.horizontalPageMargin
                width: page.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("This socket is owned by user '%1'. Its process cannot be "
                        + "identified without root, because reading another user's open "
                        + "files is privileged.").arg(user)
                }
                Label {
                    width: parent.width
                    visible: rootmon.active
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Diag.amber
                    text: qsTr("Root helper is active but did not map this socket — it may "
                        + "have just closed. Pull down to refresh the list.")
                }
                Button {
                    text: qsTr("How to enable root mode")
                    onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
}
