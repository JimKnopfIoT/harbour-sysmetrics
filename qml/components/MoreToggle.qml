import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// Toggle row shown under a list that is capped to the first N entries.
BackgroundItem {
    id: root
    property int total: 0
    property int shown: 10
    property bool expanded: false
    signal toggle()

    visible: total > shown
    width: parent ? parent.width : 0
    height: visible ? Theme.itemSizeSmall : 0
    onClicked: toggle()

    Row {
        anchors.centerIn: parent
        spacing: Theme.paddingSmall
        Label {
            text: root.expanded ? qsTr("Show fewer")
                  : qsTr("Show all %1").arg(root.total)
            font.pixelSize: Theme.fontSizeSmall
            color: Diag.cyan
        }
        Label {
            text: root.expanded ? "▲" : "▼"
            font.pixelSize: Theme.fontSizeSmall
            color: Diag.cyan
        }
    }
}
