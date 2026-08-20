import QtQuick 2.0
import Sailfish.Silica 1.0

// Label/value row that wraps long values; optional accent + monospace.
Row {
    property string label
    property string value
    property color valueColor: Theme.primaryColor
    property bool mono: false
    width: parent ? parent.width : implicitWidth
    spacing: Theme.paddingMedium

    Label {
        text: label
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
        width: Math.round(parent.width * 0.34)
        wrapMode: Text.Wrap
    }
    Label {
        text: value.length ? value : "—"
        font.pixelSize: Theme.fontSizeExtraSmall
        font.family: mono ? "monospace" : Theme.fontFamily
        color: valueColor
        width: parent.width - parent.spacing - Math.round(parent.width * 0.34)
        wrapMode: Text.WrapAnywhere
    }
}
