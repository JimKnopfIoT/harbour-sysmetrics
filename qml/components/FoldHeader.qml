import QtQuick 2.0
import Sailfish.Silica 1.0
import "."

// Section header that opens and closes the section below it. Used for the
// detail blocks that belong on a page but not in the way of an overview:
// closed they cost one line, and the count says what is inside without
// opening them.
BackgroundItem {
    id: root
    property string text
    property int count: 0
    property bool open: false
    // set for a member of a grouped run, so the list reads under its header
    property real indent: 0
    signal toggle()

    width: parent ? parent.width : 0
    height: Theme.itemSizeSmall
    onClicked: toggle()

    Label {
        id: mark
        x: Theme.horizontalPageMargin + root.indent
        anchors.verticalCenter: parent.verticalCenter
        text: (root.open ? "▲" : "▼")
              + (root.count > 0 && !root.open ? "  " + root.count : "")
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Diag.cyan
    }
    // Right-aligned like a plain SectionHeader, so an opened block sits at the
    // same edge as every section that is never folded.
    Label {
        anchors {
            left: mark.right; leftMargin: Theme.paddingMedium
            right: parent.right; rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        horizontalAlignment: Text.AlignRight
        truncationMode: TruncationMode.Fade
        text: root.text
        font.pixelSize: Theme.fontSizeSmall
        color: root.open ? Theme.highlightColor : Theme.secondaryHighlightColor
    }
}
