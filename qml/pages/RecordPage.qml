import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sysmetrics 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    DiagBackground {}

    SilicaListView {
        id: list
        anchors.fill: parent
        model: recorder.results

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Load recording") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Accumulates CPU time per process over a session and ranks "
                    + "the consumers — including short-lived processes that never show "
                    + "up in an instantaneous view. Let it run, then read the ranking.")
            }

            Item {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Theme.itemSizeMedium
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Label {
                        text: sysmon.fmtDuration(recorder.elapsedSec)
                        font.pixelSize: Theme.fontSizeExtraLarge
                        color: recorder.running ? Diag.green : Theme.primaryColor
                    }
                    Label {
                        text: qsTr("%1 CPU-seconds captured").arg(recorder.totalCpuSec.toFixed(1))
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                }
                Rectangle {
                    visible: recorder.running
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    width: Theme.paddingMedium; height: Theme.paddingMedium
                    radius: width / 2; color: Diag.red
                    SequentialAnimation on opacity {
                        running: recorder.running; loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
            }

            Row {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                Button {
                    text: recorder.running ? qsTr("Stop") : qsTr("Start")
                    onClicked: recorder.running ? recorder.stop() : recorder.start()
                }
                Button {
                    text: qsTr("Reset")
                    enabled: !recorder.running && recorder.elapsedSec > 0
                    onClicked: recorder.reset()
                }
            }

            SectionHeader { text: qsTr("Ranking") }

            Column {
                visible: recorder.results.length === 0 && !recorder.running
                width: parent.width
                spacing: Theme.paddingSmall
                Item { width: 1; height: Theme.paddingLarge }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Nothing recorded")
                    font.pixelSize: Theme.fontSizeLarge
                    color: Theme.secondaryColor
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Press Start to begin sampling")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                }
            }
        }

        delegate: BackgroundItem {
            width: list.width
            contentHeight: Theme.itemSizeSmall
            onClicked: pageStack.push(Qt.resolvedUrl("ProcessDetailPage.qml"),
                                      { pid: modelData.pid, pname: modelData.name })
            Rectangle {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin }
                height: parent.height - Theme.paddingSmall
                width: (list.width - 2 * Theme.horizontalPageMargin) * modelData.share / 100
                radius: Theme.paddingSmall / 2
                color: Qt.rgba(Diag.teal.r, Diag.teal.g, Diag.teal.b, 0.16)
            }
            Row {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin; rightMargin: Theme.horizontalPageMargin }
                Column {
                    width: parent.width * 0.62
                    Label {
                        width: parent.width; text: modelData.name
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    Label {
                        text: "PID " + modelData.pid + (modelData.isApp ? " · " + qsTr("app") : "")
                        font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor
                    }
                }
                Column {
                    width: parent.width * 0.38
                    Label {
                        anchors.right: parent.right
                        text: modelData.share.toFixed(1) + "%"
                        font.pixelSize: Theme.fontSizeSmall; color: Diag.teal
                    }
                    Label {
                        anchors.right: parent.right
                        text: modelData.cpuSec.toFixed(1) + " s"
                        font.pixelSize: Theme.fontSizeTiny; color: Theme.secondaryColor
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
