import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.sysmetrics 1.0
import "../components"

CoverBackground {
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1b2029" }
            GradientStop { position: 1.0; color: "#0b0e13" }
        }
    }

    Column {
        anchors {
            top: parent.top; left: parent.left; right: parent.right
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingSmall

        Label {
            text: "SysMetrics"
            font.pixelSize: Theme.fontSizeLarge
            color: Diag.cyan
        }
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
            text: qsTr("RAM %1%").arg(sysmon.memTotal > 0
                  ? Math.round(100 * sysmon.memUsed / sysmon.memTotal) : 0)
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
        }
    }

    HistoryGraph {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height * 0.34
        values: sysmon.cpuHistory
        maxValue: 100
        lineColor: Diag.cyan
        fillColor: Qt.rgba(Diag.cyan.r, Diag.cyan.g, Diag.cyan.b, 0.22)
        gridColor: "transparent"
    }

    CoverActionList {
        CoverAction {
            iconSource: sysmon.paused ? "image://theme/icon-cover-play"
                                      : "image://theme/icon-cover-pause"
            onTriggered: sysmon.paused = !sysmon.paused
        }
    }
}
