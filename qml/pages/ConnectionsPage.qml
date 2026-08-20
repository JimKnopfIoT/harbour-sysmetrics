import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property bool _helpAttached: false
    property var helpTopics: ["conn","threat"]
    function _attachHelp() {
        if (_helpAttached) return
        if (helpTopics && helpTopics.length === 0) { _helpAttached = true; return }
        var p = pageStack.pushAttached(Qt.resolvedUrl("HelpPage.qml"), { topics: helpTopics })
        if (p) _helpAttached = true
    }
    onStatusChanged: if (status === PageStatus.Active) _attachHelp()

    property string mode: "all"   // all, in, out, listen, ssh

    Component.onCompleted: {
        netmon.refresh()
    }
    Timer { interval: 3000; running: page.status === PageStatus.Active; repeat: true
        onTriggered: netmon.refresh() }

    function matches(c) {
        if (mode === "ssh") return c.ssh
        if (mode === "in") return c.direction === "in"
        if (mode === "out") return c.direction === "out"
        if (mode === "listen") return c.direction === "listen"
        return true
    }
    function dirColor(dir) {
        if (dir === "in") return Diag.green
        if (dir === "out") return Diag.cyan
        return Diag.violet
    }
    function dirArrow(dir) {
        if (dir === "in") return "↓"
        if (dir === "out") return "↑"
        return "◆"
    }

    DiagBackground {}

    SilicaListView {
        id: list
        anchors.fill: parent
        model: netmon.connections

        PullDownMenu {
            MenuItem { text: qsTr("Refresh"); onClicked: netmon.refresh() }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Connections")
                description: qsTr("%1 total · %2 listening · %3 established")
                    .arg(netmon.total).arg(netmon.listening).arg(netmon.established)
            }

            // --- threat assessment banner -----------------------------------
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: threatCol.height + 2 * Theme.paddingMedium
                radius: Theme.paddingMedium
                color: Qt.rgba(Diag.levelColor(netmon.threatLevel).r,
                               Diag.levelColor(netmon.threatLevel).g,
                               Diag.levelColor(netmon.threatLevel).b, 0.12)
                border.width: 1
                border.color: Diag.levelColor(netmon.threatLevel)

                Column {
                    id: threatCol
                    anchors {
                        left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                        margins: Theme.paddingMedium
                    }
                    spacing: Theme.paddingSmall

                    Row {
                        width: parent.width
                        spacing: Theme.paddingMedium
                        Rectangle {
                            width: Theme.iconSizeSmall * 0.5; height: width; radius: width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Diag.levelColor(netmon.threatLevel)
                            SequentialAnimation on opacity {
                                running: netmon.threatLevel >= 3; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 600 }
                                NumberAnimation { to: 1.0; duration: 600 }
                            }
                        }
                        Column {
                            width: parent.width - Theme.iconSizeSmall * 0.5 - Theme.paddingMedium
                            Label {
                                text: qsTr("Threat: %1").arg(netmon.threatSummary)
                                font.pixelSize: Theme.fontSizeMedium
                                color: Diag.levelColor(netmon.threatLevel)
                            }
                            Label {
                                text: qsTr("network exposure assessment")
                                font.pixelSize: Theme.fontSizeTiny
                                color: Theme.secondaryColor
                            }
                        }
                    }

                    Repeater {
                        model: netmon.threatFindings
                        Row {
                            width: threatCol.width
                            spacing: Theme.paddingSmall
                            Label {
                                text: "•"
                                color: Diag.levelColor(modelData.level)
                                font.pixelSize: Theme.fontSizeExtraSmall
                            }
                            Label {
                                width: parent.width - Theme.paddingSmall - x
                                text: modelData.text
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: modelData.level >= 2 ? Diag.levelColor(modelData.level)
                                                            : Theme.primaryColor
                            }
                        }
                    }
                }
            }

            Label {
                visible: !rootmon.active
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
                text: qsTr("Connections owned by other users (e.g. sshd as root) show "
                    + "without a process name until root mode is active.")
            }

            Flickable {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Theme.itemSizeExtraSmall * 0.8
                contentWidth: filterRow.width
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                Row {
                    id: filterRow
                    spacing: Theme.paddingSmall
                    property var opts: [
                        { k: "all", t: qsTr("All") },
                        { k: "in", t: qsTr("Inbound") },
                        { k: "out", t: qsTr("Outbound") },
                        { k: "listen", t: qsTr("Listening") },
                        { k: "ssh", t: qsTr("SSH") }
                    ]
                    Repeater {
                        model: filterRow.opts
                        FilterPill {
                            text: modelData.t
                            active: page.mode === modelData.k
                            accent: modelData.k === "ssh" ? Diag.red : Diag.cyan
                            onClicked: page.mode = modelData.k
                        }
                    }
                }
            }
        }

        delegate: ListItem {
            id: row
            visible: page.matches(modelData)
            height: visible ? contentHeight : 0
            contentHeight: Theme.itemSizeSmall
            onClicked: pageStack.push(Qt.resolvedUrl("ConnectionDetailPage.qml"), {
                proto: modelData.proto, local: modelData.local, remote: modelData.remote,
                state: modelData.state, direction: modelData.direction, user: modelData.user,
                pid: modelData.pid, pname: modelData.name, threat: modelData.threat,
                ssh: modelData.ssh, active: modelData.active })

            Row {
                anchors {
                    left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin; rightMargin: Theme.horizontalPageMargin
                }
                spacing: Theme.paddingMedium

                Column {
                    width: Theme.itemSizeExtraSmall
                    anchors.verticalCenter: parent.verticalCenter
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: page.dirArrow(modelData.direction)
                        font.pixelSize: Theme.fontSizeLarge
                        color: page.dirColor(modelData.direction)
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.paddingSmall; height: Theme.paddingSmall
                        radius: width / 2
                        visible: modelData.active
                        color: Diag.green
                        SequentialAnimation on opacity {
                            running: modelData.active; loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 700 }
                            NumberAnimation { to: 1.0; duration: 700 }
                        }
                    }
                }

                Column {
                    width: parent.width - Theme.itemSizeExtraSmall - protoCol.width - 2 * parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    Label {
                        width: parent.width
                        text: modelData.direction === "listen"
                              ? qsTr("listening on %1").arg(modelData.local)
                              : modelData.remote
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        font.family: "monospace"
                        color: modelData.threat >= 2 ? Diag.levelColor(modelData.threat)
                               : row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        text: (modelData.name.length ? modelData.name : qsTr("uid %1").arg(modelData.user))
                              + (modelData.pid > 0 ? " · " + modelData.pid : "")
                              + " · " + modelData.local
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                    }
                }

                Column {
                    id: protoCol
                    width: Math.max(implicitWidth, Theme.itemSizeExtraSmall)
                    anchors.verticalCenter: parent.verticalCenter
                    Label {
                        anchors.right: parent.right
                        text: modelData.proto.toUpperCase()
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        anchors.right: parent.right
                        text: modelData.state
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: netmon.total === 0
            text: qsTr("No connections")
        }

        VerticalScrollDecorator {}
    }
}
