import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// Thin horizontal load bar with an animated fill; label optional.
Item {
    id: root
    property real value: 0          // 0..max
    property real maxValue: 100
    property color color: Diag.loadColor(value)
    property string label
    property string caption
    height: label.length || caption.length ? Theme.itemSizeExtraSmall * 0.7 : Theme.paddingMedium

    Row {
        id: labels
        width: parent.width
        visible: label.length || caption.length
        Label {
            text: root.label
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            width: parent.width / 2
            truncationMode: TruncationMode.Fade
        }
        Label {
            text: root.caption
            font.pixelSize: Theme.fontSizeExtraSmall
            color: root.color
            horizontalAlignment: Text.AlignRight
            width: parent.width / 2
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.paddingSmall / 1.5
        radius: height / 2
        color: Diag.grid
        Rectangle {
            height: parent.height
            radius: height / 2
            width: parent.width * Math.max(0, Math.min(1, root.value / root.maxValue))
            color: root.color
            Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
        }
    }
}
