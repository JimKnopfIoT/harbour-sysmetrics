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

    // While the redraw check measures, the cover must not draw: every update
    // here is a frame lipstick composes, and the check would measure itself.
    // Hidden items are not rendered, so only the fixed text below remains.
    Label {
        visible: redraw.busy
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeSmall
        color: Diag.amber
        text: redraw.coverText
    }

    Column {
        visible: !redraw.busy
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
        visible: !redraw.busy
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height * 0.34
        values: sysmon.cpuHistory
        maxValue: 100
        lineColor: Diag.cyan
        fillColor: Qt.rgba(Diag.cyan.r, Diag.cyan.g, Diag.cyan.b, 0.22)
        gridColor: "transparent"
    }

    CoverActionList {
        enabled: !redraw.busy
        CoverAction {
            iconSource: sysmon.paused ? "image://theme/icon-cover-play"
                                      : "image://theme/icon-cover-pause"
            onTriggered: sysmon.paused = !sysmon.paused
        }
    }
}
