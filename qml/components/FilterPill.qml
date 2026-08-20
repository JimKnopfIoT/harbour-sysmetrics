import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// Toggleable pill for filters and sort keys.
BackgroundItem {
    id: root
    property bool active: false
    property string text
    property color accent: Diag.cyan
    width: pill.width
    height: Theme.itemSizeExtraSmall * 0.8
    contentHeight: height

    Rectangle {
        id: pill
        height: parent.height
        width: label.width + 2 * Theme.paddingMedium
        radius: height / 2
        color: root.active ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.2)
                           : Diag.panel
        border.width: 1
        border.color: root.active ? root.accent : Diag.grid
        Label {
            id: label
            anchors.centerIn: parent
            text: root.text
            font.pixelSize: Theme.fontSizeExtraSmall
            color: root.active ? root.accent : Theme.secondaryColor
        }
    }
}
