import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// One process line: rank bar, name, pid/user, cpu + mem figures.
ListItem {
    id: row
    property int rank: -1
    contentHeight: Theme.itemSizeSmall

    // load bar behind the row
    Rectangle {
        anchors {
            left: parent.left; verticalCenter: parent.verticalCenter
            leftMargin: Theme.horizontalPageMargin
        }
        height: parent.height - Theme.paddingSmall
        width: (row.width - 2 * Theme.horizontalPageMargin) * Math.min(cpu, 100) / 100
        radius: Theme.paddingSmall / 2
        color: Qt.rgba(Diag.loadColor(cpu).r, Diag.loadColor(cpu).g, Diag.loadColor(cpu).b, 0.16)
    }

    Row {
        anchors {
            left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
            leftMargin: Theme.horizontalPageMargin; rightMargin: Theme.horizontalPageMargin
        }
        spacing: Theme.paddingMedium

        Rectangle {
            width: Theme.paddingSmall / 2
            height: Theme.itemSizeSmall * 0.5
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: Diag.stateColor(state)
        }

        Column {
            width: parent.width - cpuCol.width - 2 * parent.spacing - Theme.paddingSmall / 2
            anchors.verticalCenter: parent.verticalCenter
            Label {
                width: parent.width
                text: name
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Label {
                width: parent.width
                text: "PID " + pid + " · " + user + (isKernel ? " · " + qsTr("kernel") : "")
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }
        }

        Column {
            id: cpuCol
            width: Math.max(implicitWidth, Theme.itemSizeSmall * 1.7)
            anchors.verticalCenter: parent.verticalCenter
            Label {
                anchors.right: parent.right
                text: cpu.toFixed(cpu >= 10 ? 0 : 1) + "%"
                font.pixelSize: Theme.fontSizeSmall
                color: Diag.loadColor(cpu)
            }
            Label {
                anchors.right: parent.right
                text: sysmon.fmtBytes(mem)
                font.pixelSize: Theme.fontSizeTiny
                color: Theme.secondaryColor
            }
        }
    }
}
